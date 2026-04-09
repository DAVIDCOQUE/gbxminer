#ifndef ALGOS_H
#define ALGOS_H

#include <string.h>
#include "compat.h"

/**
 * sha_algos - enumeration of all GPU-minable algorithms supported by gbxminer.
 *
 * Removed families (ASIC-dominated or dead chains):
 *   - X-series  (X11/X13/X17 and derivatives: hsr, sonoa, zr5)
 *   - Blake-ASIC (decred, pentablake, vanilla/blake256)
 *   - CryptoNight family (cryptonight, cryptolight, monero, graft, stellite,
 *                         wildkeccak) — CPU/ASIC, not GPU-competitive
 *   - Scrypt / Scrypt-Jane — ASIC-dominated; NeoScrypt is retained as
 *                            GoByte's primary PoW algorithm
 *
 * Added:
 *   - ALGO_ETCHASH — Ethereum Classic PoW (ECIP-1099, epoch = 60 000 blocks)
 *   - ALGO_KAPOW   — Ravencoin ProgPoW variant (epoch = 7 500 blocks,
 *                    program period = 3 blocks)
 */
enum sha_algos {
	ALGO_ALLIUM,
	ALGO_BMW,
	ALGO_DMD_GR,
	ALGO_EQUIHASH,
	ALGO_ETCHASH,		/* ETCHash (ECIP-1099) — Ethereum Classic     */
	ALGO_FUGUE256,		/* Fugue256                                    */
	ALGO_GROESTL,
	ALGO_HEAVY,		/* Heavycoin hash                              */
	ALGO_KECCAK,
	ALGO_KECCAKC,		/* Keccak-256 with pool factor 256 (CreativeCoin) */
	ALGO_JACKPOT,
	ALGO_JHA,
	ALGO_KAPOW,		/* KaPow (Ravencoin ProgPoW)                   */
	ALGO_LBRY,
	ALGO_LUFFA,
	ALGO_LYRA2,
	ALGO_LYRA2v2,
	ALGO_LYRA2v3,
	ALGO_LYRA2Z,
	ALGO_MJOLLNIR,		/* Hefty hash                                  */
	ALGO_MYR_GR,
	ALGO_NEOSCRYPT,		/* GoByte primary PoW — MUST NOT BE REMOVED    */
	ALGO_NIST5,
	ALGO_QUARK,
	ALGO_QUBIT,
	ALGO_SHA256D,
	ALGO_SHA256T,
	ALGO_SKEIN,
	ALGO_SKEIN2,
	ALGO_WHIRLCOIN,
	ALGO_WHIRLPOOL,
	ALGO_AUTO,
	ALGO_COUNT
};

extern volatile enum sha_algos opt_algo;

static const char *algo_names[] = {
	"allium",
	"bmw",
	"dmd-gr",
	"equihash",
	"etchash",
	"fugue256",
	"groestl",
	"heavy",
	"keccak",
	"keccakc",
	"jackpot",
	"jha",
	"kapow",
	"lbry",
	"luffa",
	"lyra2",
	"lyra2v2",
	"lyra2v3",
	"lyra2z",
	"mjollnir",
	"myr-gr",
	"neoscrypt",
	"nist5",
	"quark",
	"qubit",
	"sha256d",
	"sha256t",
	"skein",
	"skein2",
	"whirlcoin",
	"whirlpool",
	"auto",		/* reserved for multi-algo */
	""
};

/* string → enum; returns -1 on unknown algo */
static inline int algo_to_int(char* arg)
{
	int i;

	for (i = 0; i < ALGO_COUNT; i++) {
		if (algo_names[i] && !strcasecmp(arg, algo_names[i]))
			return i;
	}

	if (i == ALGO_COUNT) {
		/* Common aliases */
		if      (!strcasecmp("all",        arg)) i = ALGO_AUTO;
		else if (!strcasecmp("diamond",    arg)) i = ALGO_DMD_GR;
		else if (!strcasecmp("equi",       arg)) i = ALGO_EQUIHASH;
		else if (!strcasecmp("etc",        arg)) i = ALGO_ETCHASH;
		else if (!strcasecmp("doom",       arg)) i = ALGO_LUFFA;
		else if (!strcasecmp("lyra2re",    arg)) i = ALGO_LYRA2;
		else if (!strcasecmp("lyra2rev2",  arg)) i = ALGO_LYRA2v2;
		else if (!strcasecmp("lyra2rev3",  arg)) i = ALGO_LYRA2v3;
		else if (!strcasecmp("bitcoin",    arg)) i = ALGO_SHA256D;
		else if (!strcasecmp("sha256",     arg)) i = ALGO_SHA256D;
		else if (!strcasecmp("whirl",      arg)) i = ALGO_WHIRLPOOL;
		else if (!strcasecmp("ravencoin",  arg)) i = ALGO_KAPOW;
		else if (!strcasecmp("rvn",        arg)) i = ALGO_KAPOW;
		else                                     i = -1;
	}

	return i;
}

#endif /* ALGOS_H */
