// autolykos2/reduction.cu
//
// Parallel reduction utilities for the Autolykos v2 prehash pipeline.
//
// The upstream Autolykosminer archive does not ship reduction.cu as a
// separate file; the implementations were compiled into the standalone
// binary.  This file provides equivalent implementations based on the
// interface declared in reduction.h using standard CUDA parallel
// reduction patterns.
//
// All functions are used only during prehash table construction to count
// and locate out-of-range hash entries that require rehashing.

#include "reduction.h"
#include <cuda.h>

// ── CeilToPower ──────────────────────────────────────────────────────────────
// Return smallest power of two >= x.
uint32_t CeilToPower(uint32_t x)
{
    if (x == 0) return 1;
    x--;
    x |= x >> 1;  x |= x >> 2;  x |= x >> 4;
    x |= x >> 8;  x |= x >> 16;
    return x + 1;
}

// ── BlockSum kernel ───────────────────────────────────────────────────────────
// Each block reduces blockSize elements to one partial sum, written to out[].
template<uint32_t blockSize>
__global__ void BlockSum(uint32_t * in, uint32_t inlen, uint32_t * out)
{
    __shared__ uint32_t sdata[blockSize];

    uint32_t tid = threadIdx.x;
    uint32_t idx = blockIdx.x * blockSize + tid;

    sdata[tid] = (idx < inlen) ? in[idx] : 0;
    __syncthreads();

    // Tree reduction within the block
    if (blockSize >= 512) { if (tid < 256) sdata[tid] += sdata[tid+256]; __syncthreads(); }
    if (blockSize >= 256) { if (tid < 128) sdata[tid] += sdata[tid+128]; __syncthreads(); }
    if (blockSize >= 128) { if (tid <  64) sdata[tid] += sdata[tid+ 64]; __syncthreads(); }

    // Warp-level reduction (no sync needed within a warp)
    if (tid < 32) {
        volatile uint32_t *vs = sdata;
        if (blockSize >= 64) vs[tid] += vs[tid+32];
        if (blockSize >= 32) vs[tid] += vs[tid+16];
        if (blockSize >= 16) vs[tid] += vs[tid+ 8];
        if (blockSize >=  8) vs[tid] += vs[tid+ 4];
        if (blockSize >=  4) vs[tid] += vs[tid+ 2];
        if (blockSize >=  2) vs[tid] += vs[tid+ 1];
    }

    if (tid == 0) out[blockIdx.x] = sdata[0];
}

// ── BlockNonZero kernel ───────────────────────────────────────────────────────
// Each block reduces to 1 if any element is non-zero, 0 otherwise.
template<uint32_t blockSize>
__global__ void BlockNonZero(uint32_t * in, uint32_t inlen, uint32_t * out)
{
    __shared__ uint32_t sdata[blockSize];

    uint32_t tid = threadIdx.x;
    uint32_t idx = blockIdx.x * blockSize + tid;

    sdata[tid] = (idx < inlen && in[idx]) ? 1 : 0;
    __syncthreads();

    if (blockSize >= 512) { if (tid < 256) sdata[tid] |= sdata[tid+256]; __syncthreads(); }
    if (blockSize >= 256) { if (tid < 128) sdata[tid] |= sdata[tid+128]; __syncthreads(); }
    if (blockSize >= 128) { if (tid <  64) sdata[tid] |= sdata[tid+ 64]; __syncthreads(); }

    if (tid < 32) {
        volatile uint32_t *vs = sdata;
        if (blockSize >= 64) vs[tid] |= vs[tid+32];
        if (blockSize >= 32) vs[tid] |= vs[tid+16];
        if (blockSize >= 16) vs[tid] |= vs[tid+ 8];
        if (blockSize >=  8) vs[tid] |= vs[tid+ 4];
        if (blockSize >=  4) vs[tid] |= vs[tid+ 2];
        if (blockSize >=  2) vs[tid] |= vs[tid+ 1];
    }

    if (tid == 0) out[blockIdx.x] = sdata[0];
}

// ── ReduceSum ─────────────────────────────────────────────────────────────────
void ReduceSum(uint32_t * in, uint32_t inlen, uint32_t * out,
               uint32_t gridSize, uint32_t blockSize)
{
    // blockSize must be a power-of-two; dispatch the appropriate template
    switch (blockSize) {
        case 512: BlockSum<512><<<gridSize, 512>>>(in, inlen, out); break;
        case 256: BlockSum<256><<<gridSize, 256>>>(in, inlen, out); break;
        case 128: BlockSum<128><<<gridSize, 128>>>(in, inlen, out); break;
        case  64: BlockSum< 64><<<gridSize,  64>>>(in, inlen, out); break;
        default:  BlockSum< 32><<<gridSize,  32>>>(in, inlen, out); break;
    }
}

// ── ReduceNonZero ─────────────────────────────────────────────────────────────
void ReduceNonZero(uint32_t * in, uint32_t inlen, uint32_t * out,
                   uint32_t gridSize, uint32_t blockSize)
{
    switch (blockSize) {
        case 512: BlockNonZero<512><<<gridSize, 512>>>(in, inlen, out); break;
        case 256: BlockNonZero<256><<<gridSize, 256>>>(in, inlen, out); break;
        case 128: BlockNonZero<128><<<gridSize, 128>>>(in, inlen, out); break;
        case  64: BlockNonZero< 64><<<gridSize,  64>>>(in, inlen, out); break;
        default:  BlockNonZero< 32><<<gridSize,  32>>>(in, inlen, out); break;
    }
}

// ── FindSum ───────────────────────────────────────────────────────────────────
// Sum all elements of data[0..inlen-1].
// aux must be a device buffer of at least inlen uint32_t elements.
// Returns result on host.
uint32_t FindSum(uint32_t * data, uint32_t * aux, uint32_t inlen)
{
    if (inlen == 0) return 0;

    const uint32_t block = 256;
    uint32_t len         = inlen;
    uint32_t *src        = data;
    uint32_t *dst        = aux;

    while (len > 1) {
        uint32_t grid = (len + block - 1) / block;
        BlockSum<block><<<grid, block>>>(src, len, dst);
        cudaDeviceSynchronize();
        // Ping-pong: next iteration reduces dst
        uint32_t *tmp = src; src = dst; dst = tmp;
        len = grid;
    }

    uint32_t h_result = 0;
    cudaMemcpy(&h_result, src, sizeof(uint32_t), cudaMemcpyDeviceToHost);
    return h_result;
}

// ── FindNonZero ───────────────────────────────────────────────────────────────
uint32_t FindNonZero(uint32_t * data, uint32_t * aux, uint32_t inlen)
{
    if (inlen == 0) return 0;

    const uint32_t block = 256;
    uint32_t len         = inlen;
    uint32_t *src        = data;
    uint32_t *dst        = aux;

    while (len > 1) {
        uint32_t grid = (len + block - 1) / block;
        BlockNonZero<block><<<grid, block>>>(src, len, dst);
        cudaDeviceSynchronize();
        uint32_t *tmp = src; src = dst; dst = tmp;
        len = grid;
    }

    uint32_t h_result = 0;
    cudaMemcpy(&h_result, src, sizeof(uint32_t), cudaMemcpyDeviceToHost);
    return h_result;
}
