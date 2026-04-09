#pragma once
/**
 * etchash_dagger.cuh
 *
 * ETCHash DAG-traversal device function ("dagger" step).
 *
 * compute_hash() implements the Ethash/ETCHash proof-of-work inner loop:
 *   1. keccak_f1600_init  — seed mix from header + nonce
 *   2. 64 DAG accesses (parallel hash, 4 at a time)
 *   3. keccak_f1600_final — final mix → candidate hash
 *
 * Returns true when the candidate hash does NOT meet the target
 * (i.e. the nonce is not a solution).  Returning true allows the
 * caller to discard early without writing results.
 *
 * Adapted from etcminer dagger_shuffled.cuh (GPL-3.0).
 * Changes vs upstream:
 *   - SHFL → ETC_SHFL
 *   - d_dag/d_dag_size/d_target → d_etc_* namespace
 *   - cuda_swab64 defined locally in etchash_keccak.cuh
 */

#include "etchash_cuda_miner_kernel_globals.h"
#include "etchash_cuda_miner_kernel.h"
#include "etchash_fnv.cuh"
#include "etchash_keccak.cuh"

/*
 * _PARALLEL_HASH: number of nonces processed in parallel within a single
 * warp group.  4 is the sweet-spot on Maxwell–Ada (matches _PARALLEL_HASH
 * in the upstream etcminer code).
 */
#define _ETC_PARALLEL_HASH 4

DEV_INLINE bool compute_hash(uint64_t nonce, uint2* mix_hash)
{
    uint2 state[12];

    /* Inject nonce into lane 4 (keccak word 4 = bytes 32-39). */
    state[4] = vectorize(nonce);

    /* Run the first 23.5 Keccak rounds over (header ‖ nonce). */
    keccak_f1600_init(state);

    /*
     * Threads collaborate in groups of ETCHASH_THREADS_PER_HASH (8).
     * mix_idx selects which 128-bit slice each thread works on.
     */
    const int thread_id = threadIdx.x & (ETCHASH_THREADS_PER_HASH - 1);
    const int mix_idx   = thread_id & 3;

    for (int i = 0; i < ETCHASH_THREADS_PER_HASH; i += _ETC_PARALLEL_HASH)
    {
        uint4    mix[_ETC_PARALLEL_HASH];
        uint32_t offset[_ETC_PARALLEL_HASH];
        uint32_t init0[_ETC_PARALLEL_HASH];

        /* Share keccak state across the warp via shuffle. */
        for (int p = 0; p < _ETC_PARALLEL_HASH; p++)
        {
            uint2 shuf[8];
            for (int j = 0; j < 8; j++)
            {
                shuf[j].x = ETC_SHFL(state[j].x, i + p, ETCHASH_THREADS_PER_HASH);
                shuf[j].y = ETC_SHFL(state[j].y, i + p, ETCHASH_THREADS_PER_HASH);
            }
            switch (mix_idx)
            {
            case 0: mix[p].x = shuf[0].x; mix[p].y = shuf[0].y;
                    mix[p].z = shuf[1].x; mix[p].w = shuf[1].y; break;
            case 1: mix[p].x = shuf[2].x; mix[p].y = shuf[2].y;
                    mix[p].z = shuf[3].x; mix[p].w = shuf[3].y; break;
            case 2: mix[p].x = shuf[4].x; mix[p].y = shuf[4].y;
                    mix[p].z = shuf[5].x; mix[p].w = shuf[5].y; break;
            case 3: mix[p].x = shuf[6].x; mix[p].y = shuf[6].y;
                    mix[p].z = shuf[7].x; mix[p].w = shuf[7].y; break;
            }
            init0[p] = ETC_SHFL(shuf[0].x, 0, ETCHASH_THREADS_PER_HASH);
        }

        /* 64 DAG accesses (ACCESSES = 64), processed in groups of 4. */
        for (uint32_t a = 0; a < ETCHASH_ACCESSES; a += 4)
        {
            /* t selects which lane broadcasts the offset. */
            int t = (int)((a >> 2) & 7); /* bfe(a, 2, 3) */

            for (uint32_t b = 0; b < 4; b++)
            {
                for (int p = 0; p < _ETC_PARALLEL_HASH; p++)
                {
                    offset[p] = etchash_fnv(init0[p] ^ (a + b),
                                            ((uint32_t*)&mix[p])[b])
                                % d_etc_dag_size;
                    offset[p] = ETC_SHFL(offset[p], t, ETCHASH_THREADS_PER_HASH);
                    mix[p]    = etchash_fnv4(mix[p],
                                    d_etc_dag[offset[p]].uint4s[thread_id]);
                }
            }
        }

        /* Reduce each parallel mix to a single 32-bit word, then
         * broadcast across the warp to rebuild the 256-bit mix hash. */
        for (int p = 0; p < _ETC_PARALLEL_HASH; p++)
        {
            uint2    shuf[4];
            uint32_t thread_mix = etchash_fnv_reduce(mix[p]);

            shuf[0].x = ETC_SHFL(thread_mix, 0, ETCHASH_THREADS_PER_HASH);
            shuf[0].y = ETC_SHFL(thread_mix, 1, ETCHASH_THREADS_PER_HASH);
            shuf[1].x = ETC_SHFL(thread_mix, 2, ETCHASH_THREADS_PER_HASH);
            shuf[1].y = ETC_SHFL(thread_mix, 3, ETCHASH_THREADS_PER_HASH);
            shuf[2].x = ETC_SHFL(thread_mix, 4, ETCHASH_THREADS_PER_HASH);
            shuf[2].y = ETC_SHFL(thread_mix, 5, ETCHASH_THREADS_PER_HASH);
            shuf[3].x = ETC_SHFL(thread_mix, 6, ETCHASH_THREADS_PER_HASH);
            shuf[3].y = ETC_SHFL(thread_mix, 7, ETCHASH_THREADS_PER_HASH);

            if ((i + p) == thread_id)
            {
                state[8]  = shuf[0];
                state[9]  = shuf[1];
                state[10] = shuf[2];
                state[11] = shuf[3];
            }
        }
    }

    /*
     * Final keccak: keccak_256(keccak_512(header ‖ nonce) ‖ mix_hash).
     * cuda_swab64 converts the little-endian CUDA result to the
     * big-endian comparison required by the target.
     */
    if (cuda_swab64(keccak_f1600_final(state)) > d_etc_target)
        return true;    /* not a solution */

    mix_hash[0] = state[8];
    mix_hash[1] = state[9];
    mix_hash[2] = state[10];
    mix_hash[3] = state[11];

    return false;       /* solution found */
}

#undef _ETC_PARALLEL_HASH
