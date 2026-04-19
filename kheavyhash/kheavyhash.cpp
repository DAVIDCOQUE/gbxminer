/**
 * kheavyhash.cpp
 *
 * CPU-side kHeavyHash (Kaspa) integration for gbxminer.
 *
 * No DAG, no epoch management, no light cache — the matrix is re-derived
 * from each block header entirely on-device per kernel launch.
 * This file is therefore extremely thin: upload header, iterate nonce
 * window, check results, return.
 *
 * C++ STL headers before miner.h to avoid min/max macro collision.
 */

/* ── C++ STL first ─────────────────────────────────────────────────── */
#include <string>
#include <cstring>

/* ── kHeavyHash CUDA interface ─────────────────────────────────────── */
#include "kheavyhash/kheavyhash.h"
#include "kheavyhash/kheavyhash_cuda.h"

/* ── CUDA runtime ──────────────────────────────────────────────────── */
#include <cuda_runtime.h>

/* ── C standard + gbxminer ─────────────────────────────────────────── */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "miner.h"

/* ------------------------------------------------------------------ */
/*  Nonces per kernel launch                                            */
/*  kHeavyHash is cheap per-nonce; large batches keep the GPU busy.    */
/* ------------------------------------------------------------------ */

#define KHH_NONCES_PER_ITER  (1U << 18)   /* 262 144 */

/* ------------------------------------------------------------------ */
/*  scanhash_kheavyhash                                                 */
/* ------------------------------------------------------------------ */

int scanhash_kheavyhash(int thr_id, struct work *work,
                        uint32_t max_nonce, unsigned long *hashes_done)
{
    const int dev_id = device_map[thr_id];
    cudaSetDevice(dev_id);

    kheavyhash_cuda_init(thr_id);

    /*
     * work->data[0..17] = 72-byte block header (18 × uint32, little-endian).
     * work->target[0..7] = 32-byte difficulty target (little-endian).
     *
     * The GPU kernel compares the first 8 bytes of heavyhash against a
     * 64-bit compact target.  We derive it from work->target[7] (the most
     * significant word used by fulltest()).
     *
     * For Kaspa, the pool supplies the full 256-bit target; we pass the
     * first uint64 (bytes 0..7) as the compact comparison value.
     */
    uint64_t target64;
    memcpy(&target64, &work->target[0], sizeof(uint64_t));

    const uint32_t start  = work->nonces[0];
    const uint32_t window = max_nonce - start;
    const uint32_t launches = (window + KHH_NONCES_PER_ITER - 1)
                               / KHH_NONCES_PER_ITER;

    uint32_t base        = start;
    bool     found       = false;
    uint32_t found_nonce = 0;

    for (uint32_t l = 0; l < launches && !found; l++,
                  base += KHH_NONCES_PER_ITER)
    {
        const uint32_t batch = (base + KHH_NONCES_PER_ITER <= max_nonce)
                               ? KHH_NONCES_PER_ITER
                               : max_nonce - base;

        int hit = kheavyhash_run(thr_id,
                                 work->data,
                                 target64,
                                 base,
                                 batch,
                                 &found_nonce);
        if (hit) {
            found = true;
        }
    }

    *hashes_done = base - start;

    if (found) {
        work->nonces[0]    = found_nonce;
        work->valid_nonces = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  free_kheavyhash                                                     */
/* ------------------------------------------------------------------ */

void free_kheavyhash(int thr_id)
{
    cudaSetDevice(device_map[thr_id]);
    kheavyhash_cuda_free(thr_id);
}
