/**
 * Bitcoin Core solo mining (getblocktemplate / submitblock)
 *
 * Separate path from the legacy getwork and stratum code: builds the coinbase,
 * the merkle root and the full block locally, and feeds the existing SHA256d
 * CUDA scanner through the unmodified struct work.
 *
 * Copyright 2026-2027 d0wn3d
 */

#ifndef BITCOIN_SOLO_H
#define BITCOIN_SOLO_H

#include <stdbool.h>
#include <stdint.h>

struct work;

#ifdef __cplusplus
extern "C" {
#endif

/* enabled by --solo */
extern bool  opt_solo;
/* payout address, decoded locally (bech32/bech32m/base58check) */
extern char *opt_solo_address;
/* raw scriptPubKey hex, overrides opt_solo_address */
extern char *opt_solo_scriptpubkey;
/* path to the Bitcoin Core .cookie file used for RPC basic auth */
extern char *opt_solo_cookie;
/* seconds before a cached block template is refetched */
extern int   opt_solo_refresh;
/* testing only: mine at this difficulty instead of the network target */
extern double opt_solo_test_diff;

/* Resolve the payout scriptPubKey and the RPC credentials. Called once, after
 * the pool array is initialised, because cookie auth is injected into it. */
bool solo_init(void);
void solo_shutdown(void);

/* Replaces get_upstream_work()/submit_upstream_work() when opt_solo is set. */
bool solo_get_work(CURL *curl, struct work *work);
bool solo_submit_block(CURL *curl, struct work *work);

/* Offline vector tests; returns the number of failures. */
int solo_selftest(void);
/* Tests that need a live Bitcoin Core; returns the number of failures. */
int solo_selftest_live(CURL *curl);

#ifdef __cplusplus
}
#endif

#endif /* BITCOIN_SOLO_H */
