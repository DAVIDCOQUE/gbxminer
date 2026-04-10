/**
 * autolykos2_cuda.cu
 *
 * CUDA launch wrapper for Autolykos v2 GPU kernels.
 *
 * This translation unit is compiled by nvcc and contains all <<<...>>>
 * kernel launch syntax.  autolykos2.cpp (compiled by g++) calls these
 * plain C functions via the autolykos2_cuda.h interface.
 *
 * Separation rationale: the min/max macro conflict described in etchash.cpp
 * applies here too.  Keeping CUDA launches in a .cu and C++ logic in .cpp
 * avoids include-order gymnastics.
 */

#include "prehash.h"
#include "mining.h"
#include "compaction.h"
#include "reduction.h"

#include "autolykos2_cuda.h"

#include <cuda_runtime.h>

/* ------------------------------------------------------------------ */

void al2_launch_init_prehash(uint32_t *d_data,
                             uint32_t *d_hashes,
                             uint32_t *d_invalid,
                             uint32_t  grid,
                             uint32_t  block)
{
    InitPrehash<<<grid, block>>>(d_data, d_hashes, d_invalid);
}

void al2_launch_update_prehash(uint32_t *d_hashes,
                               uint32_t *d_compact_inv,
                               uint32_t  inv_count,
                               uint32_t  block)
{
    UpdatePrehash<<<(inv_count + block - 1) / block, block>>>(
        d_hashes, d_compact_inv, inv_count);
}

void al2_launch_final_prehash(uint32_t *d_hashes,
                              uint32_t  grid,
                              uint32_t  block)
{
    FinalPrehash<<<grid, block>>>(d_hashes);
}

void al2_launch_compactify(const uint32_t *d_in,
                           uint32_t        inlen,
                           uint32_t       *d_out,
                           uint32_t       *d_outlen,
                           uint32_t        block)
{
    uint32_t grid = (inlen + block - 1) / block;
    Compactify<<<grid, block>>>(d_in, inlen, d_out, d_outlen);
}

void al2_launch_blake_hash(const uint32_t *d_data,
                           uint64_t        base,
                           uint32_t       *d_bhashes,
                           uint32_t        grid,
                           uint32_t        block)
{
    BlakeHash<<<grid, block>>>(d_data, base, d_bhashes);
}

void al2_launch_block_mining(const uint32_t *d_bound,
                             const uint32_t *d_data,
                             uint64_t        base,
                             const uint32_t *d_hashes,
                             uint32_t       *d_results,
                             uint32_t       *d_valid,
                             uint32_t       *d_count,
                             uint32_t       *d_bhashes,
                             uint32_t        grid,
                             uint32_t        block)
{
    BlockMining<<<grid, block>>>(
        d_bound, d_data, base,
        d_hashes, d_results, d_valid, d_count, d_bhashes);
}

void al2_reduce_sum(uint32_t *d_data, uint32_t *d_aux,
                    uint32_t inlen, uint32_t *h_result)
{
    *h_result = FindSum(d_data, d_aux, inlen);
}
