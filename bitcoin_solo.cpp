/**
 * Bitcoin Core solo mining (getblocktemplate / submitblock)
 *
 * The legacy non-stratum path in this miner speaks getwork, which Bitcoin Core
 * dropped in 0.10, and its gbt_work_decode() only ever reads the block height.
 * This module replaces both hops for solo mining: it pulls a real block
 * template, builds the coinbase and the merkle root locally, feeds the existing
 * SHA256d CUDA scanner through the unmodified struct work, and reassembles and
 * submits the full block once a nonce is found.
 *
 * Threading: every entry point here runs on the single workio thread
 * (workio_get_work/workio_submit_work), and solo_init() runs before the threads
 * start, so the template registry needs no locking. Do not call into it from
 * a miner thread.
 *
 * Copyright 2026-2027 d0wn3d
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <jansson.h>
#include <curl/curl.h>

#include "miner.h"
#include "compat.h"
#include "solo_script.h"
#include "bitcoin_solo.h"

bool  opt_solo = false;
char *opt_solo_address = NULL;
char *opt_solo_scriptpubkey = NULL;
char *opt_solo_cookie = NULL;
int   opt_solo_refresh = 20;
double opt_solo_test_diff = 0.;

/* ---------------------------------------------------------------- state -- */

struct solo_tx {
	uint8_t *data;
	uint32_t len;
	uint8_t  txid[32];      /* internal order */
	uint8_t  wtxid[32];     /* internal order, witness commitment check only */
};

struct solo_template {
	int      refcount;
	uint32_t height;
	uint32_t version;
	uint32_t curtime;
	uint32_t mintime;
	uint32_t bits;
	uint8_t  prevhash[32];  /* internal order */
	uint64_t coinbasevalue;
	uint8_t  wit_commit[SOLO_MAX_SPK];
	size_t   wit_commit_len;
	struct solo_tx *txs;
	uint32_t tx_count;
	uint32_t target[8];
	time_t   fetched_at;
};

struct solo_job {
	uint32_t id;
	int      refcount;
	struct solo_template *tpl;
	uint64_t extranonce;
	uint8_t *coinbase;      /* witness form, goes into the block */
	size_t   coinbase_len;
	uint8_t  merkleroot[32];
};

#define SOLO_JOB_RING       16
#define SOLO_TEMPLATE_HARD_MAX  120     /* seconds before a stale template is fatal */

static uint8_t  g_payout_spk[SOLO_MAX_SPK];
static size_t   g_payout_spk_len = 0;
static struct solo_template *g_tpl = NULL;
static struct solo_job *g_jobs[SOLO_JOB_RING];
static uint32_t g_next_job_id = 1;
static uint64_t g_extranonce = 0;
static bool     g_force_refresh = false;
static uint32_t g_last_announced = 0;

/* ------------------------------------------------------------ lifecycle -- */

static void tpl_unref(struct solo_template *t)
{
	uint32_t i;
	if (!t || --t->refcount > 0)
		return;
	if (t->txs) {
		for (i = 0; i < t->tx_count; i++)
			free(t->txs[i].data);
		free(t->txs);
	}
	free(t);
}

static void job_unref(struct solo_job *j)
{
	if (!j || --j->refcount > 0)
		return;
	tpl_unref(j->tpl);
	free(j->coinbase);
	free(j);
}

static void job_ring_put(struct solo_job *j)
{
	uint32_t idx = j->id % SOLO_JOB_RING;
	if (g_jobs[idx])
		job_unref(g_jobs[idx]);     /* drop the reference held by the ring */
	g_jobs[idx] = j;                /* the ring now owns one reference */
}

static struct solo_job *job_ring_get(uint32_t id)
{
	struct solo_job *j;
	if (!id)
		return NULL;
	j = g_jobs[id % SOLO_JOB_RING];
	return (j && j->id == id) ? j : NULL;
}

/* ------------------------------------------------------------------ rpc -- */

static json_t *solo_rpc(CURL *curl, const char *req, const char *what)
{
	struct pool_infos *pool = &pools[cur_pooln];
	json_t *val, *err;
	int curl_err = 0;

	val = json_rpc_call_pool(curl, pool, req, false, false, &curl_err);
	if (!val) {
		applog(LOG_ERR, "solo: %s failed (curl %d)", what, curl_err);
		return NULL;
	}
	err = json_object_get(val, "error");
	if (err && !json_is_null(err)) {
		json_t *msg = json_object_get(err, "message");
		applog(LOG_ERR, "solo: %s rejected: %s", what,
			msg && json_is_string(msg) ? json_string_value(msg) : "unknown error");
		json_decref(val);
		return NULL;
	}
	return val;
}

static const char *solo_gbt_req =
	"{\"method\":\"getblocktemplate\",\"params\":"
	"[{\"rules\":[\"segwit\"]}],\"id\":9}\r\n";

/* ------------------------------------------------------------- template -- */

static bool jint(json_t *o, const char *k, uint32_t *out)
{
	json_t *v = json_object_get(o, k);
	if (!v || !json_is_integer(v))
		return false;
	*out = (uint32_t) json_integer_value(v);
	return true;
}

static const char *jstr(json_t *o, const char *k)
{
	json_t *v = json_object_get(o, k);
	return (v && json_is_string(v)) ? json_string_value(v) : NULL;
}

static struct solo_template *solo_template_parse(json_t *res)
{
	struct solo_template *t;
	json_t *txs, *v;
	const char *s;
	uint32_t i;

	t = (struct solo_template *) calloc(1, sizeof(*t));
	if (!t)
		return NULL;
	t->refcount = 1;

	if (!jint(res, "height", &t->height) ||
	    !jint(res, "version", &t->version) ||
	    !jint(res, "curtime", &t->curtime)) {
		applog(LOG_ERR, "solo: block template is missing height/version/curtime");
		goto fail;
	}
	if (!jint(res, "mintime", &t->mintime))
		t->mintime = 0;

	s = jstr(res, "bits");
	{
		uint8_t b[4];
		if (!s || !solo_hex2bin(b, s, 4)) {
			applog(LOG_ERR, "solo: block template has no usable \"bits\"");
			goto fail;
		}
		t->bits = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
		          ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
	}

	s = jstr(res, "previousblockhash");
	if (!s || !solo_hex2bin_rev(t->prevhash, s, 32)) {
		applog(LOG_ERR, "solo: block template has no usable previousblockhash");
		goto fail;
	}

	v = json_object_get(res, "coinbasevalue");
	if (!v || !json_is_integer(v)) {
		applog(LOG_ERR, "solo: block template has no coinbasevalue");
		goto fail;
	}
	t->coinbasevalue = (uint64_t) json_integer_value(v);

	s = jstr(res, "target");
	if (!s || !solo_target_from_hex(t->target, s)) {
		applog(LOG_ERR, "solo: block template has no usable target");
		goto fail;
	}

	s = jstr(res, "default_witness_commitment");
	if (s) {
		size_t n = strlen(s) / 2;
		if (strlen(s) % 2 || n > SOLO_MAX_SPK ||
		    !solo_hex2bin(t->wit_commit, s, n)) {
			applog(LOG_ERR, "solo: unusable default_witness_commitment");
			goto fail;
		}
		t->wit_commit_len = n;
	}

	txs = json_object_get(res, "transactions");
	if (txs && json_is_array(txs)) {
		t->tx_count = (uint32_t) json_array_size(txs);
		if (t->tx_count) {
			t->txs = (struct solo_tx *) calloc(t->tx_count, sizeof(struct solo_tx));
			if (!t->txs)
				goto fail;
		}
		for (i = 0; i < t->tx_count; i++) {
			json_t *e = json_array_get(txs, i);
			struct solo_tx *tx = &t->txs[i];
			size_t hexlen;

			s = jstr(e, "data");
			if (!s) {
				applog(LOG_ERR, "solo: transaction %u has no data", i);
				goto fail;
			}
			hexlen = strlen(s);
			if (hexlen % 2) {
				applog(LOG_ERR, "solo: transaction %u has odd-length data", i);
				goto fail;
			}
			tx->len = (uint32_t)(hexlen / 2);
			tx->data = (uint8_t *) malloc(tx->len);
			if (!tx->data || !solo_hex2bin(tx->data, s, tx->len)) {
				applog(LOG_ERR, "solo: transaction %u has malformed data", i);
				goto fail;
			}

			/* The merkle root is built from TXIDs. sha256d(data) would be the
			 * WTXID for a segwit transaction, so never fall back to it. */
			s = jstr(e, "txid");
			if (!s || !solo_hex2bin_rev(tx->txid, s, 32)) {
				applog(LOG_ERR, "solo: transaction %u has no usable txid; "
					"this Bitcoin Core does not supply what solo mining needs", i);
				goto fail;
			}
			s = jstr(e, "hash");
			if (!s || !solo_hex2bin_rev(tx->wtxid, s, 32))
				memcpy(tx->wtxid, tx->txid, 32);
		}
	}

	t->fetched_at = time(NULL);
	return t;

fail:
	tpl_unref(t);
	return NULL;
}

static struct solo_template *solo_fetch_template(CURL *curl)
{
	struct solo_template *t;
	json_t *val = solo_rpc(curl, solo_gbt_req, "getblocktemplate");
	json_t *res;

	if (!val)
		return NULL;
	res = json_object_get(val, "result");
	if (!res || !json_is_object(res)) {
		applog(LOG_ERR, "solo: getblocktemplate returned no result");
		json_decref(val);
		return NULL;
	}
	t = solo_template_parse(res);
	json_decref(val);
	return t;
}

/* ------------------------------------------------------------------ job -- */

static struct solo_job *solo_job_create(struct solo_template *tpl, uint64_t extranonce)
{
	uint8_t cb[SOLO_MAX_COINBASE], cbnw[SOLO_MAX_COINBASE];
	size_t cblen = 0, cbnwlen = 0;
	uint8_t *leaves;
	struct solo_job *j;
	uint32_t i;

	if (!solo_build_coinbase(cb, sizeof(cb), &cblen, cbnw, sizeof(cbnw), &cbnwlen,
			tpl->height, tpl->coinbasevalue, extranonce,
			g_payout_spk, g_payout_spk_len,
			tpl->wit_commit, tpl->wit_commit_len)) {
		applog(LOG_ERR, "solo: coinbase construction failed");
		return NULL;
	}

	j = (struct solo_job *) calloc(1, sizeof(*j));
	if (!j)
		return NULL;

	leaves = (uint8_t *) malloc(((size_t)tpl->tx_count + 1) * 32);
	if (!leaves) {
		free(j);
		return NULL;
	}
	/* leaf 0 is the coinbase TXID, hashed from the form without witness */
	sha256d(leaves, cbnw, (int) cbnwlen);
	for (i = 0; i < tpl->tx_count; i++)
		memcpy(leaves + (size_t)(i + 1) * 32, tpl->txs[i].txid, 32);

	if (!solo_merkle_root(j->merkleroot, leaves, (size_t)tpl->tx_count + 1)) {
		free(leaves);
		free(j);
		return NULL;
	}
	free(leaves);

	j->coinbase = (uint8_t *) malloc(cblen);
	if (!j->coinbase) {
		free(j);
		return NULL;
	}
	memcpy(j->coinbase, cb, cblen);
	j->coinbase_len = cblen;
	j->extranonce = extranonce;
	j->tpl = tpl;
	tpl->refcount++;
	j->refcount = 1;
	j->id = g_next_job_id++;
	if (!g_next_job_id)
		g_next_job_id = 1;          /* 0 means "no job" */

	return j;
}

/* ------------------------------------------------------------- get work -- */

bool solo_get_work(CURL *curl, struct work *work)
{
	struct solo_job *job;
	struct solo_template *tpl;
	time_t now = time(NULL);
	int i;

	if (!g_payout_spk_len) {
		applog(LOG_ERR, "solo: no payout scriptPubKey, call solo_init() first");
		return false;
	}

	if (!g_tpl || g_force_refresh ||
	    (now - g_tpl->fetched_at) >= opt_solo_refresh) {
		struct solo_template *t = solo_fetch_template(curl);
		if (t) {
			if (g_tpl)
				tpl_unref(g_tpl);
			g_tpl = t;
			g_extranonce = 0;
			g_force_refresh = false;
		} else if (!g_tpl || (now - g_tpl->fetched_at) >= SOLO_TEMPLATE_HARD_MAX) {
			return false;           /* caller retries with a backoff */
		}
	}
	tpl = g_tpl;

	if (tpl->height != g_last_announced) {
		g_last_announced = tpl->height;
		applog(LOG_BLUE, "solo: block %u, %u tx, reward %.8f BTC, diff %.3f",
			tpl->height, tpl->tx_count,
			(double) tpl->coinbasevalue / 1e8,
			target_to_diff(tpl->target));
	}

	job = solo_job_create(tpl, g_extranonce++);
	if (!job)
		return false;
	job_ring_put(job);

	/* Header layout: work->data[i] is the big-endian reading of header bytes
	 * 4i..4i+3, which is exactly what be32enc() in scanhash_sha256d() turns
	 * back into the bytes fed to SHA256d. */
	memset(work->data, 0, sizeof(work->data));
	work->data[0] = swab32(tpl->version);
	for (i = 0; i < 8; i++)
		work->data[1 + i] = be32dec(tpl->prevhash + 4 * i);
	for (i = 0; i < 8; i++)
		work->data[9 + i] = be32dec(job->merkleroot + 4 * i);
	work->data[17] = swab32(tpl->curtime);
	work->data[18] = swab32(tpl->bits);
	work->data[19] = 0;
	work->data[20] = 0x80000000;
	work->data[31] = 0x00000280;

	memcpy(work->target, tpl->target, sizeof(work->target));
	if (opt_solo_test_diff > 0.) {
		/* The SHA256d kernel prefilters on the top 64 bits of the hash against
		 * target[6] alone, so it never fires on a chain whose target[7] is
		 * non-zero (regtest, signet). Handing it a stricter target makes it
		 * report nonces; the blocks stay valid because a hash below a tighter
		 * target is also below the real one. */
		diff_to_target(work->target, opt_solo_test_diff);
	}
	work->targetdiff = target_to_diff(work->target);
	work->height = tpl->height;
	work->solo_id = job->id;
	work->valid_nonces = 0;
	work->tx_count = 0;
	snprintf(work->job_id, sizeof(work->job_id), "%08x solo%u", job->id, tpl->height);

	/* solo mines straight at the network target */
	net_diff = work->targetdiff;

	if (opt_debug)
		applog(LOG_DEBUG, "solo: job %08x height %u extranonce %llu",
			job->id, tpl->height, (unsigned long long) job->extranonce);

	return true;
}

/* --------------------------------------------------------------- submit -- */

/* json_rpc_call() rejects a JSON-null "result", which is exactly what a
 * successful submitblock returns, so its verdict cannot be trusted here. The
 * node's own tip is the only unambiguous answer. */
static bool solo_tip_is(CURL *curl, const char *blockid)
{
	static const char *req =
		"{\"method\":\"getbestblockhash\",\"params\":[],\"id\":5}\r\n";
	json_t *val = solo_rpc(curl, req, "getbestblockhash");
	json_t *res;
	bool same = false;

	if (!val)
		return false;
	res = json_object_get(val, "result");
	if (res && json_is_string(res))
		same = !strcmp(json_string_value(res), blockid);
	json_decref(val);
	return same;
}

static void solo_dump_block(const uint8_t *blk, size_t len, uint32_t height)
{
	char path[256];
	char *hex;
	FILE *f;

	snprintf(path, sizeof(path), "solo_block_%u_%lld.hex",
		height, (long long) time(NULL));
	f = fopen(path, "wb");
	if (!f) {
		applog(LOG_WARNING, "solo: could not write %s", path);
		return;
	}
	hex = (char *) malloc(len * 2 + 1);
	if (hex) {
		solo_bin2hex(hex, blk, len);
		fwrite(hex, 1, len * 2, f);
		free(hex);
	}
	fclose(f);
	applog(LOG_NOTICE, "solo: block candidate written to %s", path);
}

bool solo_submit_block(CURL *curl, struct work *work)
{
	struct solo_job *job;
	struct solo_template *tpl;
	uint32_t _ALIGN(64) hash[8];
	uint8_t hdr[80];
	uint8_t *blk, *p;
	size_t blklen, i;
	char *hex, *req;
	json_t *val, *res, *err;
	bool accepted = false;
	char blockid[65];

	job = job_ring_get(work->solo_id);
	if (!job) {
		applog(LOG_ERR, "solo: job %08x is no longer available, block lost",
			work->solo_id);
		return true;            /* retrying cannot help */
	}
	job->refcount++;
	tpl = job->tpl;

	/* The merkle root in the header must be the one this job built; a mismatch
	 * would mean work and job drifted apart and the block would be garbage. */
	for (i = 0; i < 8; i++) {
		if (work->data[9 + i] != be32dec(job->merkleroot + 4 * i)) {
			applog(LOG_ERR, "solo: header/job merkle mismatch on job %08x, "
				"not submitting", job->id);
			job_unref(job);
			return true;
		}
	}

	solo_header_serialize(hdr, work->data);
	sha256d((unsigned char *) hash, hdr, 80);
	if (!fulltest(hash, work->target)) {
		applog(LOG_ERR, "solo: header does not meet the target, not submitting");
		job_unref(job);
		return true;
	}
	solo_bin2hex_rev(blockid, (const uint8_t *) hash, 32);

	/* header + CompactSize(tx count) + coinbase + template transactions */
	blklen = 80 + 9 + job->coinbase_len;
	for (i = 0; i < tpl->tx_count; i++)
		blklen += tpl->txs[i].len;

	blk = (uint8_t *) malloc(blklen);
	if (!blk) {
		applog(LOG_ERR, "solo: out of memory building the block");
		job_unref(job);
		return true;
	}
	p = blk;
	memcpy(p, hdr, 80);
	p += 80;
	p += solo_compactsize(p, (uint64_t) tpl->tx_count + 1);
	memcpy(p, job->coinbase, job->coinbase_len);
	p += job->coinbase_len;
	for (i = 0; i < tpl->tx_count; i++) {
		memcpy(p, tpl->txs[i].data, tpl->txs[i].len);
		p += tpl->txs[i].len;
	}
	blklen = (size_t)(p - blk);

	solo_dump_block(blk, blklen, tpl->height);

	hex = (char *) malloc(blklen * 2 + 1);
	if (!hex) {
		free(blk);
		job_unref(job);
		return true;
	}
	solo_bin2hex(hex, blk, blklen);
	free(blk);

	/* the 512-byte buffer used by the stratum/getwork path is far too small */
	req = (char *) malloc(blklen * 2 + 96);
	if (!req) {
		free(hex);
		job_unref(job);
		return true;
	}
	sprintf(req, "{\"method\":\"submitblock\",\"params\":[\"%s\"],\"id\":1}\r\n", hex);
	free(hex);

	applog(LOG_NOTICE, "solo: submitting block %u (%u tx, %lu bytes) %s",
		tpl->height, tpl->tx_count + 1, (unsigned long) blklen, blockid);

	/* not solo_rpc(): a successful submitblock trips its error path */
	val = json_rpc_call_pool(curl, &pools[cur_pooln], req, false, false, NULL);
	free(req);

	accepted = solo_tip_is(curl, blockid);

	if (!accepted && val) {
		res = json_object_get(val, "result");
		err = json_object_get(val, "error");
		if (res && json_is_string(res)) {
			const char *reason = json_string_value(res);
			applog(LOG_WARNING, "solo: block %u rejected: %s", tpl->height, reason);
			/* the chain moved on between finding and sending; not our bug */
			if (!strcmp(reason, "duplicate") || !strcmp(reason, "inconclusive") ||
			    !strcmp(reason, "duplicate-inconclusive"))
				applog(LOG_INFO, "solo: another block for this height arrived first");
		} else if (err && !json_is_null(err)) {
			json_t *msg = json_object_get(err, "message");
			applog(LOG_WARNING, "solo: block %u rejected: %s", tpl->height,
				msg && json_is_string(msg) ? json_string_value(msg) : "unknown error");
		} else {
			applog(LOG_WARNING, "solo: block %u did not become the chain tip",
				tpl->height);
		}
	} else if (!accepted) {
		applog(LOG_WARNING, "solo: block %u submission got no reply and did not "
			"become the chain tip", tpl->height);
	}
	if (val)
		json_decref(val);

	if (accepted) {
		pools[work->pooln].accepted_count++;
		pools[work->pooln].solved_count++;
		applog(LOG_NOTICE, "*** BLOCK %u ACCEPTED *** %s", tpl->height, blockid);
	} else {
		pools[work->pooln].rejected_count++;
	}

	g_force_refresh = true;
	job_unref(job);
	return true;
}

/* ----------------------------------------------------------------- init -- */

static bool solo_load_cookie(const char *path)
{
	struct pool_infos *pool = &pools[cur_pooln];
	char line[1024];
	char *sep;
	FILE *f = fopen(path, "rb");

	if (!f)
		return false;
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return false;
	}
	fclose(f);

	line[strcspn(line, "\r\n")] = '\0';
	sep = strchr(line, ':');
	if (!sep)
		return false;
	*sep = '\0';

	snprintf(pool->user, sizeof(pool->user), "%s", line);
	snprintf(pool->pass, sizeof(pool->pass), "%s", sep + 1);
	applog(LOG_INFO, "solo: using RPC cookie %s", path);
	return true;
}

static bool solo_setup_auth(void)
{
	struct pool_infos *pool = &pools[cur_pooln];
	char path[512];
	const char *home;

	if (opt_solo_cookie) {
		if (solo_load_cookie(opt_solo_cookie))
			return true;
		applog(LOG_ERR, "solo: cannot read cookie file %s", opt_solo_cookie);
		return false;
	}
	if (pool->user[0] || pool->pass[0])
		return true;            /* -u/-p already supplied */

#ifdef WIN32
	home = getenv("APPDATA");
	if (home) {
		snprintf(path, sizeof(path), "%s\\Bitcoin\\.cookie", home);
		if (solo_load_cookie(path))
			return true;
	}
#else
	home = getenv("HOME");
	if (home) {
		snprintf(path, sizeof(path), "%s/.bitcoin/.cookie", home);
		if (solo_load_cookie(path))
			return true;
	}
#endif
	applog(LOG_ERR, "solo: no RPC credentials; pass --solo-cookie=<path to .cookie> "
		"or -u/-p");
	return false;
}

bool solo_init(void)
{
	if (opt_solo_scriptpubkey) {
		size_t hexlen = strlen(opt_solo_scriptpubkey);
		if (hexlen % 2 || hexlen / 2 > SOLO_MAX_SPK ||
		    !solo_hex2bin(g_payout_spk, opt_solo_scriptpubkey, hexlen / 2)) {
			applog(LOG_ERR, "solo: --solo-scriptpubkey is not valid hex");
			return false;
		}
		g_payout_spk_len = hexlen / 2;
	} else if (opt_solo_address) {
		if (!solo_address_to_spk(opt_solo_address, g_payout_spk,
				sizeof(g_payout_spk), &g_payout_spk_len)) {
			applog(LOG_ERR, "solo: --solo-address=%s is not a valid Bitcoin address",
				opt_solo_address);
			return false;
		}
	} else {
		applog(LOG_ERR, "solo: --solo needs --solo-address or --solo-scriptpubkey");
		return false;
	}

	{
		char hex[SOLO_MAX_SPK * 2 + 1];
		solo_bin2hex(hex, g_payout_spk, g_payout_spk_len);
		applog(LOG_INFO, "solo: payout scriptPubKey %s", hex);
	}

	if (opt_solo_refresh < 1)
		opt_solo_refresh = 1;

	if (opt_solo_test_diff > 0.)
		applog(LOG_WARNING, "solo: mining at test difficulty %g, not the network "
			"target; only useful on regtest", opt_solo_test_diff);

	return solo_setup_auth();
}

void solo_shutdown(void)
{
	int i;
	for (i = 0; i < SOLO_JOB_RING; i++) {
		if (g_jobs[i]) {
			job_unref(g_jobs[i]);
			g_jobs[i] = NULL;
		}
	}
	if (g_tpl) {
		tpl_unref(g_tpl);
		g_tpl = NULL;
	}
	g_payout_spk_len = 0;
}

/* ------------------------------------------------------------ selftests -- */

int solo_selftest(void)
{
	return solo_script_selftest();
}

static int live_check(bool ok, const char *name)
{
	applog(ok ? LOG_INFO : LOG_ERR, "  %-5s %s", ok ? "ok" : "FAIL", name);
	return ok ? 0 : 1;
}

/* Reproduces default_witness_commitment from the template's own transactions.
 * The coinbase WTXID is defined by BIP141 as 32 zero bytes, so the commitment
 * does not depend on the coinbase we build - it is usable verbatim as long as
 * our witness reserved value is also 32 zero bytes. */
static int live_test_witness_commitment(struct solo_template *tpl)
{
	uint8_t *leaves;
	uint8_t wmr[32], buf[64], commit[32], spk[38];
	uint32_t i;
	int fail;

	if (!tpl->wit_commit_len)
		return live_check(true, "template is not segwit, commitment skipped");

	leaves = (uint8_t *) malloc(((size_t)tpl->tx_count + 1) * 32);
	if (!leaves)
		return 1;
	memset(leaves, 0, 32);
	for (i = 0; i < tpl->tx_count; i++)
		memcpy(leaves + (size_t)(i + 1) * 32, tpl->txs[i].wtxid, 32);

	if (!solo_merkle_root(wmr, leaves, (size_t)tpl->tx_count + 1)) {
		free(leaves);
		return 1;
	}
	free(leaves);

	memcpy(buf, wmr, 32);
	memset(buf + 32, 0, 32);            /* witness reserved value */
	sha256d(commit, buf, 64);

	spk[0] = 0x6a;                      /* OP_RETURN */
	spk[1] = 0x24;                      /* push 36 */
	spk[2] = 0xaa; spk[3] = 0x21; spk[4] = 0xa9; spk[5] = 0xed;
	memcpy(spk + 6, commit, 32);

	fail = live_check(tpl->wit_commit_len == sizeof(spk) &&
		!memcmp(tpl->wit_commit, spk, sizeof(spk)),
		"default_witness_commitment reproduced from transactions[].hash");
	return fail;
}

/* Rebuilds the merkle root of an already mined block from its TXIDs. */
static int live_test_known_block(CURL *curl, uint32_t height)
{
	char req[160];
	json_t *val, *res, *txs;
	uint8_t *leaves, root[32];
	char hash[65], got[65];
	const char *want;
	size_t n, i;
	int fail = 1;

	snprintf(req, sizeof(req),
		"{\"method\":\"getblockhash\",\"params\":[%u],\"id\":2}\r\n", height);
	val = solo_rpc(curl, req, "getblockhash");
	if (!val)
		return 1;
	res = json_object_get(val, "result");
	if (!res || !json_is_string(res)) {
		json_decref(val);
		return 1;
	}
	snprintf(hash, sizeof(hash), "%s", json_string_value(res));
	json_decref(val);

	snprintf(req, sizeof(req),
		"{\"method\":\"getblock\",\"params\":[\"%s\",1],\"id\":3}\r\n", hash);
	val = solo_rpc(curl, req, "getblock");
	if (!val)
		return 1;
	res = json_object_get(val, "result");
	txs = res ? json_object_get(res, "tx") : NULL;
	want = res ? jstr(res, "merkleroot") : NULL;
	if (!txs || !json_is_array(txs) || !want) {
		json_decref(val);
		return 1;
	}

	n = json_array_size(txs);
	leaves = (uint8_t *) malloc(n * 32);
	if (leaves) {
		bool ok = true;
		for (i = 0; i < n; i++) {
			json_t *e = json_array_get(txs, i);
			if (!json_is_string(e) ||
			    !solo_hex2bin_rev(leaves + i * 32, json_string_value(e), 32)) {
				ok = false;
				break;
			}
		}
		if (ok && solo_merkle_root(root, leaves, n)) {
			char label[128];
			solo_bin2hex_rev(got, root, 32);
			snprintf(label, sizeof(label),
				"merkle root of block %u rebuilt from %u txids",
				height, (unsigned) n);
			fail = live_check(!strcmp(got, want), label);
		}
		free(leaves);
	}
	json_decref(val);
	return fail;
}

/* Builds a coinbase for the live template and has Bitcoin Core decode it, so
 * the TXID we feed the merkle tree is confirmed by the node itself. */
static int live_test_coinbase_txid(CURL *curl, struct solo_template *tpl)
{
	uint8_t cb[SOLO_MAX_COINBASE], cbnw[SOLO_MAX_COINBASE], txid[32];
	size_t cblen, cbnwlen;
	char hexbuf[SOLO_MAX_COINBASE * 2 + 1], want[65];
	char *req;
	json_t *val, *res;
	const char *s;
	int fail;

	if (!solo_build_coinbase(cb, sizeof(cb), &cblen, cbnw, sizeof(cbnw), &cbnwlen,
			tpl->height, tpl->coinbasevalue, 0x1122334455667788ULL,
			g_payout_spk, g_payout_spk_len,
			tpl->wit_commit, tpl->wit_commit_len))
		return 1;

	sha256d(txid, cbnw, (int) cbnwlen);
	solo_bin2hex_rev(want, txid, 32);

	solo_bin2hex(hexbuf, cb, cblen);
	req = (char *) malloc(strlen(hexbuf) + 96);
	if (!req)
		return 1;
	sprintf(req, "{\"method\":\"decoderawtransaction\",\"params\":[\"%s\"],\"id\":4}\r\n",
		hexbuf);

	val = solo_rpc(curl, req, "decoderawtransaction");
	free(req);
	if (!val)
		return 1;
	res = json_object_get(val, "result");
	s = res ? jstr(res, "txid") : NULL;
	fail = live_check(s && !strcmp(s, want),
		"Bitcoin Core agrees with our coinbase TXID");
	if (s && strcmp(s, want))
		applog(LOG_ERR, "    core=%s ours=%s", s, want);
	json_decref(val);
	return fail;
}

int solo_selftest_live(CURL *curl)
{
	struct solo_template *tpl;
	uint32_t bits_target[8];
	int fail = 0;

	applog(LOG_NOTICE, "--- solo live tests against Bitcoin Core ---");

	tpl = solo_fetch_template(curl);
	if (!tpl) {
		applog(LOG_ERR, "  FAIL  getblocktemplate");
		return 1;
	}
	fail += live_check(true, "getblocktemplate parsed");
	applog(LOG_INFO, "    height %u, %u transactions, reward %.8f BTC",
		tpl->height, tpl->tx_count, (double) tpl->coinbasevalue / 1e8);

	fail += live_check(solo_target_from_bits(bits_target, tpl->bits) &&
		!memcmp(bits_target, tpl->target, sizeof(bits_target)),
		"target field matches the target decompressed from nBits");

	fail += live_test_witness_commitment(tpl);
	fail += live_test_coinbase_txid(curl, tpl);
	if (tpl->height > 1)
		fail += live_test_known_block(curl, tpl->height - 1);

	tpl_unref(tpl);

	applog(fail ? LOG_ERR : LOG_NOTICE, "--- live tests: %d failure(s) ---", fail);
	return fail;
}
