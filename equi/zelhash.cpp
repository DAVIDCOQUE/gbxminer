/**
 * zelhash.cpp
 *
 * ZelHash (Flux) scanhash integration for gbxminer.
 *
 * ZelHash is the proof-of-work algorithm used by Flux (formerly ZelCash).
 * It is Equihash with parameters N=125, K=4.
 *
 * Key differences from standard Equihash(200,9):
 *   - Solution size: 52 bytes  (vs 1344 for 200,9)
 *   - Proof size:    16 indices (vs 512)
 *   - Block header:  140 bytes (same)
 *   - Personalisation: "ZcashPoW" with N=125, K=4 appended as LE uint32
 *     (identical mechanism to 200,9 — the kernel uses WN/WK from the
 *      CONFIG_MODE_4 template instantiation)
 *
 * The solver kernel (cuda_equi.cu) is fully templated.  This file uses
 * the CONFIG_MODE_4 instantiation (RB=5, SM=10, SSM=6, THREADS=512,
 * packer_default) which is appropriate for 125,4.
 *
 * Architecture note
 * -----------------
 * This file is intentionally separate from equihash.cpp.  The two cannot
 * share a single scanhash function because:
 *   1. Solution sizes differ (52 vs 1344 bytes)
 *   2. Static per-GPU solver pointers differ (separate context type)
 *   3. Submission buffer and verify call differ
 *
 * The stratum protocol (equi/equi-stratum.cpp) is shared — ZelHash uses
 * the same equi_stratum_notify / equi_stratum_submit flow.  The only
 * stratum difference is the personalization embedded in the block header,
 * which is handled by the solver kernel itself.
 *
 * Cannibalised from: equihash.cpp (same repo, GPL v3).
 * ZelHash specification: https://zelcash.github.io/zelcash/
 */

#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <assert.h>

/* htobe32 is a Linux/glibc/BSD extension; provide a portable fallback
 * for MinGW/MSVC where it is absent. */
#if defined(_WIN32) || !defined(htobe32)
# ifndef htobe32
#  if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define htobe32(x) (x)
#  else
#   include <stdlib.h>
static inline uint32_t htobe32(uint32_t x) {
    return ((x & 0xFFU) << 24) | ((x & 0xFF00U) << 8)
         | ((x >> 8) & 0xFF00U) | ((x >> 24) & 0xFFU);
}
#  endif
# endif
#endif

#include <stdexcept>
#include <vector>

#include <sph/sph_sha2.h>

#include "equi/eqcuda.hpp"
#include "equi/equihash.h"   /* ZEL_SOLSIZE, ZEL_PROOFSIZE, zel_verify() */

#include <miner.h>

/* ------------------------------------------------------------------ */
/*  SHA256d over the full ZelHash block (header + sol_size + solution)  */
/* ------------------------------------------------------------------ */

extern "C" void zel_hash(const void* input, void* output, int len)
{
    uint8_t _ALIGN(64) hash0[32], hash1[32];
    sph_sha256_context ctx;
    sph_sha256_init(&ctx);
    sph_sha256(&ctx, input, len);
    sph_sha256_close(&ctx, hash0);
    sph_sha256(&ctx, hash0, 32);
    sph_sha256_close(&ctx, hash1);
    memcpy(output, hash1, 32);
}

/* ------------------------------------------------------------------ */
/*  Solution verifier                                                   */
/* ------------------------------------------------------------------ */

extern "C" int zel_verify_sol(void * const hdr, void * const sol)
{
    return zel_verify((uint8_t*)hdr, (uint8_t*)sol) ? 1 : 0;
}

#include <cuda_helper.h>

#define NONCE_OFT EQNONCE_OFFSET

/* Per-GPU state — separate from equihash.cpp's statics */
static bool zel_init[MAX_GPUS]                     = { 0 };
static int  zel_valid_sols[MAX_GPUS]               = { 0 };
/* 140 (header) + 3 (varint) + 52 (solution) = 195 bytes per solution */
static uint8_t _ALIGN(64) zel_data_sols[MAX_GPUS][MAXREALSOLS][256] = { 0 };
static eq_cuda_context_interface* zel_solvers[MAX_GPUS]             = { NULL };

/* ------------------------------------------------------------------ */
/*  Solver callbacks                                                    */
/* ------------------------------------------------------------------ */

static void zel_cb_solution(int thr_id,
                             const std::vector<uint32_t>& solutions,
                             size_t cbitlen,
                             const unsigned char *compressed_sol)
{
    /* For Equihash(125,4), cbitlen = N/(K+1) = 25; solution = 52 bytes */
    std::vector<unsigned char> nSol;

    if (compressed_sol) {
        nSol.assign(compressed_sol, compressed_sol + ZEL_SOLSIZE);
    } else {
        /*
         * Build compressed solution from indices.
         * Each index is (cbitlen+1) = 26 bits.  Compact 16 indices into
         * ceil(16 * 26 / 8) = 52 bytes.
         */
        size_t lenIndices  = solutions.size() * sizeof(uint32_t);
        size_t bytePad     = sizeof(uint32_t) - ((cbitlen + 1) + 7) / 8;
        size_t minLen      = (cbitlen + 1) * lenIndices / (8 * sizeof(uint32_t));

        std::vector<unsigned char> expanded(lenIndices);
        for (size_t i = 0; i < solutions.size(); i++) {
            uint32_t be = htobe32(solutions[i]);
            memcpy(expanded.data() + i * sizeof(uint32_t), &be, sizeof(uint32_t));
        }

        nSol.resize(minLen, 0);
        /* CompressArray equivalent inline */
        uint32_t bit_len      = (uint32_t)(cbitlen + 1);
        uint32_t bit_len_mask = (1UL << bit_len) - 1;
        size_t   in_width     = (bit_len + 7) / 8 + bytePad;
        size_t   acc_bits     = 0;
        uint32_t acc_value    = 0;
        size_t   j            = 0;
        for (size_t i = 0; i < minLen; i++) {
            if (acc_bits < 8) {
                acc_value <<= bit_len;
                for (size_t x = bytePad; x < in_width; x++) {
                    acc_value |= ((expanded[j + x] &
                                  ((bit_len_mask >> (8 * (in_width - x - 1))) & 0xFF))
                                  << (8 * (in_width - x - 1)));
                }
                j += in_width;
                acc_bits += bit_len;
            }
            acc_bits -= 8;
            nSol[i] = (acc_value >> acc_bits) & 0xFF;
        }
    }

    int nsol = zel_valid_sols[thr_id];
    if (nsol < 0) nsol = 0;
    if ((int)nSol.size() == ZEL_SOLSIZE) {
        /*
         * Varint encoding for solution length:
         *   ZEL_SOLSIZE = 52 (0x34) → 1-byte varint 0x34
         */
        zel_data_sols[thr_id][nsol][140] = (uint8_t)ZEL_SOLSIZE;
        zel_data_sols[thr_id][nsol][141] = 0x00;
        zel_data_sols[thr_id][nsol][142] = 0x00;
        memcpy(&zel_data_sols[thr_id][nsol][143], nSol.data(), ZEL_SOLSIZE);
        zel_valid_sols[thr_id] = nsol + 1;
    }
}

static void zel_cb_hashdone(int thr_id)
{
    if (!zel_valid_sols[thr_id]) zel_valid_sols[thr_id] = -1;
}

static bool zel_cb_cancel(int thr_id)
{
    if (work_restart[thr_id].restart)
        zel_valid_sols[thr_id] = -1;
    return work_restart[thr_id].restart;
}

/* ------------------------------------------------------------------ */
/*  scanhash_zelhash                                                    */
/* ------------------------------------------------------------------ */

extern "C" int scanhash_zelhash(int thr_id, struct work *work,
                                 uint32_t max_nonce,
                                 unsigned long *hashes_done)
{
    uint32_t _ALIGN(64) endiandata[35];
    uint32_t *pdata   = work->data;
    uint32_t *ptarget = work->target;
    const uint32_t first_nonce = pdata[NONCE_OFT];
    uint32_t nonce_increment   = rand() & 0xFF;
    struct timeval tv_start, tv_end, diff;
    double secs, solps;
    uint32_t soluce_count = 0;

    if (opt_benchmark)
        ptarget[7] = 0xfffff;

    if (!zel_init[thr_id]) {
        try {
            /* CONFIG_MODE_4 = 5, 10, 6, 512, packer_default (ZelHash 125,4) */
            zel_solvers[thr_id] = new eq_cuda_context<CONFIG_MODE_4>(
                thr_id, device_map[thr_id]);
            size_t memSz = zel_solvers[thr_id]->equi_mem_sz / (1024*1024);
            api_set_throughput(thr_id, (uint32_t)zel_solvers[thr_id]->throughput);
            gpulog(LOG_DEBUG, thr_id,
                   "ZelHash: allocated %u MB of solver context", (u32)memSz);
            cuda_get_arch(thr_id);
            zel_init[thr_id] = true;
        } catch (const std::exception &e) {
            CUDA_LOG_ERROR();
            gpulog(LOG_ERR, thr_id, "ZelHash init: %s", e.what());
            proper_exit(EXIT_CODE_CUDA_ERROR);
        }
    }

    gettimeofday(&tv_start, NULL);
    memcpy(endiandata, pdata, 140);
    work->valid_nonces = 0;

    do {
        try {
            zel_valid_sols[thr_id] = 0;
            zel_solvers[thr_id]->solve(
                (const char *)endiandata,    (unsigned int)(140 - 32),
                (const char *)&endiandata[27], (unsigned int)32,
                &zel_cb_cancel, &zel_cb_solution, &zel_cb_hashdone);
            *hashes_done = soluce_count;
        } catch (const std::exception &e) {
            gpulog(LOG_WARNING, thr_id, "ZelHash solver: %s", e.what());
            free_zelhash(thr_id);
            sleep(1);
            return -1;
        }

        if (zel_valid_sols[thr_id] > 0) {
            const uint32_t Htarg = ptarget[7];
            uint32_t _ALIGN(64) vhash[8];
            uint8_t _ALIGN(64) full_data[ZEL_FULLSIZE] = { 0 };
            uint8_t *sol_data = &full_data[140];

            soluce_count += zel_valid_sols[thr_id];

            for (int nsol = 0; nsol < zel_valid_sols[thr_id]; nsol++) {
                memcpy(full_data, endiandata, 140);
                memcpy(sol_data, &zel_data_sols[thr_id][nsol][140],
                       3 + ZEL_SOLSIZE);
                zel_hash(full_data, vhash, ZEL_FULLSIZE);

                if (vhash[7] <= Htarg && fulltest(vhash, ptarget)) {
                    bool valid = zel_verify_sol(endiandata, &sol_data[3]);
                    if (valid && work->valid_nonces < MAX_NONCES) {
                        work->valid_nonces++;
                        memcpy(work->data, endiandata, 140);
                        /* reuse equi_store_work_solution — same struct layout */
                        equi_store_work_solution(work, vhash, sol_data);
                        work->nonces[work->valid_nonces - 1] = endiandata[NONCE_OFT];
                        pdata[NONCE_OFT] = endiandata[NONCE_OFT] + 1;
                        goto out;
                    }
                }
                if (work->valid_nonces == MAX_NONCES) goto out;
            }
            if (work->valid_nonces) goto out;
            zel_valid_sols[thr_id] = 0;
        }

        endiandata[NONCE_OFT] += nonce_increment;

    } while (!work_restart[thr_id].restart);

out:
    gettimeofday(&tv_end, NULL);
    timeval_subtract(&diff, &tv_end, &tv_start);
    secs  = (1.0 * diff.tv_sec) + (0.000001 * diff.tv_usec);
    solps = (double)soluce_count / secs;
    gpulog(LOG_DEBUG, thr_id, "ZelHash: %d solutions in %.2f s (%.2f Sol/s)",
           soluce_count, secs, solps);

    *hashes_done      = soluce_count;
    pdata[NONCE_OFT]  = endiandata[NONCE_OFT] + 1;
    return work->valid_nonces;
}

/* ------------------------------------------------------------------ */
/*  free_zelhash                                                        */
/* ------------------------------------------------------------------ */

extern "C" void free_zelhash(int thr_id)
{
    if (!zel_init[thr_id]) return;

    eq_cuda_context<CONFIG_MODE_4> *ptr =
        dynamic_cast<eq_cuda_context<CONFIG_MODE_4>*>(zel_solvers[thr_id]);
    if (ptr) ptr->freemem();
    zel_solvers[thr_id] = NULL;
    zel_init[thr_id]    = false;
}
