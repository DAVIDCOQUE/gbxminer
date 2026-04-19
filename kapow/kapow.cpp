/**
 * kapow.cpp
 *
 * CPU-side KawPow (Ravencoin ProgPoW) integration for gbxminer.
 *
 * DAG management is shared with ETCHash (same Keccak-based structure,
 * different epoch length: 7 500 blocks vs 60 000 for ETCHash).
 * The light-cache and DAG generation routines are the same; only the
 * epoch arithmetic differs.
 *
 * Per-period kernel JIT compilation is handled by
 * kapow_cuda_miner_kernel.cpp via NVRTC.  If NVRTC is absent at build
 * time, scanhash_kawpow() logs an error and returns -1.
 */

/* C++ STL headers MUST precede miner.h (which defines min/max macros
 * that break C++ template parsing in <bits/stl_algobase.h>).     */
#include "kapow/kapow_cuda_miner_kernel.h"
#include "kapow/ProgPow.h"
#include "etchash/etchash_cuda_miner_kernel.h"
#include "kapow/kapow.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "miner.h"

extern "C" {
#include "sph/sph_keccak.h"
} /* end extern "C" */

#ifdef HAVE_NVRTC
/* ------------------------------------------------------------------ */
/*  Full KawPow implementation (requires NVRTC for JIT compilation)     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

static const uint32_t KAWPOW_EPOCH_LEN    = 7500U;
static const uint64_t KAWPOW_DATASET_INIT = (1ULL << 30);
static const uint64_t KAWPOW_DATASET_GROW = (1ULL << 23);
static const uint64_t KAWPOW_MIX_BYTES    = 128;
static const uint64_t KAWPOW_HASH_BYTES   = 64;

#define KAWPOW_FNV_PRIME 0x01000193U
#define kawpow_fnv(x, y) ((x) * KAWPOW_FNV_PRIME ^ (y))

/* ------------------------------------------------------------------ */
/*  Per-GPU state                                                       */
/* ------------------------------------------------------------------ */

struct KawpowGpuState {
    int          epoch;
    uint64_t     period;     /* last compiled kernel period */
    uint32_t     dag_size;
    uint32_t     light_size;

    etc_hash128_t*             d_dag;
    etc_hash64_t*              d_light;
    volatile KawpowSearch_results* d_results;
    CUmodule                   module;   /* current compiled ProgPoW kernel */

    uint64_t allocated_dag;
    uint64_t allocated_light;
};

static KawpowGpuState s_kawpow[MAX_GPUS];
static bool s_kawpow_init = false;

static void kawpow_init_states(void)
{
    if (s_kawpow_init) return;
    for (int i = 0; i < MAX_GPUS; i++) {
        s_kawpow[i].epoch           = -1;
        s_kawpow[i].period          = UINT64_MAX;
        s_kawpow[i].d_dag           = nullptr;
        s_kawpow[i].d_light         = nullptr;
        s_kawpow[i].d_results       = nullptr;
        s_kawpow[i].module          = nullptr;
        s_kawpow[i].allocated_dag   = 0;
        s_kawpow[i].allocated_light = 0;
    }
    s_kawpow_init = true;
}

/* ------------------------------------------------------------------ */
/*  Epoch/period arithmetic                                             */
/* ------------------------------------------------------------------ */

static inline uint32_t kawpow_epoch(uint32_t block_height)
{
    return block_height / KAWPOW_EPOCH_LEN;
}

static inline uint64_t kawpow_period(uint32_t block_height)
{
    return (uint64_t)(block_height / PROGPOW_PERIOD);
}

static uint64_t kawpow_dag_bytes(uint32_t epoch)
{
    uint64_t sz = KAWPOW_DATASET_INIT + KAWPOW_DATASET_GROW * epoch - KAWPOW_MIX_BYTES;
    while (true) {
        bool ok = true;
        for (uint64_t d = 2; d * d <= sz; d++) {
            if (sz % d == 0) { ok = false; break; }
        }
        if (ok) break;
        sz -= KAWPOW_MIX_BYTES;
    }
    return sz;
}

static uint64_t kawpow_light_bytes(uint32_t epoch)
{
    uint64_t sz = (KAWPOW_DATASET_INIT / 32) + (KAWPOW_DATASET_GROW / 32) * epoch
                  - KAWPOW_HASH_BYTES;
    while (true) {
        bool ok = true;
        for (uint64_t d = 2; d * d <= sz; d++) {
            if (sz % d == 0) { ok = false; break; }
        }
        if (ok) break;
        sz -= KAWPOW_HASH_BYTES;
    }
    return sz;
}

/* ------------------------------------------------------------------ */
/*  Light-cache generation (identical algorithm to ETCHash)             */
/* ------------------------------------------------------------------ */

static void kawpow_epoch_seed(uint32_t epoch, uint8_t seed_out[32])
{
    memset(seed_out, 0, 32);
    sph_keccak256_context ctx;
    for (uint32_t i = 0; i < epoch; i++) {
        sph_keccak256_init(&ctx);
        sph_keccak256(&ctx, seed_out, 32);
        sph_keccak256_close(&ctx, seed_out);
    }
}

static void kawpow_build_light_cache(uint8_t* cache, uint64_t cache_bytes,
                                    const uint8_t* seed)
{
    const uint64_t num_items = cache_bytes / KAWPOW_HASH_BYTES;
    sph_keccak512_context ctx;

    sph_keccak512_init(&ctx);
    sph_keccak512(&ctx, seed, 32);
    sph_keccak512_close(&ctx, cache);

    for (uint64_t i = 1; i < num_items; i++) {
        sph_keccak512_init(&ctx);
        sph_keccak512(&ctx, cache + (i - 1) * KAWPOW_HASH_BYTES, KAWPOW_HASH_BYTES);
        sph_keccak512_close(&ctx, cache + i * KAWPOW_HASH_BYTES);
    }
    for (uint64_t r = 0; r < 3; r++) {
        for (uint64_t i = 0; i < num_items; i++) {
            uint32_t first_word;
            memcpy(&first_word, cache + i * KAWPOW_HASH_BYTES, sizeof(uint32_t));
            uint64_t idx  = first_word % num_items;
            uint64_t prev = (i + num_items - 1) % num_items;
            uint8_t  tmp[KAWPOW_HASH_BYTES];
            for (uint64_t j = 0; j < KAWPOW_HASH_BYTES; j++)
                tmp[j] = cache[prev * KAWPOW_HASH_BYTES + j]
                        ^ cache[idx  * KAWPOW_HASH_BYTES + j];
            sph_keccak512_init(&ctx);
            sph_keccak512(&ctx, tmp, KAWPOW_HASH_BYTES);
            sph_keccak512_close(&ctx, cache + i * KAWPOW_HASH_BYTES);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  DAG preparation                                                     */
/* ------------------------------------------------------------------ */

static bool kawpow_prepare_dag(int thr_id, uint32_t block_height)
{
    kawpow_init_states();

    const int dev_id = device_map[thr_id];
    KawpowGpuState* g = &s_kawpow[thr_id];

    const uint32_t epoch     = kawpow_epoch(block_height);
    const uint64_t dag_bytes = kawpow_dag_bytes(epoch);
    const uint64_t lgt_bytes = kawpow_light_bytes(epoch);
    const uint32_t dag_items = (uint32_t)(dag_bytes / sizeof(etc_hash64_t));
    const uint32_t lgt_items = (uint32_t)(lgt_bytes / KAWPOW_HASH_BYTES);

    if (g->epoch == (int)epoch)
        return true;

    applog(LOG_INFO, "GPU #%d: KawPow epoch %u, DAG %.2f GB — generating…",
           dev_id, epoch, (double)dag_bytes / 1073741824.0);

    uint8_t seed[32];
    kawpow_epoch_seed(epoch, seed);

    uint8_t* h_light = (uint8_t*)malloc(lgt_bytes);
    if (!h_light) {
        applog(LOG_ERR, "GPU #%d: KawPow malloc light cache failed", dev_id);
        return false;
    }
    kawpow_build_light_cache(h_light, lgt_bytes, seed);

    cudaSetDevice(dev_id);

    if (g->allocated_light < lgt_bytes) {
        if (g->d_light) cudaFree(g->d_light);
        if (cudaMalloc((void**)&g->d_light, lgt_bytes) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: KawPow cudaMalloc light failed", dev_id);
            free(h_light); return false;
        }
        g->allocated_light = lgt_bytes;
    }
    if (g->allocated_dag < dag_bytes) {
        if (g->d_dag) cudaFree(g->d_dag);
        if (cudaMalloc((void**)&g->d_dag, dag_bytes) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: KawPow cudaMalloc DAG failed", dev_id);
            free(h_light); return false;
        }
        g->allocated_dag = dag_bytes;
    }
    if (!g->d_results) {
        if (cudaMalloc((void**)&g->d_results,
                       sizeof(KawpowSearch_results)) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: KawPow cudaMalloc results failed", dev_id);
            free(h_light); return false;
        }
    }

    cudaMemcpy(g->d_light, h_light, lgt_bytes, cudaMemcpyHostToDevice);
    free(h_light);

    /* Re-use ETCHash DAG generation kernel (same algorithm). */
    etchash_set_constants(g->d_dag, dag_items, g->d_light, lgt_items);
    etchash_generate_dag(dag_bytes, 8192, 128, nullptr);

    g->epoch      = (int)epoch;
    g->dag_size   = dag_items;
    g->light_size = lgt_items;

    applog(LOG_INFO, "GPU #%d: KawPow DAG ready (epoch %u)", dev_id, epoch);
    return true;
}

/* ------------------------------------------------------------------ */
/*  scanhash_kawpow                                                      */
/* ------------------------------------------------------------------ */

int scanhash_kawpow(int thr_id, struct work* work,
                   uint32_t max_nonce, unsigned long* hashes_done)
{
    const int dev_id = device_map[thr_id];
    KawpowGpuState* g = &s_kawpow[thr_id];

    if (!kawpow_prepare_dag(thr_id, work->height)) {
        applog(LOG_ERR, "GPU #%d: KawPow DAG preparation failed", dev_id);
        return -1;
    }

    cudaSetDevice(dev_id);

    /* Compile/retrieve the period-specific kernel. */
    const uint64_t period = kawpow_period(work->height);
    if (period != g->period) {
        CUmodule mod;
        if (!kawpow_compile_kernel(period, &mod)) {
            applog(LOG_ERR, "GPU #%d: KawPow kernel compile failed (period %llu)",
                   dev_id, (unsigned long long)period);
            return -1;
        }
        g->module = mod;
        g->period = period;
    }

    /* Extract header hash (first 8 words = 32 bytes). */
    uint32_t header[8];
    memcpy(header, work->data, sizeof(header));

    /* Target: upper 64 bits of the 256-bit LE target. */
    uint64_t target64;
    memcpy(&target64, &work->target[6], sizeof(uint64_t));

    const uint32_t start_nonce = work->nonces[0];
    const uint32_t nonces      = max_nonce - start_nonce;
    const uint32_t block_size  = PROGPOW_LANES * 4;   /* = 64 threads */
    const uint32_t grid_size   = (nonces + block_size - 1) / block_size;

    cudaMemset((void*)g->d_results, 0, sizeof(KawpowSearch_results));

    kawpow_run_search(g->module, grid_size, block_size, nullptr,
                     g->d_results,
                     (uint64_t)start_nonce,
                     g->d_dag, g->dag_size,
                     header, target64);

    cudaDeviceSynchronize();

    KawpowSearch_results h_res;
    cudaMemcpy(&h_res, (void*)g->d_results,
               sizeof(KawpowSearch_results), cudaMemcpyDeviceToHost);

    *hashes_done = nonces;

    if (h_res.count == 0)
        return 0;

    work->nonces[0]    = start_nonce + h_res.result[0].gid;
    work->valid_nonces = 1;
    memcpy(work->extra, h_res.result[0].mix, sizeof(h_res.result[0].mix));

    return 1;
}

/* ------------------------------------------------------------------ */
/*  free_kawpow                                                          */
/* ------------------------------------------------------------------ */

void free_kawpow(int thr_id)
{
    KawpowGpuState* g = &s_kawpow[thr_id];
    const int dev_id = device_map[thr_id];
    cudaSetDevice(dev_id);

    if (g->d_dag)     { cudaFree(g->d_dag);                 g->d_dag     = nullptr; }
    if (g->d_light)   { cudaFree(g->d_light);               g->d_light   = nullptr; }
    if (g->d_results) { cudaFree((void*)g->d_results);       g->d_results = nullptr; }

    kawpow_free_modules();

    g->epoch           = -1;
    g->period          = UINT64_MAX;
    g->module          = nullptr;
    g->allocated_dag   = 0;
    g->allocated_light = 0;
}

#else  /* HAVE_NVRTC not defined */
/* ------------------------------------------------------------------ */
/*  KawPow stubs — NVRTC not available at build time.                   */
/*  scanhash_kawpow() logs an error and returns -1.                     */
/*  free_kawpow() is a no-op.                                           */
/* ------------------------------------------------------------------ */
int scanhash_kawpow(int thr_id, struct work *work,
                   uint32_t max_nonce, unsigned long *hashes_done)
{
    (void)thr_id; (void)work; (void)max_nonce; (void)hashes_done;
    applog(LOG_ERR,
           "KawPow: built without NVRTC support — cannot mine KawPow. "
           "Rebuild with --with-nvrtc to enable.");
    return -1;
}

void free_kawpow(int thr_id)
{
    (void)thr_id;
}
#endif /* HAVE_NVRTC */
