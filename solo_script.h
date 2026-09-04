/**
 * Bitcoin consensus serialization primitives for solo mining.
 *
 * Deliberately free of curl/jansson/miner.h so it can be compiled and unit
 * tested on its own; sha256d() is the only external symbol it needs.
 *
 * Byte order convention used throughout: "internal" means the byte order that
 * appears in the serialized block (hashes reversed with respect to the hex
 * shown by Bitcoin Core RPC).
 *
 * Copyright 2026-2027 d0wn3d
 */

#ifndef SOLO_SCRIPT_H
#define SOLO_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* provided by sph/sha2.c */
void sha256d(unsigned char *hash, const unsigned char *data, int len);

#define SOLO_MAX_SPK        64      /* largest standard scriptPubKey */
#define SOLO_MAX_COINBASE   1024    /* scriptSig is capped at 100 bytes */
#define SOLO_SCRIPTSIG_MAX  100
#define SOLO_SCRIPTSIG_MIN  2

/* --- hex ---------------------------------------------------------------- */

bool solo_hex2bin(uint8_t *out, const char *hex, size_t len);
/* decodes RPC display hex into internal (reversed) byte order */
bool solo_hex2bin_rev(uint8_t *out, const char *hex, size_t len);
void solo_bin2hex(char *out, const uint8_t *in, size_t len);
/* encodes internal byte order back into RPC display hex */
void solo_bin2hex_rev(char *out, const uint8_t *in, size_t len);

/* --- serialization ------------------------------------------------------ */

/* CompactSize; returns bytes written (1, 3, 5 or 9) */
size_t solo_compactsize(uint8_t *out, uint64_t n);
/* minimally encoded CScriptNum push, as required by BIP34 for the height.
 * Returns bytes written (push opcode included). */
size_t solo_push_scriptnum(uint8_t *out, uint32_t n);
/* little-endian integers */
void solo_put_u32le(uint8_t *out, uint32_t v);
void solo_put_u64le(uint8_t *out, uint64_t v);

/* --- addresses ---------------------------------------------------------- */

/* Decodes a bech32/bech32m or base58check address into its scriptPubKey.
 * Accepts mainnet, testnet/signet and regtest prefixes. */
bool solo_address_to_spk(const char *addr, uint8_t *spk, size_t spk_max, size_t *spk_len);

/* --- merkle ------------------------------------------------------------- */

/* leaves is count*32 bytes in internal order; duplicates the last hash on odd
 * levels, as Bitcoin requires. */
bool solo_merkle_root(uint8_t *out32, const uint8_t *leaves, size_t count);

/* --- target ------------------------------------------------------------- */

/* target[7] is the most significant word, matching fulltest() */
bool solo_target_from_bits(uint32_t *target, uint32_t bits);
bool solo_target_from_hex(uint32_t *target, const char *hex64);

/* --- header ------------------------------------------------------------- */

/* data[0..19] holds the header in the big-endian-word form the SHA256d CUDA
 * scanner expects; this writes the 80 raw bytes that get hashed and submitted. */
void solo_header_serialize(uint8_t *out80, const uint32_t *data);
/* inverse, used to load a known header into work->data */
void solo_header_to_words(uint32_t *data, const uint8_t *hdr80);

/* --- coinbase ----------------------------------------------------------- */

/* Builds both serializations of the coinbase transaction:
 *   out    - with marker/flag and the witness stack; goes into the block
 *   out_nw - without them; its sha256d is the TXID used by the merkle tree
 * Pass wit_commit_len = 0 to build a non-segwit coinbase (both forms equal).
 */
bool solo_build_coinbase(uint8_t *out, size_t out_max, size_t *out_len,
	uint8_t *out_nw, size_t out_nw_max, size_t *out_nw_len,
	uint32_t height, uint64_t value, uint64_t extranonce,
	const uint8_t *spk, size_t spk_len,
	const uint8_t *wit_commit, size_t wit_commit_len);

/* --- tests -------------------------------------------------------------- */

/* Offline vector tests; returns the number of failures. */
int solo_script_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* SOLO_SCRIPT_H */
