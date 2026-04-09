#ifndef ETCHASH_H
#define ETCHASH_H
/**
 * etchash.h
 *
 * CPU-side interface for the ETCHash (ECIP-1099) mining algorithm.
 *
 * ETCHash protocol summary
 * ------------------------
 * ETCHash is ETC's ASIC-resistant PoW.  It is structurally identical to
 * Ethash with one consensus-critical change: the epoch length is 60 000
 * blocks (Ethash uses 30 000).  This doubles the epoch duration and halves
 * the rate at which GPU VRAM requirements grow, keeping the algorithm
 * accessible to consumer-grade GPUs for longer.
 *
 * DAG size formula (unchanged from Ethash):
 *   initial_size = 1_073_741_824  (1 GiB)
 *   grows by     ~   8_388_608   (8 MiB) per epoch
 *
 * Epoch number = block_height / ETCHASH_EPOCH_LENGTH
 *
 * Stratum protocol: eth_getWork / ethproxy / Nicehash-eth style.
 * The work package carries a 32-byte header hash (seed for Keccak) and a
 * 32-byte boundary (target).  Epoch/block height are inferred from the
 * seed hash or supplied as extradata.
 *
 * Thread model
 * ------------
 * Each GPU thread calls scanhash_etchash() which:
 *   1. Computes the epoch from work->height.
 *   2. Lazily generates / reuses the DAG for that epoch.
 *   3. Launches the CUDA search kernel over [start_nonce, max_nonce].
 *   4. Returns the number of solutions found (0 or 1 in practice).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * scanhash_etchash - GPU hash loop for ETCHash.
 *
 * @param thr_id      GPU thread index (0..active_gpus-1).
 * @param work        Pointer to current work package.  work->data[0..7]
 *                    carries the 32-byte header hash; work->height carries
 *                    the block number used to derive the DAG epoch.
 * @param max_nonce   Upper nonce bound for this scan window.
 * @param hashes_done Set to the number of nonces tested on return.
 *
 * Returns 1 if a solution was found (nonce written to work->nonces[0]),
 *         0 if the window was exhausted without a solution.
 */
int scanhash_etchash(int thr_id, struct work* work,
                     uint32_t max_nonce, unsigned long* hashes_done);

/** Release device memory held by the ETCHash DAG for this thread. */
void free_etchash(int thr_id);

#ifdef __cplusplus
}
#endif

#endif /* ETCHASH_H */
