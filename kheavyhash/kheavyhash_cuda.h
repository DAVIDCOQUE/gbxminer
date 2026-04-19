#pragma once
/**
 * kheavyhash_cuda.h
 *
 * Plain-C CUDA launch wrapper for the kHeavyHash GPU kernel.
 * Safe to include from .cpp files compiled by g++.
 *
 * kHeavyHash algorithm (Kaspa)
 * ----------------------------
 * 1. pre_hash = keccak256(header[0:72])
 * 2. Derive 64x64 matrix M from pre_hash using xoshiro256** PRNG
 * 3. product  = M * pre_hash (mod 2^17), element-wise
 * 4. heavyhash = keccak256(pre_hash XOR product)
 * 5. Solution iff heavyhash < target
 *
 * The matrix is re-derived per block (cheap — no DAG, no epoch).
 * Nonce occupies bytes 32..39 of the 72-byte header.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * kheavyhash_cuda_init - allocate per-GPU result buffer.
 * Must be called once before the first kheavyhash_run() call.
 */
void kheavyhash_cuda_init(int thr_id);

/**
 * kheavyhash_run - launch the search kernel.
 *
 * @param thr_id        GPU thread index.
 * @param header        72-byte block header (host pointer); nonce at [32..39].
 * @param target        64-bit compact difficulty target (little-endian).
 * @param start_nonce   First nonce to test (lower 32 bits).
 * @param nonces_done   Number of nonces tested by this launch.
 * @param found_nonce   Set to winning nonce if return value is 1.
 *
 * Returns 1 if a solution was found, 0 otherwise.
 */
int kheavyhash_run(int thr_id,
                   const uint32_t *header,   /* 18 uint32 = 72 bytes */
                   uint64_t        target,
                   uint32_t        start_nonce,
                   uint32_t        nonces_done,
                   uint32_t       *found_nonce);

/**
 * kheavyhash_cuda_free - release device memory for this thread.
 */
void kheavyhash_cuda_free(int thr_id);

#ifdef __cplusplus
}
#endif
