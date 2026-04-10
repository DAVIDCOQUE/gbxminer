/**
 * autolykos2.cpp
 *
 * CPU-side Autolykos v2 (Ergo) integration for gbxminer.
 * All CUDA kernel launches delegated to autolykos2_cuda.cu (nvcc).
 * This file is compiled by g++ — no <<<>>> syntax.
 *
 * Include order: C++ STL before miner.h to avoid min/max macro collision.
 *
 * Cannibalised from Autolykosminer by mhssamadani (GPL-3.0).
 */

/* ── C++ STL first ─────────────────────────────────────────────────── */
#include <string>
#include <cstring>

/* ── Autolykos2 CUDA launch wrappers (plain-C, no <<<>>>) ─────────── */
#include "autolykos2/autolykos2_cuda.h"
#include "autolykos2/autolykos2.h"

/* ── CUDA runtime ──────────────────────────────────────────────────── */
#include <cuda_runtime.h>

/* ── C standard + gbxminer ─────────────────────────────────────────── */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "miner.h"

/* ------------------------------------------------------------------ */
/*  Algorithm parameters                                               */
/* ------------------------------------------------------------------ */

#define AL2_N_LEN           0x4000000U
#define AL2_NUM_SIZE_8      32U
#define AL2_TABLE_BYTES     ((uint64_t)AL2_N_LEN * AL2_NUM_SIZE_8)
#define AL2_INVALID_BYTES   ((uint64_t)(AL2_N_LEN + 1) * 4U)
#define AL2_NONCES_PER_ITER (1U << 18)
#define AL2_BLOCK_DIM       64U
#define AL2_GRID_DIM        (AL2_NONCES_PER_ITER / AL2_BLOCK_DIM)
#define AL2_MAX_SOLS        16U

/* ------------------------------------------------------------------ */
/*  Per-GPU state                                                      */
/* ------------------------------------------------------------------ */

struct Al2GpuState {
    uint32_t *d_hashes;
    uint32_t *d_invalid;
    uint32_t *d_aux;
    uint32_t *d_data;
    uint32_t *d_results;
    uint32_t *d_valid;
    uint32_t *d_count;
    uint32_t *d_bhashes;
    uint8_t   last_msg[AL2_NUM_SIZE_8];
    bool      table_valid;
    bool      allocated;
};

static Al2GpuState s_al2[MAX_GPUS];
static bool        s_al2_init = false;

static void al2_init_states(void)
{
    if (s_al2_init) return;
    for (int i = 0; i < MAX_GPUS; i++)
        memset(&s_al2[i], 0, sizeof(Al2GpuState));
    s_al2_init = true;
}

/* ------------------------------------------------------------------ */
/*  Allocation                                                         */
/* ------------------------------------------------------------------ */

static bool al2_alloc(int thr_id)
{
    Al2GpuState *g      = &s_al2[thr_id];
    if (g->allocated) return true;
    const int    dev_id = device_map[thr_id];
    cudaSetDevice(dev_id);

    applog(LOG_INFO,
           "GPU #%d: Autolykos2 allocating ~%.1f GB device RAM",
           dev_id,
           (AL2_TABLE_BYTES + AL2_INVALID_BYTES * 2.0) / 1073741824.0);

    bool ok =
        cudaMalloc((void**)&g->d_hashes,  AL2_TABLE_BYTES)       == cudaSuccess &&
        cudaMalloc((void**)&g->d_invalid, AL2_INVALID_BYTES)      == cudaSuccess &&
        cudaMalloc((void**)&g->d_aux,     AL2_INVALID_BYTES)      == cudaSuccess &&
        cudaMalloc((void**)&g->d_data,    4096UL)                  == cudaSuccess &&
        cudaMalloc((void**)&g->d_results, AL2_MAX_SOLS * AL2_NUM_SIZE_8 * 4UL)
                                                                   == cudaSuccess &&
        cudaMalloc((void**)&g->d_valid,   AL2_MAX_SOLS * 4UL)     == cudaSuccess &&
        cudaMalloc((void**)&g->d_count,   4UL)                    == cudaSuccess &&
        cudaMalloc((void**)&g->d_bhashes,
                   (uint64_t)AL2_NONCES_PER_ITER * AL2_NUM_SIZE_8) == cudaSuccess;

    if (!ok) {
        applog(LOG_ERR,
               "GPU #%d: Autolykos2 cudaMalloc failed — need ~2.2 GB free VRAM",
               dev_id);
        free_autolykos2(thr_id);
        return false;
    }
    g->allocated = true;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Data block layout (pk 33B || msg 32B || bound 32B || zeroes)       */
/* ------------------------------------------------------------------ */

static void al2_fill_data(uint8_t h_data[512],
                          const uint32_t *msg,
                          const uint32_t *bound,
                          const uint32_t *pk)
{
    memset(h_data, 0, 512);
    if (pk)    memcpy(h_data,      pk,    33);
    if (msg)   memcpy(h_data + 33, msg,   AL2_NUM_SIZE_8);
    if (bound) memcpy(h_data + 65, bound, AL2_NUM_SIZE_8);
}

/* ------------------------------------------------------------------ */
/*  Prehash table build                                                */
/* ------------------------------------------------------------------ */

static bool al2_build_prehash(int thr_id,
                              const uint32_t *msg,
                              const uint32_t *bound,
                              const uint32_t *pk)
{
    Al2GpuState *g      = &s_al2[thr_id];
    const int    dev_id = device_map[thr_id];
    cudaSetDevice(dev_id);

    applog(LOG_INFO,
           "GPU #%d: Autolykos2 building prehash table (~2 GiB)…", dev_id);

    uint8_t h_data[512];
    al2_fill_data(h_data, msg, bound, pk);
    if (cudaMemcpy(g->d_data, h_data, sizeof(h_data),
                   cudaMemcpyHostToDevice) != cudaSuccess)
        return false;

    cudaMemset(g->d_invalid, 0, AL2_INVALID_BYTES);

    const uint32_t grid  = AL2_N_LEN / AL2_BLOCK_DIM;
    const uint32_t block = AL2_BLOCK_DIM;

    al2_launch_init_prehash(g->d_data, g->d_hashes, g->d_invalid, grid, block);
    if (cudaGetLastError() != cudaSuccess) {
        applog(LOG_ERR, "GPU #%d: Autolykos2 InitPrehash kernel failed", dev_id);
        return false;
    }

    /* Rehash out-of-range entries until none remain */
    for (int iter = 0; iter < 32; iter++) {
        uint32_t inv_count = 0;
        al2_reduce_sum(g->d_invalid, g->d_aux, AL2_N_LEN, &inv_count);
        if (inv_count == 0) break;

        uint32_t *d_compact = nullptr;
        if (cudaMalloc((void**)&d_compact,
                       (uint64_t)(inv_count + 1) * 4U) != cudaSuccess) {
            applog(LOG_ERR,
                   "GPU #%d: Autolykos2 compact alloc failed", dev_id);
            return false;
        }

        /* outlen scratch — reuse d_aux (already a uint32_t device buffer) */
        al2_launch_compactify(g->d_invalid, AL2_N_LEN,
                              d_compact, g->d_aux, block);
        cudaDeviceSynchronize();

        al2_launch_update_prehash(g->d_hashes, d_compact, inv_count, block);
        cudaDeviceSynchronize();

        cudaFree(d_compact);
    }

    al2_launch_final_prehash(g->d_hashes, grid, block);
    cudaDeviceSynchronize();

    if (cudaGetLastError() != cudaSuccess) {
        applog(LOG_ERR, "GPU #%d: Autolykos2 FinalPrehash failed", dev_id);
        return false;
    }

    memcpy(g->last_msg, msg, AL2_NUM_SIZE_8);
    g->table_valid = true;
    applog(LOG_INFO, "GPU #%d: Autolykos2 prehash table ready", dev_id);
    return true;
}

/* ------------------------------------------------------------------ */
/*  scanhash_autolykos2                                                */
/* ------------------------------------------------------------------ */

int scanhash_autolykos2(int thr_id, struct work *work,
                        uint32_t max_nonce, unsigned long *hashes_done)
{
    al2_init_states();
    const int dev_id = device_map[thr_id];

    if (!al2_alloc(thr_id)) return -1;

    Al2GpuState *g = &s_al2[thr_id];
    cudaSetDevice(dev_id);

    /*
     * work->data layout for Autolykos2 (80-byte header):
     *   [0..7]   32-byte message hash
     *   [8..15]  32-byte difficulty bound
     *   [16..24] 33-byte pk (zeros = public pool mode)
     */
    const uint32_t *msg   = work->data;
    const uint32_t *bound = work->data + 8;
    const uint32_t *pk    = work->data + 16;

    if (!g->table_valid ||
        memcmp(g->last_msg, msg, AL2_NUM_SIZE_8) != 0)
    {
        if (!al2_build_prehash(thr_id, msg, bound, pk))
            return -1;
    }

    /* Refresh data block each scan (bound may change on pool retarget) */
    uint8_t h_data[512];
    al2_fill_data(h_data, msg, bound, pk);
    cudaMemcpy(g->d_data, h_data, sizeof(h_data), cudaMemcpyHostToDevice);

    const uint32_t start    = work->nonces[0];
    const uint32_t nonces   = max_nonce - start;
    const uint32_t launches = (nonces + AL2_NONCES_PER_ITER - 1)
                               / AL2_NONCES_PER_ITER;
    uint64_t base   = (uint64_t)start;
    bool     found  = false;
    uint32_t fnonce = 0;

    for (uint32_t l = 0; l < launches && !found; l++,
                  base += AL2_NONCES_PER_ITER)
    {
        cudaMemset(g->d_count, 0, 4);
        cudaMemset(g->d_valid, 0, AL2_MAX_SOLS * 4U);

        al2_launch_blake_hash(g->d_data, base, g->d_bhashes,
                              AL2_GRID_DIM, AL2_BLOCK_DIM);
        cudaGetLastError();

        /*
         * Bound is at byte offset 65 in h_data = word offset 16 (64 B).
         * Pass d_data + 16 as the bound pointer into BlockMining.
         */
        al2_launch_block_mining(
            g->d_data + 16, g->d_data, base,
            g->d_hashes,
            g->d_results, g->d_valid, g->d_count, g->d_bhashes,
            AL2_GRID_DIM, AL2_BLOCK_DIM);
        cudaGetLastError();
        cudaDeviceSynchronize();

        uint32_t h_count = 0;
        cudaMemcpy(&h_count, g->d_count, 4, cudaMemcpyDeviceToHost);

        if (h_count > 0) {
            uint32_t h_valid[AL2_MAX_SOLS];
            cudaMemcpy(h_valid, g->d_valid,
                       AL2_MAX_SOLS * 4U, cudaMemcpyDeviceToHost);
            fnonce = (uint32_t)(base + h_valid[0]);
            found  = true;
        }
    }

    *hashes_done = (uint32_t)(base - (uint64_t)start);

    if (found) {
        work->nonces[0]    = fnonce;
        work->valid_nonces = 1;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  free_autolykos2                                                    */
/* ------------------------------------------------------------------ */

void free_autolykos2(int thr_id)
{
    Al2GpuState *g      = &s_al2[thr_id];
    cudaSetDevice(device_map[thr_id]);

#define AL2_FREE(p) do { if (p) { cudaFree(p); (p) = nullptr; } } while (0)
    AL2_FREE(g->d_hashes);  AL2_FREE(g->d_invalid); AL2_FREE(g->d_aux);
    AL2_FREE(g->d_data);    AL2_FREE(g->d_results); AL2_FREE(g->d_valid);
    AL2_FREE(g->d_count);   AL2_FREE(g->d_bhashes);
#undef AL2_FREE
    g->table_valid = false;
    g->allocated   = false;
}
