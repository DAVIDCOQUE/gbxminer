/**
 * kawpow_cuda_miner_kernel.cpp
 *
 * KawPow (Ravencoin ProgPoW) kernel management using NVRTC.
 *
 * Design
 * ------
 * ProgPoW generates a unique inner-loop program for each period
 * (block_number / PROGPOW_PERIOD).  This file:
 *   1. Uses ProgPow::getKern() to produce the CUDA C source for that period.
 *   2. Compiles it at runtime via NVRTC into a PTX string.
 *   3. Loads the PTX via the CUDA Driver API into a CUmodule.
 *   4. Caches the module; only recompiles when the period changes.
 *   5. Launches the search kernel via cuLaunchKernel with inline DAG/header/
 *      target parameters (no __constant__ memory — parameters are passed
 *      as kernel arguments to allow per-launch variation).
 *
 * Fallback
 * --------
 * If NVRTC is not available at link time (HAVE_NVRTC is not defined in
 * gbxminer-config.h) all functions return immediately with an error log.
 * The build still succeeds; KawPow simply cannot be used.
 */

/* gbxminer-config.h MUST come first — it defines HAVE_NVRTC which gates
 * the macro and type definitions in kawpow_cuda_miner_kernel.h.        */
#include <gbxminer-config.h>

/* C++ STL headers MUST precede miner.h (which defines min/max macros
 * that break C++ template parsing). ProgPow.h uses std::string and
 * std::stringstream extensively.                                   */
#include "kawpow/kawpow_cuda_miner_kernel.h"
#include "kawpow/ProgPow.h"

#include <map>
#include <string>

#include "miner.h"

/* ------------------------------------------------------------------ */
/*  NVRTC availability guard                                            */
/* ------------------------------------------------------------------ */
#ifdef HAVE_NVRTC
#  include <cuda.h>
#  include <nvrtc.h>

#  define NVRTC_SAFE_CALL(call)                                             \
    do {                                                                     \
        nvrtcResult _r = (call);                                             \
        if (NVRTC_SUCCESS != _r) {                                           \
            applog(LOG_ERR, "KawPow NVRTC error: %s", nvrtcGetErrorString(_r)); \
            return false;                                                    \
        }                                                                    \
    } while (0)

#endif /* HAVE_NVRTC */

/* ------------------------------------------------------------------ */
/*  Module cache                                                        */
/* ------------------------------------------------------------------ */

static std::map<uint64_t, CUmodule> s_module_cache;

/* ------------------------------------------------------------------ */
/*  Kernel source template                                              */
/*                                                                      */
/*  The generated progPowLoop() is embedded inside a fixed outer        */
/*  kernel that mirrors the ProgPoW search structure.  Header hash,     */
/*  DAG pointer, DAG size, and target are passed as kernel parameters   */
/*  rather than __constant__ memory, allowing reuse of the same         */
/*  compiled module across multiple launches.                           */
/* ------------------------------------------------------------------ */

static std::string kawpow_build_kernel_source(uint64_t period)
{
    std::string inner = ProgPow::getKern(period, ProgPow::KERNEL_CUDA);

    /* Outer wrapper: FNV constants, mix initialisation, DAG gather,
     * and final Keccak reduction.  This mirrors kawpowminer's search.cu
     * structure but is integrated with gbxminer's result type. */
    std::string src;
    src += inner;
    src += R"(
#define KAWPOW_MAX_RESULTS 4U
#define KAWPOW_LANES       )" + std::to_string(PROGPOW_LANES) + R"(

struct KawpowResult { unsigned gid; unsigned mix[8]; unsigned pad[7]; };
struct KawpowResults { KawpowResult result[KAWPOW_MAX_RESULTS]; unsigned count; };

// FNV1a
__device__ __forceinline__ unsigned fnv1a(unsigned h, unsigned d)
{ return (h ^ d) * 0x1000193u; }

// keccak_f800 (ProgPoW uses Keccak-f[800], not f[1600])
__device__ unsigned keccak_f800_progpow(
    uint32_t* state, uint64_t seed, const uint32_t* mix_lane)
{
    // Simplified: absorb header+seed, permute, squeeze
    // Full implementation in kawpowminer; abbreviated here.
    // TODO: embed full keccak_f800 rounds.
    (void)state; (void)seed; (void)mix_lane;
    return 0;
}

/* __launch_bounds__(max_threads_per_block, min_blocks_per_sm)
 *
 * max_threads_per_block = KAWPOW_LANES * 4 = 64.
 *   This is the fixed block size used by kawpow.cpp; making it explicit
 *   here lets NVRTC's register allocator know the block is small and can
 *   afford more registers per thread than it would assume conservatively.
 *
 * min_blocks_per_sm = 2.
 *   Secondary guidance: aim for at least 2 blocks in flight per SM to
 *   give the hardware scheduler enough warps to hide memory latency.
 *   ProgPoW's PROGPOW_REGS = 32 mix registers means register pressure is
 *   already moderate; 2 blocks is achievable without spilling on sm_61+.
 *
 * Without --gpu-architecture passed to NVRTC (see kawpow_compile_kernel),
 * NVRTC would evaluate this hint against an old baseline register file.
 * The arch flag and __launch_bounds__ are designed to work together.    */
extern "C" __global__ __launch_bounds__(KAWPOW_LANES * 4, 2) void kawpow_search(
    const dag_t* __restrict__ g_dag,
    const uint32_t            dag_size,
    const uint32_t            header[8],
    const uint64_t            target,
    const uint64_t            start_nonce,
    volatile KawpowResults*    g_output)
{
    const uint32_t gid = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t lane_id = threadIdx.x & (KAWPOW_LANES - 1);
    const uint64_t nonce = start_nonce + gid;

    // Build c_dag from L1 cache
    __shared__ uint32_t c_dag[PROGPOW_CACHE_WORDS];
    {
        uint32_t prog_seed_lo = (uint32_t)(start_nonce / PROGPOW_PERIOD);
        for (uint32_t i = threadIdx.x;
             i < PROGPOW_CACHE_WORDS;
             i += blockDim.x)
        {
            c_dag[i] = g_dag[i % dag_size].s[i % PROGPOW_DAG_LOADS];
        }
    }
    __syncthreads();

    uint32_t mix[PROGPOW_REGS];
    // Initialise mix from nonce (simplified seed)
    #pragma unroll
    for (int i = 0; i < PROGPOW_REGS; i++)
        mix[i] = (uint32_t)(nonce >> (i & 1 ? 32 : 0)) ^ (uint32_t)i;

    // Run the period-specific ProgPoW loop
    bool hack_false = false;
    #pragma unroll 1
    for (uint32_t loop = 0; loop < PROGPOW_CNT_DAG; loop++)
        progPowLoop(loop, mix, g_dag, c_dag, hack_false);

    // Reduce mix to 8 words via FNV1a
    uint32_t mix_hash[8];
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        mix_hash[i] = 0x811c9dc5u;
        for (int j = i * 4; j < (i + 1) * 4 && j < PROGPOW_REGS; j++)
            mix_hash[i] = fnv1a(mix_hash[i], mix[j]);
    }

    // Final hash: simplified boundary check
    uint32_t result = mix_hash[0];
    // TODO: full keccak_f800 seed+mix -> final hash, compare to target
    if ((uint64_t)result > target) return;

    uint32_t index = atomicInc((uint32_t*)&g_output->count, 0xffffffffu);
    if (index >= KAWPOW_MAX_RESULTS) return;
    g_output->result[index].gid = gid;
    #pragma unroll
    for (int i = 0; i < 8; i++) g_output->result[index].mix[i] = mix_hash[i];
}
)";
    return src;
}

/* ------------------------------------------------------------------ */
/*  kawpow_compile_kernel                                                */
/* ------------------------------------------------------------------ */

bool kawpow_compile_kernel(uint64_t period, CUmodule* mod_out)
{
#ifndef HAVE_NVRTC
    applog(LOG_ERR, "KawPow: NVRTC not available — cannot JIT-compile kernel");
    (void)period; (void)mod_out;
    return false;
#else
    /* ------------------------------------------------------------------
     * Determine the current device's compute capability so that:
     *   (a) NVRTC can evaluate __launch_bounds__ against the correct
     *       register-file size for this architecture, and
     *   (b) the module cache key is unique per (period, sm_version) so
     *       heterogeneous GPU rigs don't share a stale-arch module.
     * ------------------------------------------------------------------ */
    int cur_dev = 0;
    cudaGetDevice(&cur_dev);

    int sm_major = 0, sm_minor = 0;
    cudaDeviceGetAttribute(&sm_major, cudaDevAttrComputeCapabilityMajor, cur_dev);
    cudaDeviceGetAttribute(&sm_minor, cudaDevAttrComputeCapabilityMinor, cur_dev);

    /* Cache key: pack (period, sm_major, sm_minor) into one uint64_t.
     * Periods are block/3; they fit in 48 bits.  SM is two decimal digits. */
    const uint64_t cache_key = period * 10000ULL
                             + (uint64_t)(sm_major * 100 + sm_minor);

    auto it = s_module_cache.find(cache_key);
    if (it != s_module_cache.end()) {
        *mod_out = it->second;
        return true;
    }

    std::string src = kawpow_build_kernel_source(period);

    nvrtcProgram prog;
    NVRTC_SAFE_CALL(nvrtcCreateProgram(&prog, src.c_str(),
                                       "kawpow_kernel.cu",
                                       0, nullptr, nullptr));

    /* Pass --gpu-architecture so NVRTC evaluates __launch_bounds__ against
     * the actual register file size of the target SM, not a generic baseline.
     * compute_XY (virtual arch) is used rather than sm_XY (real arch) because
     * NVRTC produces PTX which the driver then JITs to native ISA.           */
    std::string arch_flag = "--gpu-architecture=compute_"
                          + std::to_string(sm_major)
                          + std::to_string(sm_minor);

    const char* opts[] = {
        "--std=c++14",
        "--use_fast_math",
        "--generate-line-info",
        arch_flag.c_str(),
    };
    nvrtcResult compile_res = nvrtcCompileProgram(prog, 4, opts);
    if (compile_res != NVRTC_SUCCESS) {
        size_t log_size = 0;
        nvrtcGetProgramLogSize(prog, &log_size);
        std::string log(log_size, '\0');
        nvrtcGetProgramLog(prog, &log[0]);
        applog(LOG_ERR, "KawPow NVRTC compile error (period %llu, sm_%d%d):\n%s",
               (unsigned long long)period, sm_major, sm_minor, log.c_str());
        nvrtcDestroyProgram(&prog);
        return false;
    }

    size_t ptx_size = 0;
    NVRTC_SAFE_CALL(nvrtcGetPTXSize(prog, &ptx_size));
    std::string ptx(ptx_size, '\0');
    NVRTC_SAFE_CALL(nvrtcGetPTX(prog, &ptx[0]));
    nvrtcDestroyProgram(&prog);

    CUmodule mod;
    CUresult cu_res = cuModuleLoadData(&mod, ptx.c_str());
    if (cu_res != CUDA_SUCCESS) {
        const char* msg = nullptr;
        cuGetErrorString(cu_res, &msg);
        applog(LOG_ERR, "KawPow cuModuleLoadData failed (period %llu, sm_%d%d): %s",
               (unsigned long long)period, sm_major, sm_minor, msg ? msg : "unknown");
        return false;
    }

    s_module_cache[cache_key] = mod;
    *mod_out = mod;
    applog(LOG_INFO, "KawPow: compiled kernel for period %llu (sm_%d%d)",
           (unsigned long long)period, sm_major, sm_minor);
    return true;
#endif /* HAVE_NVRTC */
}

/* ------------------------------------------------------------------ */
/*  kawpow_run_search                                                    */
/* ------------------------------------------------------------------ */

#ifdef HAVE_NVRTC

void kawpow_run_search(CUmodule module, uint32_t grid_size, uint32_t block_size,
                      cudaStream_t stream,
                      volatile KawpowSearch_results* g_output,
                      uint64_t start_nonce,
                      etc_hash128_t* dag, uint32_t dag_size,
                      const uint32_t header[8], uint64_t target)
{
    CUfunction func;
    KAWPOW_CU_SAFE_CALL(cuModuleGetFunction(&func, module, "kawpow_search"));

    /* Zero result counter. */
    cudaMemset((void*)&g_output->count, 0, sizeof(uint32_t));

    void* args[] = {
        &dag,
        (void*)&dag_size,
        (void*)header,
        (void*)&target,
        (void*)&start_nonce,
        (void*)&g_output
    };

    KAWPOW_CU_SAFE_CALL(cuLaunchKernel(
        func,
        grid_size,  1, 1,
        block_size, 1, 1,
        0,
        (CUstream)stream,
        args, nullptr));

    KAWPOW_CUDA_SAFE_CALL(cudaGetLastError());
}

/* ------------------------------------------------------------------ */
/*  kawpow_free_modules                                                  */
/* ------------------------------------------------------------------ */

void kawpow_free_modules(void)
{
    for (auto& kv : s_module_cache)
        cuModuleUnload(kv.second);
    s_module_cache.clear();
}

#endif /* HAVE_NVRTC */
