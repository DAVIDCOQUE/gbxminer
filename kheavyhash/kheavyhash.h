#ifndef KHEAVYHASH_H
#define KHEAVYHASH_H
/**
 * kheavyhash.h
 *
 * CPU-side interface for the kHeavyHash (Kaspa) proof-of-work algorithm.
 *
 * kHeavyHash protocol summary
 * ---------------------------
 * kHeavyHash is Kaspa's proof-of-work, designed for GPU efficiency.
 * Unlike DAG-based algorithms (ETCHash, KawPow) there is no epoch — the
 * matrix is re-derived from each block header in-kernel with negligible
 * cost.  This makes it ideal for short mining windows (donation bursts).
 *
 * Per-block computation:
 *   1. pre_hash  = keccak256(header[0:72])              (raw Keccak, not SHA3)
 *   2. M         = 64×64 nibble matrix from pre_hash    (xoshiro256** PRNG)
 *   3. product_i = Σ_j M[i][j] · nibble_j(pre_hash)   (mod 2^17)
 *   4. heavyhash = keccak256(pre_hash XOR product)
 *   5. Solution  iff heavyhash < target
 *
 * Block header layout (72 bytes):
 *   [0..31]  previous block hash
 *   [32..39] nonce (8 bytes, little-endian uint64)
 *   [40..71] merkle root / timestamp / bits
 *
 * Stratum: getblocktemplate or Kaspa stratum bridge.
 * Work packages include: header (72 bytes), target (32 bytes).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * scanhash_kheavyhash - GPU hash loop for kHeavyHash.
 *
 * @param thr_id      GPU thread index.
 * @param work        Current work package.
 *                    work->data[0..17] = 72-byte block header (18 × uint32)
 *                    work->target      = 32-byte difficulty target
 * @param max_nonce   Upper nonce bound for this scan window.
 * @param hashes_done Set to nonces tested on return.
 *
 * Returns 1 on solution found (nonce in work->nonces[0]),
 *         0 on exhaustion, -1 on error.
 */
int scanhash_kheavyhash(int thr_id, struct work *work,
                        uint32_t max_nonce, unsigned long *hashes_done);

/** Release device memory held for this GPU thread. */
void free_kheavyhash(int thr_id);

#ifdef __cplusplus
}
#endif

#endif /* KHEAVYHASH_H */
