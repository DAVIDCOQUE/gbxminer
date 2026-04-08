#ifndef ALGOS_H
#define ALGOS_H

#include <string.h>
#include "compat.h"

enum sha_algos {
	ALGO_ALLIUM,
	ALGO_BMW,
	ALGO_CRYPTOLIGHT,
	ALGO_CRYPTONIGHT,
	ALGO_DECRED,
	ALGO_DMD_GR,
	ALGO_EQUIHASH,
	ALGO_FUGUE256,		/* Fugue256 */
	ALGO_GROESTL,
	ALGO_HEAVY,		/* Heavycoin hash */
	ALGO_HSR,
	ALGO_KECCAK,
	ALGO_KECCAKC,		/* refreshed Keccak with pool factor 256 */
	ALGO_JACKPOT,
	ALGO_JHA,
	ALGO_LBRY,
	ALGO_LUFFA,
	ALGO_LYRA2,
	ALGO_LYRA2v2,
	ALGO_LYRA2v3,
	ALGO_LYRA2Z,
	ALGO_MJOLLNIR,		/* Hefty hash */
	ALGO_MYR_GR,
	ALGO_NEOSCRYPT,
	ALGO_NIST5,
	ALGO_PENTABLAKE,
	ALGO_QUARK,
	ALGO_QUBIT,
	ALGO_SCRYPT,
	ALGO_SCRYPT_JANE,
	ALGO_SHA256D,
	ALGO_SHA256T,
	ALGO_SKEIN,
	ALGO_SKEIN2,
	ALGO_SONOA,
	ALGO_WHIRLCOIN,
	ALGO_WHIRLPOOL,
	ALGO_WILDKECCAK,
	ALGO_ZR5,
	ALGO_MONERO,
	ALGO_GRAFT,
	ALGO_STELLITE,
	ALGO_AUTO,
	ALGO_COUNT
};

extern volatile enum sha_algos opt_algo;

static const char *algo_names[] = {
	"allium",
	"bmw",
	"cryptolight",
	"cryptonight",
	"decred",
	"dmd-gr",
	"equihash",
	"fugue256",
	"groestl",
	"heavy",
	"hsr",
	"keccak",
	"keccakc",
	"jackpot",
	"jha",
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
	"penta",
	"quark",
	"qubit",
	"scrypt",
	"scrypt-jane",
	"sha256d",
	"sha256t",
	"skein",
	"skein2",
	"sonoa",
	"whirlcoin",
	"whirlpool",
	"wildkeccak",
	"zr5",
	"monero",
	"graft",
	"stellite",
	"auto", /* reserved for multi algo */
	""
};

// string to int/enum
static inline int algo_to_int(char* arg)
{
	int i;

	for (i = 0; i < ALGO_COUNT; i++) {
		if (algo_names[i] && !strcasecmp(arg, algo_names[i])) {
			return i;
		}
	}

	if (i == ALGO_COUNT) {
		// some aliases...
		if (!strcasecmp("all", arg))
			i = ALGO_AUTO;
		else if (!strcasecmp("cryptonight-light", arg))
			i = ALGO_CRYPTOLIGHT;
		else if (!strcasecmp("cryptonight-lite", arg))
			i = ALGO_CRYPTOLIGHT;
		else if (!strcasecmp("diamond", arg))
			i = ALGO_DMD_GR;
		else if (!strcasecmp("equi", arg))
			i = ALGO_EQUIHASH;
		else if (!strcasecmp("doom", arg))
			i = ALGO_LUFFA;
		else if (!strcasecmp("hshare", arg))
			i = ALGO_HSR;
		else if (!strcasecmp("lyra2re", arg))
			i = ALGO_LYRA2;
		else if (!strcasecmp("lyra2rev2", arg))
			i = ALGO_LYRA2v2;
		else if (!strcasecmp("lyra2rev3", arg))
			i = ALGO_LYRA2v3;
		else if (!strcasecmp("bitcoin", arg))
			i = ALGO_SHA256D;
		else if (!strcasecmp("sha256", arg))
			i = ALGO_SHA256D;
		else if (!strcasecmp("whirl", arg))
			i = ALGO_WHIRLPOOL;
		else if (!strcasecmp("ziftr", arg))
			i = ALGO_ZR5;
		else
			i = -1;
	}

	return i;
}

static inline int get_cryptonight_algo(int fork)
{
	int algo = ALGO_COUNT;

	switch (fork) {
		case 8:
			algo = ALGO_GRAFT;
			break;

		case 7:
			algo = ALGO_MONERO;
			break;

		case 3:
			algo = ALGO_STELLITE;
			break;

		default:
			algo = ALGO_CRYPTONIGHT;
			break;
	}

	return algo;
}

#endif
