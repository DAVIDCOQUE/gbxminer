/**
 * Bitcoin consensus serialization primitives for solo mining.
 *
 * Copyright 2026-2027 d0wn3d
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solo_script.h"

/* ------------------------------------------------------------------ hex -- */

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool solo_hex2bin(uint8_t *out, const char *hex, size_t len)
{
	size_t i;
	if (!out || !hex || strlen(hex) != len * 2)
		return false;
	for (i = 0; i < len; i++) {
		int hi = hexval(hex[2*i]), lo = hexval(hex[2*i+1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

bool solo_hex2bin_rev(uint8_t *out, const char *hex, size_t len)
{
	size_t i;
	if (!solo_hex2bin(out, hex, len))
		return false;
	for (i = 0; i < len / 2; i++) {
		uint8_t t = out[i];
		out[i] = out[len - 1 - i];
		out[len - 1 - i] = t;
	}
	return true;
}

void solo_bin2hex(char *out, const uint8_t *in, size_t len)
{
	static const char d[] = "0123456789abcdef";
	size_t i;
	for (i = 0; i < len; i++) {
		out[2*i]   = d[in[i] >> 4];
		out[2*i+1] = d[in[i] & 0xf];
	}
	out[2*len] = '\0';
}

void solo_bin2hex_rev(char *out, const uint8_t *in, size_t len)
{
	static const char d[] = "0123456789abcdef";
	size_t i;
	for (i = 0; i < len; i++) {
		uint8_t b = in[len - 1 - i];
		out[2*i]   = d[b >> 4];
		out[2*i+1] = d[b & 0xf];
	}
	out[2*len] = '\0';
}

/* -------------------------------------------------------- serialization -- */

void solo_put_u32le(uint8_t *out, uint32_t v)
{
	out[0] = (uint8_t)(v);
	out[1] = (uint8_t)(v >> 8);
	out[2] = (uint8_t)(v >> 16);
	out[3] = (uint8_t)(v >> 24);
}

void solo_put_u64le(uint8_t *out, uint64_t v)
{
	int i;
	for (i = 0; i < 8; i++)
		out[i] = (uint8_t)(v >> (8 * i));
}

size_t solo_compactsize(uint8_t *out, uint64_t n)
{
	if (n < 0xfdULL) {
		out[0] = (uint8_t)n;
		return 1;
	}
	if (n <= 0xffffULL) {
		out[0] = 0xfd;
		out[1] = (uint8_t)(n);
		out[2] = (uint8_t)(n >> 8);
		return 3;
	}
	if (n <= 0xffffffffULL) {
		out[0] = 0xfe;
		solo_put_u32le(out + 1, (uint32_t)n);
		return 5;
	}
	out[0] = 0xff;
	solo_put_u64le(out + 1, n);
	return 9;
}

size_t solo_push_scriptnum(uint8_t *out, uint32_t n)
{
	uint8_t tmp[5];
	size_t len = 0;
	uint32_t v = n;

	/* Bitcoin Core checks BIP34 by rebuilding CScript() << nHeight and
	 * comparing bytes, so this has to match CScript::operator<< exactly:
	 * 0 and 1..16 collapse to the OP_0/OP_N opcodes rather than a push. */
	if (n == 0) {
		out[0] = 0x00;                  /* OP_0 */
		return 1;
	}
	if (n <= 16) {
		out[0] = (uint8_t)(0x50 + n);   /* OP_1 .. OP_16 */
		return 1;
	}

	/* CScriptNum: little-endian magnitude, sign bit in the top byte. A value
	 * whose high bit is set needs a trailing zero so it stays positive.
	 * Not a CompactSize: BIP34 requires the height as a script number. */
	while (v) {
		tmp[len++] = (uint8_t)(v & 0xff);
		v >>= 8;
	}
	if (len && (tmp[len-1] & 0x80))
		tmp[len++] = 0x00;

	out[0] = (uint8_t)len;              /* len <= 5 < OP_PUSHDATA1 */
	if (len)
		memcpy(out + 1, tmp, len);
	return len + 1;
}

/* ------------------------------------------------------------- bech32 ---- */

static const char *B32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static uint32_t bech32_polymod(const uint8_t *v, size_t len)
{
	static const uint32_t GEN[5] = {
		0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3
	};
	uint32_t chk = 1;
	size_t i;
	int j;

	for (i = 0; i < len; i++) {
		uint8_t top = (uint8_t)(chk >> 25);
		chk = ((chk & 0x1ffffff) << 5) ^ v[i];
		for (j = 0; j < 5; j++)
			if ((top >> j) & 1)
				chk ^= GEN[j];
	}
	return chk;
}

/* 5-bit groups -> 8-bit bytes, no padding allowed on the way out */
static bool convertbits_5to8(uint8_t *out, size_t *outlen, const uint8_t *in, size_t inlen)
{
	uint32_t acc = 0;
	int bits = 0;
	size_t n = 0;
	size_t i;

	for (i = 0; i < inlen; i++) {
		if (in[i] >> 5)
			return false;
		acc = (acc << 5) | in[i];
		bits += 5;
		while (bits >= 8) {
			bits -= 8;
			out[n++] = (uint8_t)((acc >> bits) & 0xff);
		}
	}
	/* leftover bits must be zero padding, and fewer than one full byte */
	if (bits >= 5 || ((acc << (8 - bits)) & 0xff))
		return false;
	*outlen = n;
	return true;
}

/* Returns witness version (0..16) and program, or -1. */
static int bech32_decode_addr(const char *addr, uint8_t *prog, size_t *proglen)
{
	char lower[100];
	uint8_t values[100];
	uint8_t payload[100];
	uint8_t expanded[200];
	size_t len = strlen(addr);
	size_t hrplen, datalen, i, n = 0;
	int sep = -1;
	bool has_lower = false, has_upper = false;
	uint32_t chk;
	int witver;

	if (len < 8 || len > 90)
		return -1;

	for (i = 0; i < len; i++) {
		char c = addr[i];
		if (c < 33 || c > 126)
			return -1;
		if (c >= 'a' && c <= 'z') has_lower = true;
		if (c >= 'A' && c <= 'Z') has_upper = true;
		lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	lower[len] = '\0';
	if (has_lower && has_upper)             /* mixed case is invalid */
		return -1;

	for (i = 0; i < len; i++)
		if (lower[i] == '1')
			sep = (int)i;
	if (sep < 1 || (size_t)sep + 7 > len)
		return -1;

	hrplen = (size_t)sep;
	datalen = len - hrplen - 1;
	if (datalen < 6)
		return -1;

	/* only the prefixes we can actually mine to */
	if (!(hrplen == 2 && (!memcmp(lower, "bc", 2) || !memcmp(lower, "tb", 2))) &&
	    !(hrplen == 4 && !memcmp(lower, "bcrt", 4)))
		return -1;

	for (i = 0; i < datalen; i++) {
		const char *p = strchr(B32_CHARSET, lower[hrplen + 1 + i]);
		if (!p)
			return -1;
		values[i] = (uint8_t)(p - B32_CHARSET);
	}

	for (i = 0; i < hrplen; i++)
		expanded[n++] = (uint8_t)(lower[i] >> 5);
	expanded[n++] = 0;
	for (i = 0; i < hrplen; i++)
		expanded[n++] = (uint8_t)(lower[i] & 31);
	for (i = 0; i < datalen; i++)
		expanded[n++] = values[i];

	chk = bech32_polymod(expanded, n);
	if (chk != 1 && chk != 0x2bc830a3)
		return -1;

	witver = values[0];
	if (witver > 16)
		return -1;
	/* v0 uses bech32, v1+ uses bech32m (BIP350) */
	if (witver == 0 ? (chk != 1) : (chk != 0x2bc830a3))
		return -1;

	if (!convertbits_5to8(payload, proglen, values + 1, datalen - 1 - 6))
		return -1;
	if (*proglen < 2 || *proglen > 40)
		return -1;
	if (witver == 0 && *proglen != 20 && *proglen != 32)
		return -1;

	memcpy(prog, payload, *proglen);
	return witver;
}

/* ------------------------------------------------------------- base58 ---- */

static const char *B58_CHARSET =
	"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static bool base58check_decode(const char *s, uint8_t *out25)
{
	uint8_t acc[64];
	uint8_t hash[32];
	size_t len = strlen(s);
	size_t zeros = 0, i, first, total;
	int j;

	if (len < 26 || len > 40)
		return false;

	memset(acc, 0, sizeof(acc));
	for (i = 0; i < len; i++) {
		const char *p = strchr(B58_CHARSET, s[i]);
		int carry;
		if (!p)
			return false;
		carry = (int)(p - B58_CHARSET);
		for (j = (int)sizeof(acc) - 1; j >= 0; j--) {
			carry += 58 * acc[j];
			acc[j] = (uint8_t)(carry & 0xff);
			carry >>= 8;
		}
		if (carry)                      /* wider than the buffer */
			return false;
	}

	while (zeros < len && s[zeros] == '1')
		zeros++;

	for (first = 0; first < sizeof(acc) && !acc[first]; first++)
		;
	total = zeros + (sizeof(acc) - first);
	if (total != 25)
		return false;

	memset(out25, 0, zeros);
	memcpy(out25 + zeros, acc + first, sizeof(acc) - first);

	sha256d(hash, out25, 21);
	if (memcmp(hash, out25 + 21, 4))
		return false;

	return true;
}

/* ------------------------------------------------------------ address ---- */

bool solo_address_to_spk(const char *addr, uint8_t *spk, size_t spk_max, size_t *spk_len)
{
	uint8_t prog[40];
	uint8_t dec[25];
	size_t proglen = 0;
	int witver;

	if (!addr || !spk || !spk_len)
		return false;

	witver = bech32_decode_addr(addr, prog, &proglen);
	if (witver >= 0) {
		if (spk_max < proglen + 2)
			return false;
		spk[0] = witver ? (uint8_t)(0x50 + witver) : 0x00;  /* OP_0 / OP_n */
		spk[1] = (uint8_t)proglen;
		memcpy(spk + 2, prog, proglen);
		*spk_len = proglen + 2;
		return true;
	}

	if (!base58check_decode(addr, dec))
		return false;

	switch (dec[0]) {
	case 0x00:      /* P2PKH mainnet */
	case 0x6f:      /* P2PKH testnet/signet/regtest */
		if (spk_max < 25)
			return false;
		spk[0] = 0x76;                  /* OP_DUP */
		spk[1] = 0xa9;                  /* OP_HASH160 */
		spk[2] = 0x14;
		memcpy(spk + 3, dec + 1, 20);
		spk[23] = 0x88;                 /* OP_EQUALVERIFY */
		spk[24] = 0xac;                 /* OP_CHECKSIG */
		*spk_len = 25;
		return true;
	case 0x05:      /* P2SH mainnet */
	case 0xc4:      /* P2SH testnet/signet/regtest */
		if (spk_max < 23)
			return false;
		spk[0] = 0xa9;                  /* OP_HASH160 */
		spk[1] = 0x14;
		memcpy(spk + 2, dec + 1, 20);
		spk[22] = 0x87;                 /* OP_EQUAL */
		*spk_len = 23;
		return true;
	default:
		return false;
	}
}

/* ------------------------------------------------------------- merkle ---- */

bool solo_merkle_root(uint8_t *out32, const uint8_t *leaves, size_t count)
{
	uint8_t *cur;
	uint8_t buf[64];
	size_t n = count, i;

	if (!out32 || !leaves || !count)
		return false;

	/* an odd level duplicates its last hash, so one extra slot is enough */
	cur = (uint8_t *)malloc((count + 1) * 32);
	if (!cur)
		return false;
	memcpy(cur, leaves, count * 32);

	while (n > 1) {
		if (n & 1) {
			memcpy(cur + n * 32, cur + (n - 1) * 32, 32);
			n++;
		}
		for (i = 0; i < n / 2; i++) {
			memcpy(buf, cur + (2 * i) * 32, 32);
			memcpy(buf + 32, cur + (2 * i + 1) * 32, 32);
			sha256d(cur + i * 32, buf, 64);
		}
		n /= 2;
	}

	memcpy(out32, cur, 32);
	free(cur);
	return true;
}

/* ------------------------------------------------------------- target ---- */

static uint32_t be32_at(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void target_from_be32(uint32_t *target, const uint8_t *be)
{
	int i;
	/* target[7] is the most significant word, as fulltest() expects */
	for (i = 0; i < 8; i++)
		target[7 - i] = be32_at(be + 4 * i);
}

bool solo_target_from_bits(uint32_t *target, uint32_t bits)
{
	uint8_t be[32];
	uint32_t mant = bits & 0x007fffff;
	uint32_t exp = bits >> 24;

	if (bits & 0x00800000)          /* negative target is never valid */
		return false;

	memset(be, 0, sizeof(be));
	if (!mant) {
		target_from_be32(target, be);
		return true;
	}

	if (exp <= 3) {
		mant >>= 8 * (3 - exp);
		be[31] = (uint8_t)(mant);
		be[30] = (uint8_t)(mant >> 8);
		be[29] = (uint8_t)(mant >> 16);
	} else {
		if (exp > 32)
			return false;
		/* mantissa occupies the three bytes ending at offset 32-exp+2 */
		size_t off = 32 - exp;
		be[off]     = (uint8_t)(mant >> 16);
		be[off + 1] = (uint8_t)(mant >> 8);
		be[off + 2] = (uint8_t)(mant);
	}

	target_from_be32(target, be);
	return true;
}

bool solo_target_from_hex(uint32_t *target, const char *hex64)
{
	uint8_t be[32];
	if (!solo_hex2bin(be, hex64, 32))
		return false;
	target_from_be32(target, be);
	return true;
}

/* ------------------------------------------------------------- header ---- */

void solo_header_serialize(uint8_t *out80, const uint32_t *data)
{
	int i;
	/* work->data[i] holds the big-endian reading of header bytes 4i..4i+3,
	 * which is what be32enc() in scanhash_sha256d() reproduces. */
	for (i = 0; i < 20; i++) {
		out80[4*i]   = (uint8_t)(data[i] >> 24);
		out80[4*i+1] = (uint8_t)(data[i] >> 16);
		out80[4*i+2] = (uint8_t)(data[i] >> 8);
		out80[4*i+3] = (uint8_t)(data[i]);
	}
}

void solo_header_to_words(uint32_t *data, const uint8_t *hdr80)
{
	int i;
	for (i = 0; i < 20; i++)
		data[i] = be32_at(hdr80 + 4 * i);
}

/* ----------------------------------------------------------- coinbase ---- */

bool solo_build_coinbase(uint8_t *out, size_t out_max, size_t *out_len,
	uint8_t *out_nw, size_t out_nw_max, size_t *out_nw_len,
	uint32_t height, uint64_t value, uint64_t extranonce,
	const uint8_t *spk, size_t spk_len,
	const uint8_t *wit_commit, size_t wit_commit_len)
{
	uint8_t sig[SOLO_SCRIPTSIG_MAX];
	uint8_t mid[SOLO_MAX_COINBASE];         /* input + outputs, shared */
	size_t siglen = 0, midlen = 0;
	size_t n = 0, m = 0;
	bool segwit = (wit_commit_len > 0);

	if (!out || !out_len || !out_nw || !out_nw_len || !spk)
		return false;
	if (!spk_len || spk_len > SOLO_MAX_SPK)
		return false;
	if (wit_commit_len > SOLO_MAX_SPK)
		return false;

	/* scriptSig: BIP34 height first, then our extranonce */
	siglen += solo_push_scriptnum(sig + siglen, height);
	sig[siglen++] = 0x08;
	solo_put_u64le(sig + siglen, extranonce);
	siglen += 8;
	if (siglen < SOLO_SCRIPTSIG_MIN || siglen > SOLO_SCRIPTSIG_MAX)
		return false;

	/* --- shared middle: input list and output list --- */
	mid[midlen++] = 0x01;                           /* vin count */
	memset(mid + midlen, 0, 32);                    /* null prevout hash */
	midlen += 32;
	memset(mid + midlen, 0xff, 4);                  /* prevout index */
	midlen += 4;
	midlen += solo_compactsize(mid + midlen, siglen);
	memcpy(mid + midlen, sig, siglen);
	midlen += siglen;
	memset(mid + midlen, 0xff, 4);                  /* sequence */
	midlen += 4;

	mid[midlen++] = segwit ? 0x02 : 0x01;           /* vout count */
	solo_put_u64le(mid + midlen, value);
	midlen += 8;
	midlen += solo_compactsize(mid + midlen, spk_len);
	memcpy(mid + midlen, spk, spk_len);
	midlen += spk_len;
	if (segwit) {
		solo_put_u64le(mid + midlen, 0);            /* zero-value OP_RETURN */
		midlen += 8;
		midlen += solo_compactsize(mid + midlen, wit_commit_len);
		memcpy(mid + midlen, wit_commit, wit_commit_len);
		midlen += wit_commit_len;
	}

	/* --- form without witness: this is what the TXID hashes --- */
	if (out_nw_max < midlen + 8)
		return false;
	solo_put_u32le(out_nw + m, 1);                  /* version */
	m += 4;
	memcpy(out_nw + m, mid, midlen);
	m += midlen;
	solo_put_u32le(out_nw + m, 0);                  /* locktime */
	m += 4;
	*out_nw_len = m;

	/* --- form with witness: this is what goes into the block --- */
	if (out_max < midlen + 8 + (segwit ? 36 : 0))
		return false;
	solo_put_u32le(out + n, 1);
	n += 4;
	if (segwit) {
		out[n++] = 0x00;                            /* marker */
		out[n++] = 0x01;                            /* flag */
	}
	memcpy(out + n, mid, midlen);
	n += midlen;
	if (segwit) {
		out[n++] = 0x01;                            /* one witness item */
		out[n++] = 0x20;                            /* 32 bytes */
		memset(out + n, 0, 32);                     /* witness reserved value */
		n += 32;
	}
	solo_put_u32le(out + n, 0);
	n += 4;
	*out_len = n;

	return true;
}

/* -------------------------------------------------------------- tests ---- */

static int g_fail;

static void check(bool ok, const char *name)
{
	if (ok) {
		printf("  ok    %s\n", name);
	} else {
		printf("  FAIL  %s\n", name);
		g_fail++;
	}
}

static bool hexeq(const uint8_t *bin, size_t len, const char *expect)
{
	char buf[512];
	if (len * 2 + 1 > sizeof(buf))
		return false;
	solo_bin2hex(buf, bin, len);
#ifdef _MSC_VER
	return _stricmp(buf, expect) == 0;
#else
	return strcasecmp(buf, expect) == 0;
#endif
}

static void test_compactsize(void)
{
	uint8_t b[9];
	printf("CompactSize\n");
	check(solo_compactsize(b, 0) == 1 && hexeq(b, 1, "00"), "0");
	check(solo_compactsize(b, 252) == 1 && hexeq(b, 1, "fc"), "252");
	check(solo_compactsize(b, 253) == 3 && hexeq(b, 3, "fdfd00"), "253");
	check(solo_compactsize(b, 65535) == 3 && hexeq(b, 3, "fdffff"), "65535");
	check(solo_compactsize(b, 65536) == 5 && hexeq(b, 5, "fe00000100"), "65536");
	check(solo_compactsize(b, 0x100000000ULL) == 9 &&
	      hexeq(b, 9, "ff0000000001000000"), "2^32");
}

static void test_scriptnum(void)
{
	uint8_t b[6];
	printf("BIP34 height push\n");
	/* CScript() << n collapses 0 and 1..16 into single opcodes */
	check(solo_push_scriptnum(b, 0) == 1 && hexeq(b, 1, "00"), "0 is OP_0");
	check(solo_push_scriptnum(b, 1) == 1 && hexeq(b, 1, "51"), "1 is OP_1");
	check(solo_push_scriptnum(b, 16) == 1 && hexeq(b, 1, "60"), "16 is OP_16");
	check(solo_push_scriptnum(b, 17) == 2 && hexeq(b, 2, "0111"), "17 is a push");
	check(solo_push_scriptnum(b, 127) == 2 && hexeq(b, 2, "017f"), "127");
	check(solo_push_scriptnum(b, 128) == 3 && hexeq(b, 3, "028000"), "128 needs pad");
	check(solo_push_scriptnum(b, 255) == 3 && hexeq(b, 3, "02ff00"), "255 needs pad");
	check(solo_push_scriptnum(b, 256) == 3 && hexeq(b, 3, "020001"), "256");
	check(solo_push_scriptnum(b, 32767) == 3 && hexeq(b, 3, "02ff7f"), "32767");
	check(solo_push_scriptnum(b, 32768) == 4 && hexeq(b, 4, "03008000"), "32768 needs pad");
	/* 227836 = 0x0379fc, the first BIP34-enforcing height */
	check(solo_push_scriptnum(b, 227836) == 4 && hexeq(b, 4, "03fc7903"), "227836");
	check(solo_push_scriptnum(b, 900000) == 4 && hexeq(b, 4, "03a0bb0d"), "900000");
	check(solo_push_scriptnum(b, 8388608) == 5 && hexeq(b, 5, "0400008000"),
	      "8388608 needs pad");
}

struct addr_vec { const char *addr; const char *spk; };

static void test_address(void)
{
	/* BIP173/BIP350 test vectors plus well-known base58 addresses */
	static const struct addr_vec ok[] = {
		{ "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4",
		  "0014751e76e8199196d454941c45d1b3a323f1433bd6" },
		{ "BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4",
		  "0014751e76e8199196d454941c45d1b3a323f1433bd6" },
		{ "bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3",
		  "00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262" },
		{ "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0",
		  "512079be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798" },
		{ "tb1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3q0sl5k7",
		  "00201863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262" },
		{ "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2",
		  "76a91477bff20c60e522dfaa3350c39b030a5d004e839a88ac" },
		{ "3EktnHQD7RiAE6uzMj2ZifT9YgRrkSgzQX",
		  "a9148f55563b9a19f321c211e9b9f38cdf686ea0784587" },
	};
	static const char *bad[] = {
		/* v0 program must use bech32, not bech32m (BIP350 invalid vector) */
		"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kemeawh",
		/* v1 program must use bech32m, not bech32 */
		"bc1p38j9r5y49hruaue7wxjce0updqjuyyx0kh56v8s25huc6995vvpql3jow4",
		"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t5",  /* bad checksum */
		"BC1QW508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4",  /* mixed case */
		"1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN3",          /* bad base58 checksum */
		"xc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4",  /* unknown hrp */
		"",
	};
	uint8_t spk[SOLO_MAX_SPK];
	size_t spklen;
	size_t i;

	printf("Address -> scriptPubKey\n");
	for (i = 0; i < sizeof(ok)/sizeof(ok[0]); i++) {
		bool r = solo_address_to_spk(ok[i].addr, spk, sizeof(spk), &spklen);
		check(r && hexeq(spk, spklen, ok[i].spk), ok[i].addr);
	}
	for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
		bool r = solo_address_to_spk(bad[i], spk, sizeof(spk), &spklen);
		char label[128];
		snprintf(label, sizeof(label), "rejects \"%s\"", bad[i]);
		check(!r, label);
	}
}

static void test_merkle(void)
{
	uint8_t leaves[4][32];
	uint8_t root[32];
	uint8_t expect[32];
	uint8_t a[64], b[64], l1[32], l2[32], manual[32];
	char hex[65];

	printf("Merkle root\n");

	/* block 1: a single transaction, root equals the coinbase txid */
	solo_hex2bin_rev(leaves[0],
		"0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098", 32);
	check(solo_merkle_root(root, leaves[0], 1) &&
	      !memcmp(root, leaves[0], 32), "1 leaf (block 1)");

	/* block 170: two transactions */
	solo_hex2bin_rev(leaves[0],
		"b1fea52486ce0c62bb442b530a3f0132b826c74e473d1f2c220bfa78111c5082", 32);
	solo_hex2bin_rev(leaves[1],
		"f4184fc596403b9d638783cf57adfe4c75c605f6356fbc91338530e9831e9e16", 32);
	solo_hex2bin_rev(expect,
		"7dac2c5666815c17a3b36427de37bb9d2e2c5ccec3f8633eb91a4205cb4c10ff", 32);
	check(solo_merkle_root(root, leaves[0], 2) &&
	      !memcmp(root, expect, 32), "2 leaves (block 170)");

	/* odd level must duplicate the last hash: root == H(H(A|B) | H(C|C)) */
	memset(leaves[0], 0xaa, 32);
	memset(leaves[1], 0xbb, 32);
	memset(leaves[2], 0xcc, 32);
	memcpy(a, leaves[0], 32);
	memcpy(a + 32, leaves[1], 32);
	sha256d(l1, a, 64);
	memcpy(b, leaves[2], 32);
	memcpy(b + 32, leaves[2], 32);
	sha256d(l2, b, 64);
	memcpy(a, l1, 32);
	memcpy(a + 32, l2, 32);
	sha256d(manual, a, 64);
	check(solo_merkle_root(root, leaves[0], 3) &&
	      !memcmp(root, manual, 32), "3 leaves duplicate the last hash");

	check(!solo_merkle_root(root, leaves[0], 0), "rejects 0 leaves");

	/* display order round trip */
	solo_bin2hex_rev(hex, expect, 32);
	check(!strcmp(hex,
		"7dac2c5666815c17a3b36427de37bb9d2e2c5ccec3f8633eb91a4205cb4c10ff"),
		"internal/display hex round trip");
}

static void test_target(void)
{
	uint32_t t[8], u[8];

	printf("Target\n");
	check(solo_target_from_bits(t, 0x1d00ffff) &&
	      t[7] == 0x00000000 && t[6] == 0xffff0000 &&
	      t[5] == 0 && t[4] == 0 && t[3] == 0 &&
	      t[2] == 0 && t[1] == 0 && t[0] == 0, "bits 1d00ffff (difficulty 1)");

	check(solo_target_from_hex(u,
		"00000000ffff0000000000000000000000000000000000000000000000000000") &&
	      !memcmp(t, u, sizeof(t)), "hex target matches bits");

	check(solo_target_from_bits(t, 0x1b0404cb) &&
	      t[6] == 0x000404cb && t[7] == 0, "bits 1b0404cb");

	check(solo_target_from_bits(t, 0x170355f0) && t[6] == 0x00000000 &&
	      t[5] == 0x000355f0, "bits 170355f0 (modern mainnet)");

	check(!solo_target_from_bits(t, 0x1d80ffff), "rejects negative target");
}

static void test_header(void)
{
	/* Bitcoin block 1 - validates the whole endianness chain at once */
	uint32_t data[20];
	uint8_t hdr[80], hash[32];
	char hex[65];
	int i;

	printf("Header serialization\n");

	memset(data, 0, sizeof(data));
	data[0] = 0x01000000;                   /* swab32(version 1) */

	{
		uint8_t prev[32], merkle[32];
		solo_hex2bin_rev(prev,
			"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f", 32);
		solo_hex2bin_rev(merkle,
			"0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098", 32);
		for (i = 0; i < 8; i++)
			data[1 + i] = ((uint32_t)prev[4*i] << 24) | ((uint32_t)prev[4*i+1] << 16) |
			              ((uint32_t)prev[4*i+2] << 8) | prev[4*i+3];
		for (i = 0; i < 8; i++)
			data[9 + i] = ((uint32_t)merkle[4*i] << 24) | ((uint32_t)merkle[4*i+1] << 16) |
			              ((uint32_t)merkle[4*i+2] << 8) | merkle[4*i+3];
	}
	/* swab32 of curtime / bits / nonce, as stratum_gen_work stores them */
	data[17] = 0x61bc6649;                  /* time   1231469665 = 0x4966bc61 */
	data[18] = 0xffff001d;                  /* bits   0x1d00ffff */
	data[19] = 0x01e36299;                  /* nonce  2573394689 = 0x9962e301 */

	solo_header_serialize(hdr, data);
	sha256d(hash, hdr, 80);
	solo_bin2hex_rev(hex, hash, 32);
	check(!strcmp(hex,
		"00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048"),
		"block 1 header hashes to its known block id");

	{
		uint32_t back[20];
		solo_header_to_words(back, hdr);
		check(!memcmp(back, data, sizeof(data)), "words -> bytes -> words");
	}
}

static void test_coinbase(void)
{
	uint8_t spk[SOLO_MAX_SPK], commit[38];
	uint8_t cb[SOLO_MAX_COINBASE], cbnw[SOLO_MAX_COINBASE];
	size_t spklen, cblen, cbnwlen;
	bool r;

	printf("Coinbase\n");

	solo_address_to_spk("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4",
		spk, sizeof(spk), &spklen);
	solo_hex2bin(commit,
		"6a24aa21a9ed"
		"0000000000000000000000000000000000000000000000000000000000000000", 38);

	r = solo_build_coinbase(cb, sizeof(cb), &cblen, cbnw, sizeof(cbnw), &cbnwlen,
		900000, 312500000ULL, 0x0102030405060708ULL,
		spk, spklen, commit, sizeof(commit));
	check(r, "builds a segwit coinbase");

	/* the two forms differ by exactly marker+flag (2) and the witness (34) */
	check(cblen == cbnwlen + 36, "witness form is 36 bytes longer");
	check(cb[4] == 0x00 && cb[5] == 0x01, "marker/flag present");
	check(cbnw[4] == 0x01, "txid form starts its vin count right after version");

	/* version(4) marker/flag(2) vin count(1) prevout(36) puts the scriptSig
	 * CompactSize at offset 43; push(03 a0bb0d) + push(08 <extranonce LE>) */
	check(cb[43] == 13, "scriptSig length");
	check(hexeq(cb + 44, 13, "03a0bb0d080807060504030201"),
	      "scriptSig is BIP34 height then extranonce");

	/* witness stack: one 32-byte zero item, then a zero locktime */
	check(hexeq(cb + cblen - 38, 38,
		"0120"                                                              /* count, len */
		"0000000000000000000000000000000000000000000000000000000000000000"  /* reserved */
		"00000000"),                                                        /* locktime */
		"witness reserved value and locktime");

	/* non-segwit template: single output, identical serializations */
	r = solo_build_coinbase(cb, sizeof(cb), &cblen, cbnw, sizeof(cbnw), &cbnwlen,
		1, 5000000000ULL, 0, spk, spklen, NULL, 0);
	check(r && cblen == cbnwlen && !memcmp(cb, cbnw, cblen),
	      "non-segwit coinbase has a single form");

	/* an oversized scriptPubKey must be refused, not silently truncated */
	r = solo_build_coinbase(cb, sizeof(cb), &cblen, cbnw, sizeof(cbnw), &cbnwlen,
		1, 0, 0, spk, SOLO_MAX_SPK + 1, NULL, 0);
	check(!r, "rejects an oversized scriptPubKey");
}

int solo_script_selftest(void)
{
	g_fail = 0;
	printf("--- solo_script offline vectors ---\n");
	test_compactsize();
	test_scriptnum();
	test_address();
	test_merkle();
	test_target();
	test_header();
	test_coinbase();
	printf("--- %s ---\n", g_fail ? "FAILURES" : "all offline vectors passed");
	return g_fail;
}
