/**
 * etchash.cpp
 *
 * CPU-side ETCHash (ECIP-1099) integration for gbxminer.
 *
 * Responsibilities
 * ----------------
 *  1. Compute the Keccak-based light cache (seed → cache) on the host.
 *  2. Manage per-GPU device allocations: light cache and DAG buffers.
 *  3. Detect epoch changes and rebuild the DAG accordingly.
 *  4. Provide scanhash_etchash() conforming to the standard gbxminer
 *     scanhash interface used by the mining loop in gbxminer.cpp.
 *
 * ETCHash vs Ethash epoch arithmetic
 * ------------------------------------
 * ETCHash epoch = block_height / 60000   (ECIP-1099, activated at block 11700000)
 * Ethash  epoch = block_height / 30000
 *
 * The DAG-size and light-cache-size growth curves are identical to Ethash;
 * only the epoch boundary differs.
 *
 * Light-cache generation (CPU)
 * ----------------------------
 * This implementation uses a portable C implementation of Keccak-512 to
 * build the light cache on the host.  The sph_keccak routines already
 * present in sph/ are reused for this purpose.
 *
 * DAG generation (GPU)
 * --------------------
 * etchash_generate_dag() in etchash_cuda_miner_kernel.cu generates the
 * full DAG on the device from the uploaded light cache.
 */

/* C++ STL headers (pulled in by etchash_cuda_miner_kernel.h) MUST be
 * included before miner.h, which defines min/max as C macros that
 * break C++ template parsing in <bits/stl_algobase.h>.            */
#include "etchash/etchash_cuda_miner_kernel.h"
#include "etchash/etchash.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "miner.h"

/* ------------------------------------------------------------------ */
/*  SPH Keccak-512 for light-cache generation (host side)              */
/* ------------------------------------------------------------------ */
extern "C" {
#include "sph/sph_keccak.h"
}

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

/* ECIP-1099: epoch length doubled from Ethash's 30 000. */
static const uint32_t ETCHASH_EPOCH_LEN    = 60000U;

/* DAG parameters (identical growth curve to Ethash). */
static const uint64_t ETCHASH_DATASET_INIT = (1ULL << 30);  /* 1 GiB */
static const uint64_t ETCHASH_DATASET_GROW = (1ULL << 23);  /* 8 MiB per epoch */
static const uint64_t ETCHASH_MIX_BYTES    = 128;
static const uint64_t ETCHASH_HASH_BYTES   = 64;
static const uint64_t ETCHASH_DATASET_PARENTS = 256;
static const uint64_t ETCHASH_CACHE_ROUNDS   = 3;
static const uint64_t ETCHASH_ACCESSES_HOST  = 64;

#define ETCHASH_FNV_PRIME 0x01000193U
#define etchash_fnv_host(x, y) ((x) * ETCHASH_FNV_PRIME ^ (y))

/* ------------------------------------------------------------------ */
/*  Per-GPU state                                                       */
/* ------------------------------------------------------------------ */

struct EtchashGpuState {
    int          epoch;          /* -1 = uninitialised */
    uint32_t     dag_size;       /* DAG item count (128-byte nodes) */
    uint32_t     light_size;     /* light cache item count (64-byte nodes) */

    etc_hash128_t* d_dag;        /* device DAG buffer   */
    etc_hash64_t*  d_light;      /* device light buffer */

    volatile EtcSearch_results* d_results; /* search results buffer */

    uint64_t allocated_dag;
    uint64_t allocated_light;
};

static EtchashGpuState s_gpu[MAX_GPUS];
static bool s_initialized = false;

static void etchash_init_states(void)
{
    if (s_initialized) return;
    for (int i = 0; i < MAX_GPUS; i++) {
        s_gpu[i].epoch           = -1;
        s_gpu[i].d_dag           = nullptr;
        s_gpu[i].d_light         = nullptr;
        s_gpu[i].d_results       = nullptr;
        s_gpu[i].allocated_dag   = 0;
        s_gpu[i].allocated_light = 0;
    }
    s_initialized = true;
}

/* ------------------------------------------------------------------ */
/*  Epoch arithmetic                                                    */
/* ------------------------------------------------------------------ */

static inline uint32_t etchash_epoch(uint32_t block_height)
{
    return block_height / ETCHASH_EPOCH_LEN;
}

/*
 * DAG size in 128-byte items for a given epoch.
 * Uses the same prime-number search as Ethash to keep the DAG
 * size a prime multiple of MIX_BYTES.
 */
static uint64_t etchash_dag_byte_size(uint32_t epoch)
{
    uint64_t sz = ETCHASH_DATASET_INIT + ETCHASH_DATASET_GROW * epoch;
    sz -= ETCHASH_MIX_BYTES;
    /* Find largest prime <= sz. */
    while (true) {
        bool is_prime = true;
        for (uint64_t d = 2; d * d <= sz; d++) {
            if (sz % d == 0) { is_prime = false; break; }
        }
        if (is_prime) break;
        sz -= ETCHASH_MIX_BYTES;
    }
    return sz;
}

static uint64_t etchash_light_byte_size(uint32_t epoch)
{
    /* Light cache is 1/1024 of the DAG by default.  Apply same prime search. */
    uint64_t sz = (ETCHASH_DATASET_INIT / 32) + (ETCHASH_DATASET_GROW / 32) * epoch;
    sz -= ETCHASH_HASH_BYTES;
    while (true) {
        bool is_prime = true;
        for (uint64_t d = 2; d * d <= sz; d++) {
            if (sz % d == 0) { is_prime = false; break; }
        }
        if (is_prime) break;
        sz -= ETCHASH_HASH_BYTES;
    }
    return sz;
}

/* ------------------------------------------------------------------ */
/*  Seed hash for an epoch (host-side Keccak-256 chain)                */
/* ------------------------------------------------------------------ */

static void etchash_epoch_seed(uint32_t epoch, uint8_t seed_out[32])
{
    memset(seed_out, 0, 32);
    sph_keccak256_context ctx;
    for (uint32_t i = 0; i < epoch; i++) {
        sph_keccak256_init(&ctx);
        sph_keccak256(&ctx, seed_out, 32);
        sph_keccak256_close(&ctx, seed_out);
    }
}

/* ------------------------------------------------------------------ */
/*  Light-cache generation (host, Keccak-512 RandMemoHash)             */
/* ------------------------------------------------------------------ */

static void etchash_build_light_cache(uint8_t* cache, uint64_t cache_bytes,
                                      const uint8_t* seed)
{
    const uint64_t num_items = cache_bytes / ETCHASH_HASH_BYTES;
    uint64_t* c = (uint64_t*)cache;

    /* Phase 1: seed the first 64-byte item from seed hash. */
    sph_keccak512_context ctx;
    sph_keccak512_init(&ctx);
    sph_keccak512(&ctx, seed, 32);
    sph_keccak512_close(&ctx, cache);

    /* Phase 2: sequentially hash each item from the previous. */
    for (uint64_t i = 1; i < num_items; i++) {
        sph_keccak512_init(&ctx);
        sph_keccak512(&ctx, cache + (i - 1) * ETCHASH_HASH_BYTES, ETCHASH_HASH_BYTES);
        sph_keccak512_close(&ctx, cache + i * ETCHASH_HASH_BYTES);
    }

    /* Phase 3: ETCHASH_CACHE_ROUNDS rounds of RandMemoHash mixing. */
    for (uint64_t r = 0; r < ETCHASH_CACHE_ROUNDS; r++) {
        for (uint64_t i = 0; i < num_items; i++) {
            uint64_t idx = c[i * (ETCHASH_HASH_BYTES / sizeof(uint64_t))] % num_items;
            uint64_t prev = (i + num_items - 1) % num_items;
            uint8_t  tmp[ETCHASH_HASH_BYTES];

            /* XOR item[prev] with item[idx]. */
            uint8_t* pi = cache + prev * ETCHASH_HASH_BYTES;
            uint8_t* qi = cache + idx  * ETCHASH_HASH_BYTES;
            for (uint64_t j = 0; j < ETCHASH_HASH_BYTES; j++)
                tmp[j] = pi[j] ^ qi[j];

            sph_keccak512_init(&ctx);
            sph_keccak512(&ctx, tmp, ETCHASH_HASH_BYTES);
            sph_keccak512_close(&ctx, cache + i * ETCHASH_HASH_BYTES);
        }
    }
    (void)c; /* suppress unused warning on non-uint64-aligned platforms */
}

/* ------------------------------------------------------------------ */
/*  DAG management                                                      */
/* ------------------------------------------------------------------ */

static bool etchash_prepare_dag(int thr_id, uint32_t block_height)
{
    etchash_init_states();

    const int dev_id = device_map[thr_id];
    EtchashGpuState* g = &s_gpu[thr_id];

    const uint32_t epoch     = etchash_epoch(block_height);
    const uint64_t dag_bytes = etchash_dag_byte_size(epoch);
    const uint64_t lgt_bytes = etchash_light_byte_size(epoch);
    const uint32_t dag_items = (uint32_t)(dag_bytes / sizeof(etc_hash64_t));
    const uint32_t lgt_items = (uint32_t)(lgt_bytes / ETCHASH_HASH_BYTES);

    if (g->epoch == (int)epoch)
        return true;  /* DAG already current */

    applog(LOG_INFO, "GPU #%d: ETCHash epoch %u, DAG %.2f GB — generating…",
           dev_id, epoch, (double)dag_bytes / 1073741824.0);

    /* ---- host: build light cache ---- */
    uint8_t seed[32];
    etchash_epoch_seed(epoch, seed);

    uint8_t* h_light = (uint8_t*)malloc(lgt_bytes);
    if (!h_light) {
        applog(LOG_ERR, "GPU #%d: ETCHash malloc light cache failed (%llu bytes)",
               dev_id, (unsigned long long)lgt_bytes);
        return false;
    }
    etchash_build_light_cache(h_light, lgt_bytes, seed);

    /* ---- device: re-allocate if size changed ---- */
    cudaSetDevice(dev_id);

    if (g->allocated_light < lgt_bytes) {
        if (g->d_light) cudaFree(g->d_light);
        if (cudaMalloc((void**)&g->d_light, lgt_bytes) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: cudaMalloc light cache failed", dev_id);
            free(h_light);
            return false;
        }
        g->allocated_light = lgt_bytes;
    }

    if (g->allocated_dag < dag_bytes) {
        if (g->d_dag) cudaFree(g->d_dag);
        if (cudaMalloc((void**)&g->d_dag, dag_bytes) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: cudaMalloc DAG failed (%.2f GB needed)",
                   dev_id, (double)dag_bytes / 1073741824.0);
            free(h_light);
            return false;
        }
        g->allocated_dag = dag_bytes;
    }

    /* Allocate results buffer if needed. */
    if (!g->d_results) {
        if (cudaMalloc((void**)&g->d_results, sizeof(EtcSearch_results)) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: cudaMalloc results failed", dev_id);
            free(h_light);
            return false;
        }
    }

    /* Upload light cache to device. */
    cudaMemcpy(g->d_light, h_light, lgt_bytes, cudaMemcpyHostToDevice);
    free(h_light);

    /* Upload device pointers to constant memory. */
    etchash_set_constants(g->d_dag,  dag_items,
                          g->d_light, lgt_items);

    /* Generate DAG on device. */
    etchash_generate_dag(dag_bytes,
                         /* blocks  */ 8192,
                         /* threads */ 128,
                         /* stream  */ nullptr);

    g->epoch      = (int)epoch;
    g->dag_size   = dag_items;
    g->light_size = lgt_items;

    applog(LOG_INFO, "GPU #%d: ETCHash DAG ready (epoch %u)", dev_id, epoch);
    return true;
}

/* ------------------------------------------------------------------ */
/*  scanhash_etchash                                                    */
/* ------------------------------------------------------------------ */

int scanhash_etchash(int thr_id, struct work* work,
                     uint32_t max_nonce, unsigned long* hashes_done)
{
    const int dev_id = device_map[thr_id];

    if (!etchash_prepare_dag(thr_id, work->height)) {
        applog(LOG_ERR, "GPU #%d: ETCHash DAG preparation failed", dev_id);
        return -1;
    }

    EtchashGpuState* g = &s_gpu[thr_id];
    cudaSetDevice(dev_id);

    /* Upload 32-byte header hash from work->data (first 8 words = 32 bytes). */
    etc_hash32_t header;
    memcpy(header.uint4s, work->data, sizeof(etc_hash32_t));
    etchash_set_header(header);

    /* Upload target: work->target[7] is the most-significant word of the
     * 256-bit LE target.  The kernel compares a 64-bit big-endian value,
     * so we take the upper 64 bits of the target. */
    uint64_t target64;
    memcpy(&target64, &work->target[6], sizeof(uint64_t));
    etchash_set_target(target64);

    /* Zero the results counter. */
    cudaMemset((void*)g->d_results, 0, sizeof(EtcSearch_results));

    const uint32_t start_nonce = work->nonces[0];
    const uint32_t nonces      = max_nonce - start_nonce;

    /* Grid: 128 threads/block, enough blocks to cover all nonces. */
    const uint32_t block_size = 128;
    const uint32_t grid_size  = (nonces + block_size - 1) / block_size;

    etchash_run_search(grid_size, block_size, nullptr,
                       g->d_results, (uint64_t)start_nonce);

    /* Synchronise and collect results. */
    cudaDeviceSynchronize();

    EtcSearch_results h_results;
    cudaMemcpy(&h_results, (void*)g->d_results,
               sizeof(EtcSearch_results), cudaMemcpyDeviceToHost);

    *hashes_done = nonces;

    if (h_results.count == 0)
        return 0;

    /* Report first solution. */
    work->nonces[0] = start_nonce + h_results.result[0].gid;
    work->valid_nonces = 1;

    /* Store mix hash in extra[] for submission. */
    memcpy(work->extra, h_results.result[0].mix,
           sizeof(h_results.result[0].mix));

    return 1;
}

/* ------------------------------------------------------------------ */
/*  free_etchash                                                        */
/* ------------------------------------------------------------------ */

void free_etchash(int thr_id)
{
    EtchashGpuState* g = &s_gpu[thr_id];
    const int dev_id = device_map[thr_id];
    cudaSetDevice(dev_id);

    if (g->d_dag)     { cudaFree(g->d_dag);     g->d_dag     = nullptr; }
    if (g->d_light)   { cudaFree(g->d_light);   g->d_light   = nullptr; }
    if (g->d_results) { cudaFree((void*)g->d_results); g->d_results = nullptr; }

    g->epoch           = -1;
    g->allocated_dag   = 0;
    g->allocated_light = 0;
}
