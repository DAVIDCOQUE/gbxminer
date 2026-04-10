#ifndef AUTOLYKOS2_H
#define AUTOLYKOS2_H
/**
 * autolykos2.h
 *
 * CPU-side interface for the Autolykos v2 (Ergo) proof-of-work algorithm.
 *
 * Autolykos v2 protocol summary
 * ------------------------------
 * Autolykos v2 is Ergo's ASIC-resistant PoW, defined in EIP-0037.
 * It is a k-sum puzzle over a large prehashed table:
 *
 *   1. Build a table of N = 2^26 BLAKE2b-256 hashes ("prehash table"):
 *        hash[j] = blake2b256(j || M || pk || msg || w)    mod Q
 *      where Q is the secp256k1 curve order.
 *
 *   2. For each nonce, compute:
 *        d = blake2b256(msg || nonce || hash[i0] || ... || hash[i_{k-1}])
 *      where i_0..i_{k-1} are 32 indices derived from the nonce.
 *
 *   3. Solution if d < bound (supplied by pool).
 *
 * Unlike ETCHash/KaPow, Autolykos2 does NOT use an epoch-based DAG.
 * The prehash table is rebuilt only when the block message or public key
 * changes (i.e. on each new block), making it effectively a per-block
 * table.  This is more memory-efficient (~2 GB) but requires a fast rebuild
 * path.
 *
 * The table fits in a device memory allocation of:
 *   N_LEN * NUM_SIZE_8 = 2^26 * 32 = 2 GiB
 *
 * Stratum protocol: "ergo" stratum (getblocktemplate / Stratum v1 variant).
 * Work packages include: bound (256-bit), message (256-bit), pk (33-byte
 * compressed secp256k1 public key).  Block height is carried in work->height.
 *
 * Cannibalised from:
 *   Autolykosminer by mhssamadani (GPL-3.0)
 *   https://github.com/mhssamadani/Autolykosminer
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * scanhash_autolykos2 - GPU hash loop for Autolykos v2 (Ergo).
 *
 * @param thr_id      GPU thread index.
 * @param work        Current work package.
 *                    work->data[0..7]   = 32-byte message hash (big-endian)
 *                    work->data[8..15]  = 32-byte bound (big-endian)
 *                    work->target[0..7] = 32-byte difficulty target
 *                    work->height       = block height (used to detect
 *                                        message/table change)
 * @param max_nonce   Upper nonce bound for this scan window.
 * @param hashes_done Set to nonces tested on return.
 *
 * Returns 1 on solution found (nonce in work->nonces[0]),
 *         0 on exhaustion, -1 on error.
 */
int scanhash_autolykos2(int thr_id, struct work *work,
                        uint32_t max_nonce, unsigned long *hashes_done);

/** Release device memory held for this GPU thread. */
void free_autolykos2(int thr_id);

#ifdef __cplusplus
}
#endif

#endif /* AUTOLYKOS2_H */
