// autolykos2/compaction.cu
//
// Stream compaction for the Autolykos v2 prehash pipeline.
//
// Compactify removes zero entries from the invalid-index array, producing
// a dense list that UpdatePrehash can iterate over efficiently.
//
// Same rationale as reduction.cu — the upstream archive does not ship
// this as a separate file; we provide an equivalent implementation.

#include "compaction.h"
#include <cuda.h>

// ── WarpInc ───────────────────────────────────────────────────────────────────
// Atomically increment *len within a warp using ballot/popc.
// Returns the thread-local offset for writing into the output array.
__device__ uint32_t WarpInc(uint32_t * len)
{
#if (__CUDA_ARCH__ >= 300)
    // Ballot: how many threads in this warp are active (always all in uniform)
    uint32_t mask  = __ballot_sync(0xFFFFFFFFU, 1);
    uint32_t count = __popc(mask);
    uint32_t lane  = threadIdx.x & 31U;

    uint32_t base = 0;
    if (lane == 0)
        base = atomicAdd(len, count);

    // Broadcast base to all lanes in the warp
    base = __shfl_sync(0xFFFFFFFFU, base, 0);

    // Each thread's offset = base + number of active lanes below it
    uint32_t prefix = __popc(mask & ((1U << lane) - 1U));
    return base + prefix;
#else
    return atomicAdd(len, 1U);
#endif
}

// ── Compactify ────────────────────────────────────────────────────────────────
// Write non-zero elements of in[0..inlen-1] to out[], recording the count
// in *outlen (device pointer).  Caller zeroes *outlen before launch.
__global__ void Compactify(const uint32_t * in,
                           const uint32_t   inlen,
                           uint32_t       * out,
                           uint32_t       * outlen)
{
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= inlen) return;

    uint32_t val = in[idx];
    if (val != 0)
    {
        uint32_t pos = WarpInc(outlen);
        out[pos] = val;
    }
}
