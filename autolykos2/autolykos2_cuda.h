#pragma once
/**
 * autolykos2_cuda.h
 *
 * Plain-C declarations of the CUDA kernel launch wrappers defined in
 * autolykos2_cuda.cu.  Including this header from a .cpp file is safe:
 * no triple-chevron <<<>>> syntax, no __global__ annotations.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Launch InitPrehash kernel: build N_LEN BLAKE2b-256 hashes. */
void al2_launch_init_prehash(uint32_t *d_data,
                             uint32_t *d_hashes,
                             uint32_t *d_invalid,
                             uint32_t  grid,
                             uint32_t  block);

/** Launch UpdatePrehash kernel: rehash out-of-range entries. */
void al2_launch_update_prehash(uint32_t *d_hashes,
                               uint32_t *d_compact_inv,
                               uint32_t  inv_count,
                               uint32_t  block);

/** Launch FinalPrehash kernel: reduce all table entries mod Q. */
void al2_launch_final_prehash(uint32_t *d_hashes,
                              uint32_t  grid,
                              uint32_t  block);

/** Launch Compactify kernel: pack non-zero entries of d_in into d_out. */
void al2_launch_compactify(const uint32_t *d_in,
                           uint32_t        inlen,
                           uint32_t       *d_out,
                           uint32_t       *d_outlen,
                           uint32_t        block);

/** Launch BlakeHash kernel: compute per-nonce preliminary BLAKE2b hashes. */
void al2_launch_blake_hash(const uint32_t *d_data,
                           uint64_t        base,
                           uint32_t       *d_bhashes,
                           uint32_t        grid,
                           uint32_t        block);

/** Launch BlockMining kernel: check k-sum condition. */
void al2_launch_block_mining(const uint32_t *d_bound,
                             const uint32_t *d_data,
                             uint64_t        base,
                             const uint32_t *d_hashes,
                             uint32_t       *d_results,
                             uint32_t       *d_valid,
                             uint32_t       *d_count,
                             uint32_t       *d_bhashes,
                             uint32_t        grid,
                             uint32_t        block);

/**
 * Sum all elements of d_data[0..inlen-1] using d_aux as scratch.
 * Result written to *h_result (host pointer).
 */
void al2_reduce_sum(uint32_t *d_data, uint32_t *d_aux,
                    uint32_t inlen, uint32_t *h_result);

#ifdef __cplusplus
}
#endif
