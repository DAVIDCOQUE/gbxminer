#ifndef KAWPOW_H
#define KAWPOW_H
/**
 * kawpow.h
 *
 * CPU-side interface for the KawPow (Ravencoin ProgPoW) mining algorithm.
 *
 * KawPow protocol summary
 * ----------------------
 * KawPow is Ravencoin's ASIC-resistant PoW, derived from ProgPoW.
 * Key parameters that differ from ETH ProgPoW:
 *
 *   EPOCH_LENGTH  = 7 500 blocks   (controls DAG epoch)
 *   PERIOD        = 3 blocks       (controls program regeneration)
 *
 * The DAG structure is identical to Ethash / ETCHash (128-byte nodes,
 * Keccak-512 light cache, FNV-based DAG expansion).  The search kernel
 * is re-compiled each time the period changes using NVRTC.
 *
 * Stratum protocol: "kawpow" variant of ethproxy / Nicehash-eth.
 * Work packages include block height (for epoch/period computation).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * scanhash_kawpow - GPU hash loop for KawPow.
 *
 * @param thr_id      GPU thread index.
 * @param work        Current work package (work->height = block height).
 * @param max_nonce   Upper nonce bound.
 * @param hashes_done Set to nonces tested.
 *
 * Returns 1 on solution found, 0 on exhaustion, -1 on error.
 */
int scanhash_kawpow(int thr_id, struct work* work,
                   uint32_t max_nonce, unsigned long* hashes_done);

/** Release all device memory and compiled kernel modules for this thread. */
void free_kawpow(int thr_id);

#ifdef __cplusplus
}
#endif

#endif /* KAWPOW_H */
