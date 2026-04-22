/**
 * etchash_cuda_miner_kernel.cu
 *
 * ETCHash (ECIP-1099) CUDA search and DAG-generation kernels.
 *
 * Architecture notes
 * ------------------
 * ETCHash is Ethash with epoch_length = 60 000 blocks.  The DAG
 * structure is identical to Ethash; only the epoch boundary changes.
 * The DAG is stored as 128-byte nodes (etc_hash128_t); the light
 * cache uses 64-byte nodes (etc_hash64_t).
 *
 * Thread/block layout
 * -------------------
 *   blockSize must be a multiple of (ETCHASH_THREADS_PER_HASH * 4) = 32.
 *   Recommended: blockSize = 128, gridSize = intensity-dependent.
 *
 * Adapted from etcminer libethash-cuda (GPL-3.0).
 */

#include "etchash_cuda_miner_kernel.h"
#include "etchash_cuda_miner_kernel_globals.h"
#include "etchash_dagger.cuh"

/* ------------------------------------------------------------------ */
/*  Helper: copy uint4 words                                            */
/* ------------------------------------------------------------------ */
#define ETC_COPY(dst, src, count) \
    for (int _i = 0; _i != (count); ++_i) { (dst)[_i] = (src)[_i]; }

/* ------------------------------------------------------------------ */
/*  Search kernel                                                       */
/* ------------------------------------------------------------------ */

__global__ void __launch_bounds__(128, 7)
etchash_search(volatile EtcSearch_results* g_output, uint64_t start_nonce)
{
    const uint32_t gid = blockIdx.x * blockDim.x + threadIdx.x;
    uint2 mix[4];

    if (compute_hash(start_nonce + gid, mix))
        return;     /* not a solution */

    /* Atomically claim a result slot. */
    uint32_t index = atomicInc((uint32_t*)&g_output->count, 0xffffffffU);
    if (index >= ETCHASH_MAX_RESULTS)
        return;

    g_output->result[index].gid    = gid;
    g_output->result[index].mix[0] = mix[0].x;
    g_output->result[index].mix[1] = mix[0].y;
    g_output->result[index].mix[2] = mix[1].x;
    g_output->result[index].mix[3] = mix[1].y;
    g_output->result[index].mix[4] = mix[2].x;
    g_output->result[index].mix[5] = mix[2].y;
    g_output->result[index].mix[6] = mix[3].x;
    g_output->result[index].mix[7] = mix[3].y;
}

void etchash_run_search(uint32_t grid_size, uint32_t block_size,
                        cudaStream_t stream,
                        volatile EtcSearch_results* g_output,
                        uint64_t start_nonce)
{
    etchash_search<<<grid_size, block_size, 0, stream>>>(g_output, start_nonce);
    ETCHASH_CUDA_SAFE_CALL(cudaGetLastError());
}

/* ------------------------------------------------------------------ */
/*  DAG generation kernel                                               */
/* ------------------------------------------------------------------ */

#define ETCHASH_DATASET_PARENTS 256
#define ETCHASH_NODE_WORDS      (64 / 4)

__global__ void etchash_calculate_dag_item(uint32_t start)
{
    const uint32_t node_index = start + blockIdx.x * blockDim.x + threadIdx.x;

    /* Guard: only generate items within the DAG. */
    if (((node_index >> 1) & (~1U)) >= d_etc_dag_size)
        return;

    etc_hash128_t dag_node;

    /* Seed with light-cache item at (node_index % light_size). */
    ETC_COPY(dag_node.uint4s, d_etc_light[node_index % d_etc_light_size].uint4s, 4);
    dag_node.words[0] ^= node_index;
    SHA3_512(dag_node.uint2s);

    const int thread_id = (int)(threadIdx.x & 3);

    for (uint32_t i = 0; i != ETCHASH_DATASET_PARENTS; ++i)
    {
        uint32_t parent_index =
            etchash_fnv(node_index ^ i,
                        dag_node.words[i % ETCHASH_NODE_WORDS])
            % d_etc_light_size;

        for (uint32_t t = 0; t < 4; t++)
        {
            uint32_t shuffle_index = ETC_SHFL(parent_index, (int)t, 4);
            uint4    p4 = d_etc_light[shuffle_index].uint4s[thread_id];

            for (int w = 0; w < 4; w++)
            {
                uint4 s4 = make_uint4(
                    ETC_SHFL(p4.x, w, 4),
                    ETC_SHFL(p4.y, w, 4),
                    ETC_SHFL(p4.z, w, 4),
                    ETC_SHFL(p4.w, w, 4));

                if (t == (uint32_t)thread_id)
                    dag_node.uint4s[w] = etchash_fnv4(dag_node.uint4s[w], s4);
            }
        }
    }

    SHA3_512(dag_node.uint2s);

    /* Write the 64-byte node into the 128-byte DAG slot. */
    etc_hash64_t* dag_nodes = (etc_hash64_t*)d_etc_dag;
    ETC_COPY(dag_nodes[node_index].uint4s, dag_node.uint4s, 4);
}

void etchash_generate_dag(uint64_t dag_size, uint32_t blocks,
                          uint32_t threads, cudaStream_t stream)
{
    const uint32_t work = (uint32_t)(dag_size / sizeof(etc_hash64_t));
    const uint32_t run  = blocks * threads;
    uint32_t base;

    for (base = 0; base <= work - run; base += run)
    {
        etchash_calculate_dag_item<<<blocks, threads, 0, stream>>>(base);
        ETCHASH_CUDA_SAFE_CALL(cudaDeviceSynchronize());
    }
    if (base < work)
    {
        uint32_t last_grid = work - base;
        last_grid = (last_grid + threads - 1) / threads;
        etchash_calculate_dag_item<<<last_grid, threads, 0, stream>>>(base);
        ETCHASH_CUDA_SAFE_CALL(cudaDeviceSynchronize());
    }
    ETCHASH_CUDA_SAFE_CALL(cudaGetLastError());
}

/* ------------------------------------------------------------------ */
/*  Constant-memory accessors                                           */
/* ------------------------------------------------------------------ */

void etchash_set_constants(etc_hash128_t* dag,  uint32_t dag_size,
                           etc_hash64_t*  light, uint32_t light_size)
{
    ETCHASH_CUDA_SAFE_CALL(
        cudaMemcpyToSymbol(d_etc_dag,        &dag,        sizeof(etc_hash128_t*)));
    ETCHASH_CUDA_SAFE_CALL(
        cudaMemcpyToSymbol(d_etc_dag_size,   &dag_size,   sizeof(uint32_t)));
    ETCHASH_CUDA_SAFE_CALL(
        cudaMemcpyToSymbol(d_etc_light,      &light,      sizeof(etc_hash64_t*)));
    ETCHASH_CUDA_SAFE_CALL(
        cudaMemcpyToSymbol(d_etc_light_size, &light_size, sizeof(uint32_t)));
}

void etchash_get_constants(etc_hash128_t** dag,  uint32_t* dag_size,
                           etc_hash64_t**  light, uint32_t* light_size)
{
    if (dag) {
        etc_hash128_t* d;
        ETCHASH_CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&d, d_etc_dag, sizeof(etc_hash128_t*)));
        *dag = d;
    }
    if (dag_size) {
        uint32_t ds;
        ETCHASH_CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&ds, d_etc_dag_size, sizeof(uint32_t)));
        *dag_size = ds;
    }
    if (light) {
        etc_hash64_t* l;
        ETCHASH_CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&l, d_etc_light, sizeof(etc_hash64_t*)));
        *light = l;
    }
    if (light_size) {
        uint32_t ls;
        ETCHASH_CUDA_SAFE_CALL(cudaMemcpyFromSymbol(&ls, d_etc_light_size, sizeof(uint32_t)));
        *light_size = ls;
    }
}

void etchash_set_header(etc_hash32_t header, cudaStream_t stream)
{
    /* cudaMemcpyToSymbolAsync to constant memory:
     * The CUDA runtime copies the 32-byte header value into an internal
     * staging buffer before this call returns, so the caller's stack-allocated
     * 'header' does not need to be pinned.  The copy is ordered before any
     * subsequent work enqueued on 'stream', guaranteeing the kernel sees the
     * updated value. */
    ETCHASH_CUDA_SAFE_CALL(
        cudaMemcpyToSymbolAsync(d_etc_header, &header, sizeof(etc_hash32_t),
                                0, cudaMemcpyHostToDevice, stream));
}

void etchash_set_target(uint64_t target, cudaStream_t stream)
{
    ETCHASH_CUDA_SAFE_CALL(
        cudaMemcpyToSymbolAsync(d_etc_target, &target, sizeof(uint64_t),
                                0, cudaMemcpyHostToDevice, stream));
}
