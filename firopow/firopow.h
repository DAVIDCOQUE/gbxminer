#ifndef FIROPOW_H
#define FIROPOW_H
/**
 * firopow.h
 *
 * CPU-side interface for the FiroPow (Firo ProgPoW) mining algorithm.
 *
 * FiroPow protocol summary
 * ------------------------
 * FiroPow is Firo's (formerly Zcoin) ASIC-resistant PoW, replacing MTP
 * in 2021.  It is ProgPoW with two consensus-critical parameter changes:
 *
 *   EPOCH_LENGTH  = 1 300 blocks   (vs 7 500 for KawPow)
 *   PERIOD        = 13 blocks      (vs 3 for KawPow)
 *
 * All other ProgPoW constants (LANES=16, REGS=32, DAG_LOADS=4,
 * CACHE_BYTES=16384, CNT_DAG=64, CNT_CACHE=11, CNT_MATH=18) are
 * identical to KawPow.  The DAG structure (Keccak-512 light cache,
 * FNV-based expansion, 128-byte nodes) is also identical.
 *
 * The search kernel is re-compiled each time the period changes using
 * NVRTC (same mechanism as KawPow).  Requires --with-nvrtc at configure
 * time; builds to a clean no-op stub without it.
 *
 * Stratum protocol: ethproxy / Nicehash-eth variant, same as KawPow.
 * Work packages include block height (for epoch/period computation).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * scanhash_firopow - GPU hash loop for FiroPow.
 *
 * @param thr_id      GPU thread index.
 * @param work        Current work package (work->height = block height).
 * @param max_nonce   Upper nonce bound.
 * @param hashes_done Set to nonces tested.
 *
 * Returns 1 on solution found, 0 on exhaustion, -1 on error.
 */
int scanhash_firopow(int thr_id, struct work* work,
                     uint32_t max_nonce, unsigned long* hashes_done);

/** Release all device memory and compiled kernel modules for this thread. */
void free_firopow(int thr_id);

#ifdef __cplusplus
}
#endif

#endif /* FIROPOW_H */
