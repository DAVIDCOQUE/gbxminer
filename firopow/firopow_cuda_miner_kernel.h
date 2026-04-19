#pragma once
/**
 * firopow_cuda_miner_kernel.h
 *
 * FiroPow (Firo ProgPoW) GPU kernel interface for gbxminer.
 *
 * FiroPow is Firo's PoW algorithm.  It is ProgPoW with the following
 * consensus-critical parameters (distinct from ETH ProgPoW):
 *
 *   PROGPOW_PERIOD        3          (program changes every 3 blocks)
 *   PROGPOW_LANES         16
 *   PROGPOW_REGS          32
 *   PROGPOW_DAG_LOADS     4
 *   PROGPOW_CACHE_BYTES   16 384     (16 KiB)
 *   PROGPOW_CNT_DAG       64
 *   PROGPOW_CNT_CACHE     11
 *   PROGPOW_CNT_MATH      18
 *   EPOCH_LENGTH          7 500      (Firo-specific)
 *
 * The random inner-loop program is regenerated from the block number's
 * period seed via ProgPow::getKern() (ProgPow.h / ProgPow.cpp).  The
 * generated source is compiled at runtime via NVRTC (NVIDIA Runtime
 * Compilation) and the resulting kernel is launched per-period.
 *
 * NVRTC dependency
 * ----------------
 * Runtime compilation requires libnvrtc.so (Linux) / nvrtc64_*.dll (Win).
 * The Makefile.am HAVE_NVRTC guard controls this path; without NVRTC the
 * build still succeeds but FiroPow is unavailable at runtime.
 */

#include <stdint.h>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cuda_runtime.h"

/* Re-use ETCHash type definitions for the shared DAG layout. */
#include "etchash/etchash_cuda_miner_kernel.h"

/* ------------------------------------------------------------------ */
/*  FiroPow-specific constants                                            */
/* ------------------------------------------------------------------ */

#define FIROPOW_EPOCH_LENGTH    7500U
#define FIROPOW_PERIOD          3U

#define FIROPOW_MAX_RESULTS     4U
#define FIROPOW_THREADS_PER_HASH (128 / 16)   /* = 8 */

/* ------------------------------------------------------------------ */
/*  Result types                                                        */
/* ------------------------------------------------------------------ */

struct KawpowSearch_Result
{
    uint32_t gid;
    uint32_t mix[8];
    uint32_t pad[7];
};

struct KawpowSearch_results
{
    KawpowSearch_Result result[FIROPOW_MAX_RESULTS];
    uint32_t count;
};

/* ------------------------------------------------------------------ */
/*  Host API — only meaningful when NVRTC is present.                  */
/*  CUmodule is a CUDA Driver API type; guard its use so firopow.cpp     */
/*  can be compiled (as a stub) without NVRTC.                         */
/* ------------------------------------------------------------------ */

#ifdef HAVE_NVRTC
/* ------------------------------------------------------------------ */
/*  Host API                                                            */
/* ------------------------------------------------------------------ */

/**
 * firopow_run_search - launch the ProgPoW search kernel.
 *
 * @param module      CUDA module handle returned by firopow_compile_kernel().
 * @param grid_size   CUDA grid dimension.
 * @param block_size  CUDA block dimension (multiple of PROGPOW_LANES * 4).
 * @param stream      CUDA stream.
 * @param g_output    Device result buffer.
 * @param start_nonce Starting nonce.
 * @param dag         Device DAG pointer (etc_hash128_t layout).
 * @param dag_size    Number of 128-byte DAG items.
 * @param header      32-byte block header hash.
 * @param target      64-bit difficulty target.
 */
void firopow_run_search(CUmodule module, uint32_t grid_size, uint32_t block_size,
                      cudaStream_t stream,
                      volatile KawpowSearch_results* g_output,
                      uint64_t start_nonce,
                      etc_hash128_t* dag, uint32_t dag_size,
                      const uint32_t header[8], uint64_t target);

/**
 * firopow_compile_kernel - compile the ProgPoW inner loop for a given period.
 *
 * Uses NVRTC to JIT-compile the period-specific inner-loop source generated
 * by ProgPow::getKern().  The compiled module is cached per period.
 *
 * @param period      Block number / FIROPOW_PERIOD.
 * @param mod_out     Receives the compiled CUmodule.
 *
 * Returns true on success.
 */
bool firopow_compile_kernel(uint64_t period, CUmodule* mod_out);

/** Free all cached compiled modules and device memory for this thread. */
void firopow_free_modules(void);

/* ------------------------------------------------------------------ */
/*  Error helpers                                                       */
/* ------------------------------------------------------------------ */

struct firopow_cuda_error : public virtual std::runtime_error
{
    explicit firopow_cuda_error(const std::string& msg) : std::runtime_error(msg) {}
};

#define FIROPOW_CUDA_SAFE_CALL(call)                                               \
    do {                                                                          \
        cudaError_t _e = (call);                                                  \
        if (cudaSuccess != _e) {                                                  \
            std::stringstream _ss;                                                \
            _ss << "CUDA error in " << __FUNCTION__                               \
                << " at line " << __LINE__ << ": " << cudaGetErrorString(_e);     \
            throw firopow_cuda_error(_ss.str());                                    \
        }                                                                         \
    } while (0)

#define FIROPOW_CU_SAFE_CALL(call)                                                 \
    do {                                                                          \
        CUresult _r = (call);                                                     \
        if (CUDA_SUCCESS != _r) {                                                 \
            const char* _msg = nullptr;                                           \
            cuGetErrorString(_r, &_msg);                                          \
            std::stringstream _ss;                                                \
            _ss << "CU error in " << __FUNCTION__                                 \
                << " at line " << __LINE__ << ": "                                \
                << (_msg ? _msg : "unknown");                                     \
            throw firopow_cuda_error(_ss.str());                                    \
        }                                                                         \
    } while (0)

#endif /* HAVE_NVRTC */
