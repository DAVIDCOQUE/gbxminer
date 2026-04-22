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

/*
 * ETCHASH_DAG_LOOKAHEAD_BYTES
 *
 * Extra bytes reserved beyond the current epoch's DAG size on the first
 * allocation.  Two full epochs of growth (2 × 8 MiB = 16 MiB) means the
 * GPU DAG buffer survives two epoch transitions before a reallocation is
 * needed.  This eliminates the cudaFree + cudaMalloc + GPU-idle stall that
 * the original code incurred on every epoch change.
 *
 * Value: 2 × ETCHASH_DATASET_GROW = 2 × (1 << 23) = 16 777 216 bytes.
 */
static const uint64_t ETCHASH_DAG_LOOKAHEAD_BYTES = 2ULL * (1ULL << 23);

struct EtchashGpuState {
    int          epoch;          /* -1 = uninitialised */
    uint32_t     dag_size;       /* DAG item count (128-byte nodes) */
    uint32_t     light_size;     /* light cache item count (64-byte nodes) */

    etc_hash128_t* d_dag;        /* device DAG buffer   */
    etc_hash64_t*  d_light;      /* device light buffer */

    volatile EtcSearch_results* d_results; /* search results buffer */

    uint64_t allocated_dag;      /* current GPU allocation capacity (bytes) */
    uint64_t allocated_light;    /* current GPU allocation capacity (bytes) */

    /*
     * Pinned host staging buffer for the light cache.
     *
     * cudaMallocHost() allocates page-locked (pinned) host memory.  The CUDA
     * DMA engine can then transfer directly from this buffer without an
     * intermediate pageable bounce-copy, raising effective PCIe throughput
     * from ~4 GB/s (pageable) to ~12 GB/s (pinned) on PCIe 3.0 x16.
     *
     * h_light_pinned is allocated on first use and kept for the lifetime of
     * the miner; it is freed in free_etchash() via cudaFreeHost().  If
     * cudaMallocHost() fails (e.g. no driver, CI node), the code falls back
     * to a regular malloc() allocation so the miner still functions.
     *
     * pinned_light_cap tracks the current capacity so the buffer is only
     * reallocated when the light cache grows past the existing reservation.
     */
    uint8_t* h_light_pinned;     /* pinned host staging buffer (or malloc fallback) */
    uint64_t pinned_light_cap;   /* byte capacity of h_light_pinned */
    bool     pinned_alloc_ok;    /* true = cudaMallocHost succeeded */

    /*
     * Per-scan CUDA stream and pinned result buffer.
     *
     * All per-scan operations — header upload, target upload, result-counter
     * reset, kernel launch, and D2H result copy — are enqueued on this single
     * named stream.  This replaces the default-stream (nullptr) pattern and
     * eliminates three synchronous CPU-blocking operations that previously
     * occurred before each kernel launch:
     *
     *   Before: cudaMemcpyToSymbol(header)  ~10 µs CPU blocks
     *           cudaMemcpyToSymbol(target)   ~10 µs CPU blocks
     *           cudaMemset(results)           ~5 µs CPU blocks
     *           kernel launch                 returns immediately
     *           cudaDeviceSynchronize()       ~ms (kernel + CPU wakes)
     *           cudaMemcpy D2H                ~5 µs CPU blocks
     *
     *   After:  cudaMemcpyToSymbolAsync(header, stream)  returns immediately
     *           cudaMemcpyToSymbolAsync(target, stream)  returns immediately
     *           cudaMemsetAsync(results, stream)          returns immediately
     *           kernel launch on stream                   returns immediately
     *           cudaMemcpyAsync D2H on stream             returns immediately
     *           cudaStreamSynchronize(stream)             ~ms (kernel + D2H)
     *
     * The ~25 µs of blocking CPU overhead before each kernel launch is
     * eliminated.  One cudaStreamSynchronize covers both kernel completion
     * and the D2H result copy, removing a second synchronization round-trip.
     *
     * h_results_pinned must be pinned (cudaMallocHost) for cudaMemcpyAsync
     * DeviceToHost to avoid falling back to synchronous behaviour.  It is
     * allocated once and reused across scan windows.
     */
    cudaStream_t          stream;           /* named per-GPU CUDA stream */
    EtcSearch_results*    h_results_pinned; /* pinned host result buffer */
    bool                  stream_created;   /* lazy-init guard */
};

static EtchashGpuState s_gpu[MAX_GPUS];
static bool s_initialized = false;

static void etchash_init_states(void)
{
    if (s_initialized) return;
    for (int i = 0; i < MAX_GPUS; i++) {
        s_gpu[i].epoch            = -1;
        s_gpu[i].d_dag            = nullptr;
        s_gpu[i].d_light          = nullptr;
        s_gpu[i].d_results        = nullptr;
        s_gpu[i].allocated_dag    = 0;
        s_gpu[i].allocated_light  = 0;
        s_gpu[i].h_light_pinned   = nullptr;
        s_gpu[i].pinned_light_cap = 0;
        s_gpu[i].pinned_alloc_ok  = false;
        s_gpu[i].stream           = nullptr;
        s_gpu[i].h_results_pinned = nullptr;
        s_gpu[i].stream_created   = false;
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

    cudaSetDevice(dev_id);

    /* ------------------------------------------------------------------
     * Pinned host staging buffer for the light cache.
     *
     * Grow the pinned buffer when the new light cache exceeds the current
     * reservation.  On first use pinned_light_cap == 0 so this always
     * allocates.  The buffer is never shrunk; cudaFreeHost is only called
     * in free_etchash().
     *
     * Fallback: if cudaMallocHost() fails (headless CI nodes, no driver),
     * fall back to regular malloc() so the miner still functions.  The
     * bool flag pinned_alloc_ok lets free_etchash() call the right free.
     * ------------------------------------------------------------------ */
    if (g->pinned_light_cap < lgt_bytes) {
        /* Release the previous buffer, if any. */
        if (g->h_light_pinned) {
            if (g->pinned_alloc_ok)
                cudaFreeHost(g->h_light_pinned);
            else
                free(g->h_light_pinned);
            g->h_light_pinned   = nullptr;
            g->pinned_light_cap = 0;
            g->pinned_alloc_ok  = false;
        }

        if (cudaMallocHost((void**)&g->h_light_pinned, lgt_bytes) == cudaSuccess) {
            g->pinned_light_cap = lgt_bytes;
            g->pinned_alloc_ok  = true;
        } else {
            /* Pinned allocation failed — fall back to pageable malloc(). */
            applog(LOG_WARNING,
                   "GPU #%d: cudaMallocHost light cache failed, "
                   "falling back to pageable malloc", dev_id);
            g->h_light_pinned = (uint8_t*)malloc(lgt_bytes);
            if (!g->h_light_pinned) {
                applog(LOG_ERR,
                       "GPU #%d: malloc light cache failed (%llu bytes)",
                       dev_id, (unsigned long long)lgt_bytes);
                return false;
            }
            g->pinned_light_cap = lgt_bytes;
            g->pinned_alloc_ok  = false;
        }
    }

    /* Build the light cache into the (pinned or pageable) staging buffer. */
    uint8_t seed[32];
    etchash_epoch_seed(epoch, seed);
    etchash_build_light_cache(g->h_light_pinned, lgt_bytes, seed);

    /* ------------------------------------------------------------------
     * Persistent GPU DAG allocation.
     *
     * Over-provision by ETCHASH_DAG_LOOKAHEAD_BYTES on the first call so
     * the next two epoch transitions do not trigger cudaFree + cudaMalloc.
     * cudaFree is synchronous: it blocks until all pending GPU work drains
     * and then releases the buffer, idling the GPU for tens of milliseconds.
     *
     * We only reallocate when the new DAG genuinely exceeds the existing
     * allocation — i.e. when the lookahead margin has been exhausted.
     * ------------------------------------------------------------------ */
    if (g->allocated_dag < dag_bytes) {
        if (g->d_dag) {
            applog(LOG_INFO,
                   "GPU #%d: DAG allocation exhausted lookahead — "
                   "reallocating (%.2f GB → %.2f GB)",
                   dev_id,
                   (double)g->allocated_dag  / 1073741824.0,
                   (double)(dag_bytes + ETCHASH_DAG_LOOKAHEAD_BYTES) / 1073741824.0);
            cudaFree(g->d_dag);
            g->d_dag          = nullptr;
            g->allocated_dag  = 0;
        }

        const uint64_t alloc_size = dag_bytes + ETCHASH_DAG_LOOKAHEAD_BYTES;
        if (cudaMalloc((void**)&g->d_dag, alloc_size) != cudaSuccess) {
            applog(LOG_ERR,
                   "GPU #%d: cudaMalloc DAG failed (%.2f GB needed)",
                   dev_id, (double)alloc_size / 1073741824.0);
            return false;
        }
        g->allocated_dag = alloc_size;
    }

    /* Light cache device allocation — grows only when needed. */
    if (g->allocated_light < lgt_bytes) {
        if (g->d_light) {
            cudaFree(g->d_light);
            g->d_light         = nullptr;
            g->allocated_light = 0;
        }
        if (cudaMalloc((void**)&g->d_light, lgt_bytes) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: cudaMalloc light cache failed", dev_id);
            return false;
        }
        g->allocated_light = lgt_bytes;
    }

    /* Results buffer — allocated once, never resized. */
    if (!g->d_results) {
        if (cudaMalloc((void**)&g->d_results,
                       sizeof(EtcSearch_results)) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: cudaMalloc results failed", dev_id);
            return false;
        }
    }

    /* Upload light cache — full PCIe bandwidth when h_light_pinned is
     * page-locked; automatic bounce-buffer path otherwise (fallback).    */
    cudaMemcpy(g->d_light, g->h_light_pinned, lgt_bytes,
               cudaMemcpyHostToDevice);

    /* Upload device pointers to constant memory. */
    etchash_set_constants(g->d_dag,  dag_items,
                          g->d_light, lgt_items);

    /* Generate DAG on device (in-place, over the existing allocation). */
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

    /* ------------------------------------------------------------------
     * Lazy stream and pinned result buffer initialisation.
     *
     * Done here rather than in etchash_prepare_dag() to keep epoch-change
     * logic separate from per-scan setup.  Both objects live for the miner
     * lifetime and are freed in free_etchash().
     * ------------------------------------------------------------------ */
    if (!g->stream_created) {
        if (cudaStreamCreate(&g->stream) != cudaSuccess) {
            applog(LOG_ERR, "GPU #%d: ETCHash cudaStreamCreate failed", dev_id);
            return -1;
        }
        if (cudaMallocHost((void**)&g->h_results_pinned,
                           sizeof(EtcSearch_results)) != cudaSuccess) {
            applog(LOG_ERR,
                   "GPU #%d: ETCHash cudaMallocHost results failed", dev_id);
            cudaStreamDestroy(g->stream);
            g->stream = nullptr;
            return -1;
        }
        g->stream_created = true;
    }

    /* ------------------------------------------------------------------
     * Async per-scan pipeline.
     *
     * All five operations are enqueued onto g->stream before the CPU waits.
     * Same-stream ordering guarantees the kernel sees the header and target
     * written by the two MemcpyToSymbolAsync calls, and that the D2H copy
     * reads the results the kernel has written.
     * ------------------------------------------------------------------ */

    /* 1. Upload 32-byte header hash from work->data. */
    etc_hash32_t header;
    memcpy(header.uint4s, work->data, sizeof(etc_hash32_t));
    etchash_set_header(header, g->stream);

    /* 2. Upload 64-bit difficulty target (upper 64 bits of 256-bit LE target). */
    uint64_t target64;
    memcpy(&target64, &work->target[6], sizeof(uint64_t));
    etchash_set_target(target64, g->stream);

    /* 3. Reset results counter asynchronously. */
    cudaMemsetAsync((void*)g->d_results, 0, sizeof(EtcSearch_results), g->stream);

    /* 4. Launch search kernel on the named stream. */
    const uint32_t start_nonce = work->nonces[0];
    const uint32_t nonces      = max_nonce - start_nonce;
    const uint32_t block_size  = 128;
    const uint32_t grid_size   = (nonces + block_size - 1) / block_size;

    etchash_run_search(grid_size, block_size, g->stream,
                       g->d_results, (uint64_t)start_nonce);

    /* 5. Async D2H: copy results to pinned host buffer.
     *    cudaMemcpyAsync DeviceToHost requires pinned destination; h_results_pinned
     *    satisfies this, avoiding a synchronous fallback. */
    cudaMemcpyAsync(g->h_results_pinned, (void*)g->d_results,
                    sizeof(EtcSearch_results),
                    cudaMemcpyDeviceToHost, g->stream);

    /* Single synchronisation point: blocks until the entire stream — symbol
     * writes, kernel, and D2H — has completed.  Replaces the previous
     * cudaDeviceSynchronize() + synchronous cudaMemcpy pair. */
    cudaStreamSynchronize(g->stream);

    *hashes_done = nonces;

    if (g->h_results_pinned->count == 0)
        return 0;

    /* Report first solution. */
    work->nonces[0]    = start_nonce + g->h_results_pinned->result[0].gid;
    work->valid_nonces = 1;

    /* Store mix hash in extra[] for submission. */
    memcpy(work->extra, g->h_results_pinned->result[0].mix,
           sizeof(g->h_results_pinned->result[0].mix));

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

    /* Drain the stream before freeing any buffers it may reference. */
    if (g->stream_created && g->stream)
        cudaStreamSynchronize(g->stream);

    if (g->d_dag)     { cudaFree(g->d_dag);     g->d_dag     = nullptr; }
    if (g->d_light)   { cudaFree(g->d_light);   g->d_light   = nullptr; }
    if (g->d_results) { cudaFree((void*)g->d_results); g->d_results = nullptr; }

    /* Release the pinned (or pageable fallback) light cache staging buffer. */
    if (g->h_light_pinned) {
        if (g->pinned_alloc_ok)
            cudaFreeHost(g->h_light_pinned);
        else
            free(g->h_light_pinned);
        g->h_light_pinned   = nullptr;
        g->pinned_light_cap = 0;
        g->pinned_alloc_ok  = false;
    }

    /* Release per-scan stream and pinned result buffer. */
    if (g->h_results_pinned) {
        cudaFreeHost(g->h_results_pinned);
        g->h_results_pinned = nullptr;
    }
    if (g->stream_created && g->stream) {
        cudaStreamDestroy(g->stream);
        g->stream         = nullptr;
        g->stream_created = false;
    }

    g->epoch           = -1;
    g->allocated_dag   = 0;
    g->allocated_light = 0;
}
