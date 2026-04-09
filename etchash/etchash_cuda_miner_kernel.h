#pragma once
/**
 * etchash_cuda_miner_kernel.h
 *
 * ETCHash (ECIP-1099) GPU kernel interface for gbxminer.
 *
 * ETCHash is the Ethereum Classic proof-of-work algorithm defined by ECIP-1099.
 * It is structurally identical to Ethash (Keccak-based DAG PoW) with one key
 * difference: the epoch length is doubled from 30,000 to 60,000 blocks, halving
 * the rate at which the DAG grows.  This keeps the DAG accessible to GPU miners
 * with smaller VRAM for longer.
 *
 * Epoch formula:  epoch = block_number / ETCHASH_EPOCH_LENGTH  (60000)
 *
 * Adapted from etcminer <https://github.com/nicehash/etcminer>
 * Original authors: Genoil, nicehash team; licence: GPL-3.0.
 * Adaptation: stripped CMake/Farm framework; wired to gbxminer work/stratum model.
 */

#include <stdint.h>
#include <sstream>
#include <stdexcept>
#include <string>

#include "cuda_runtime.h"

/* ETCHash epoch: 60 000 blocks per epoch (ECIP-1099). */
#define ETCHASH_EPOCH_LENGTH    60000U

/*
 * Maximum solutions returned per kernel launch.  A power-of-two gives better
 * CUDA occupancy.  In practice etchash yields at most one solution per
 * gridSize nonces, but we leave headroom for large grids.
 */
#define ETCHASH_MAX_RESULTS     4U

/* DAG memory access pattern constants (identical to Ethash). */
#define ETCHASH_ACCESSES        64
#define ETCHASH_THREADS_PER_HASH (128 / 16)

/* ------------------------------------------------------------------ */
/*  Shared data types                                                   */
/* ------------------------------------------------------------------ */

struct EtcSearch_Result
{
    uint32_t gid;       /* global thread id that found the nonce */
    uint32_t mix[8];    /* mix-hash (32 bytes)                   */
    uint32_t pad[7];    /* pad to power-of-two size              */
};

struct EtcSearch_results
{
    EtcSearch_Result result[ETCHASH_MAX_RESULTS];
    uint32_t count;     /* atomic counter; reset to 0 before each launch */
};

typedef struct
{
    uint4 uint4s[32 / sizeof(uint4)];
} etc_hash32_t;

typedef union
{
    uint32_t words[128 / sizeof(uint32_t)];
    uint2    uint2s[128 / sizeof(uint2)];
    uint4    uint4s[128 / sizeof(uint4)];
} etc_hash128_t;

typedef union
{
    uint32_t words[64 / sizeof(uint32_t)];
    uint2    uint2s[64 / sizeof(uint2)];
    uint4    uint4s[64 / sizeof(uint4)];
} etc_hash64_t;

/* ------------------------------------------------------------------ */
/*  Host-side API                                                       */
/* ------------------------------------------------------------------ */

/**
 * Upload DAG and light-cache device pointers to CUDA constant memory.
 * Must be called after allocating and populating device buffers.
 */
void etchash_set_constants(etc_hash128_t* dag,  uint32_t dag_size,
                           etc_hash64_t*  light, uint32_t light_size);

/** Query back the device pointers stored in constant memory. */
void etchash_get_constants(etc_hash128_t** dag,  uint32_t* dag_size,
                           etc_hash64_t**  light, uint32_t* light_size);

/** Upload 32-byte block header to constant memory. */
void etchash_set_header(etc_hash32_t header);

/** Upload 64-bit difficulty target to constant memory. */
void etchash_set_target(uint64_t target);

/**
 * Launch the search kernel.
 *
 * @param grid_size   Number of CUDA blocks.
 * @param block_size  Threads per block (must be multiple of ETCHASH_THREADS_PER_HASH * 4).
 * @param stream      CUDA stream to launch on (NULL = default stream).
 * @param g_output    Device-side result buffer (pre-zeroed count field).
 * @param start_nonce First nonce to test.
 */
void etchash_run_search(uint32_t grid_size, uint32_t block_size, cudaStream_t stream,
                        volatile EtcSearch_results* g_output, uint64_t start_nonce);

/**
 * Generate the DAG on-device.
 *
 * @param dag_size  DAG byte size.
 * @param blocks    Grid dimension for generation kernel.
 * @param threads   Block dimension for generation kernel.
 * @param stream    CUDA stream.
 */
void etchash_generate_dag(uint64_t dag_size, uint32_t blocks, uint32_t threads,
                          cudaStream_t stream);

/* ------------------------------------------------------------------ */
/*  Error helpers                                                       */
/* ------------------------------------------------------------------ */

struct etchash_cuda_error : public virtual std::runtime_error
{
    explicit etchash_cuda_error(const std::string& msg) : std::runtime_error(msg) {}
};

#define ETCHASH_CUDA_SAFE_CALL(call)                                                \
    do {                                                                             \
        cudaError_t _err = (call);                                                   \
        if (cudaSuccess != _err) {                                                   \
            std::stringstream _ss;                                                   \
            _ss << "CUDA error in " << __FUNCTION__                                  \
                << " at line " << __LINE__ << ": "                                   \
                << cudaGetErrorString(_err);                                         \
            throw etchash_cuda_error(_ss.str());                                     \
        }                                                                            \
    } while (0)
