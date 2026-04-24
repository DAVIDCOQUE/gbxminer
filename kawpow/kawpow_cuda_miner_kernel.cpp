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

/* ------------------------------------------------------------------ */
/*  Keccak-f[800]                                                       */
/* ------------------------------------------------------------------ */

/* KawPoW uses Keccak-f[800]: a 25 × uint32 (800-bit) state with 22 rounds.
 * This is distinct from Keccak-f[1600] used in Ethereum's SHA3.
 *
 * Reference: NIST Keccak reference, "Keccak-f[800]" variant.
 * The round constants below are the low 32 bits of the Keccak-f[1600]
 * constants (which are defined up to 64 bits), truncated to 32 bits.   */

__device__ __forceinline__ uint32_t keccak_rotl32(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32u - n));
}

__device__ void keccak_f800(uint32_t st[25])
{
    /* 22 round constants for Keccak-f[800] (low 32 bits of f[1600] RC). */
    const uint32_t RC[22] = {
        0x00000001u, 0x00008082u, 0x0000808au, 0x80008000u,
        0x0000808bu, 0x80000001u, 0x80008081u, 0x00008009u,
        0x0000008au, 0x00000088u, 0x80008009u, 0x8000000au,
        0x8000808bu, 0x0000008bu, 0x00008089u, 0x00008003u,
        0x00008002u, 0x00000080u, 0x0000800au, 0x8000000au,
        0x8000808au, 0x80000001u
    };
    /* Rho rotation offsets (mod 32), ordered by Pi permutation index.  */
    const uint32_t ROTC[24] = {
         1u,  3u,  6u, 10u, 15u, 21u, 28u, 36u % 32u,
        45u % 32u, 55u % 32u,  2u, 14u,
        27u, 41u % 32u, 56u % 32u,  8u,
        25u, 43u % 32u, 62u % 32u, 18u,
        39u % 32u, 61u % 32u, 20u, 44u % 32u
    };
    /* Pi lane permutation indices.                                     */
    const int PILN[24] = {
        10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
        15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
    };

    uint32_t bc[5], t;
    for (int r = 0; r < 22; r++) {
        /* Theta */
        for (int i = 0; i < 5; i++)
            bc[i] = st[i] ^ st[i+5] ^ st[i+10] ^ st[i+15] ^ st[i+20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i+4)%5] ^ keccak_rotl32(bc[(i+1)%5], 1u);
            for (int j = 0; j < 25; j += 5)
                st[j+i] ^= t;
        }
        /* Rho-Pi */
        t = st[1];
        for (int i = 0; i < 24; i++) {
            const int j = PILN[i];
            bc[0] = st[j];
            st[j] = keccak_rotl32(t, ROTC[i]);
            t = bc[0];
        }
        /* Chi */
        for (int j = 0; j < 25; j += 5) {
            uint32_t b[5];
            for (int i = 0; i < 5; i++) b[i] = st[j+i];
            for (int i = 0; i < 5; i++)
                st[j+i] ^= (~b[(i+1)%5]) & b[(i+2)%5];
        }
        /* Iota */
        st[0] ^= RC[r];
    }
}

/* KawPoW Keccak wrapper: pack header[8] || nonce || digest[8] into a
 * zeroed 25-word state, run 22 rounds, return first 8 output words.
 *
 * Used twice per nonce:
 *   seed  = keccak_f800_progpow(header, nonce, {0...0})
 *   final = keccak_f800_progpow(header, nonce, mix_hash)             */
__device__ void keccak_f800_progpow(
    const uint32_t header[8],
    const uint64_t nonce,
    const uint32_t digest[8],
    uint32_t       out[8])
{
    uint32_t st[25];
    #pragma unroll
    for (int i = 0; i < 25; i++) st[i] = 0u;
    #pragma unroll
    for (int i = 0; i < 8;  i++) st[i]      = header[i];
    st[8]  = (uint32_t)(nonce);
    st[9]  = (uint32_t)(nonce >> 32u);
    #pragma unroll
    for (int i = 0; i < 8;  i++) st[10 + i] = digest[i];
    keccak_f800(st);
    #pragma unroll
    for (int i = 0; i < 8;  i++) out[i]     = st[i];
}

/* ------------------------------------------------------------------ */
/*  KISS-99 PRNG + fill_mix                                             */
/* ------------------------------------------------------------------ */

/* KISS-99: Marsaglia's "Keep It Simple Stupid" 99-edition PRNG.
 * Used by fill_mix() to generate the initial mix array from the seed.  */
typedef struct { uint32_t z, w, jsr, jcong; } kiss99_t;

__device__ __forceinline__ uint32_t kiss99(kiss99_t *st)
{
    st->z    = 36969u * (st->z    & 0xFFFFu) + (st->z    >> 16u);
    st->w    = 18000u * (st->w    & 0xFFFFu) + (st->w    >> 16u);
    const uint32_t mwc = (st->z << 16u) + st->w;
    st->jsr ^= st->jsr << 17u;
    st->jsr ^= st->jsr >> 13u;
    st->jsr ^= st->jsr <<  5u;
    st->jcong = 69069u * st->jcong + 1234567u;
    return (mwc ^ st->jcong) + st->jsr;
}

/* Seed KISS-99 from the 64-bit Keccak seed and the lane index, then fill
 * mix[PROGPOW_REGS] with pseudo-random 32-bit values.  Each lane in the
 * warp gets a distinct sequence because lane_id is mixed into the state.*/
__device__ void fill_mix(const uint64_t seed, const uint32_t lane_id,
                          uint32_t mix[PROGPOW_REGS])
{
    kiss99_t st;
    st.z     = fnv1a(0x811c9dc5u, (uint32_t)(seed));
    st.w     = fnv1a(st.z,        (uint32_t)(seed >> 32u));
    st.jsr   = fnv1a(st.w,        lane_id);
    st.jcong = fnv1a(st.jsr,      lane_id);
    #pragma unroll
    for (int i = 0; i < PROGPOW_REGS; i++)
        mix[i] = kiss99(&st);
}

/* ------------------------------------------------------------------ */
/*  kawpow_search kernel                                                 */
/* ------------------------------------------------------------------ */

extern "C" __global__ __launch_bounds__(KAWPOW_LANES * 4, 2) void kawpow_search(
    const dag_t* __restrict__ g_dag,
    const uint32_t            dag_size,
    const uint32_t            header[8],
    const uint64_t            target,
    const uint64_t            start_nonce,
    volatile KawpowResults*   g_output)
{
    const uint32_t gid     = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t lane_id = threadIdx.x & (KAWPOW_LANES - 1);

    /* ProgPoW is warp-collaborative: all KAWPOW_LANES threads in a group
     * process the same nonce but hold different lanes of the mix array.
     * The warp index within the grid (gid / KAWPOW_LANES) uniquely
     * identifies the nonce for this group.                              */
    const uint64_t nonce = start_nonce + (uint64_t)(gid / KAWPOW_LANES);

    /* Load the DAG cache into shared memory.  All threads in the block
     * collaborate on this; __syncthreads() ensures visibility before use.*/
    __shared__ uint32_t c_dag[PROGPOW_CACHE_WORDS];
    for (uint32_t i = threadIdx.x; i < PROGPOW_CACHE_WORDS; i += blockDim.x)
        c_dag[i] = g_dag[i % dag_size].s[i % PROGPOW_DAG_LOADS];
    __syncthreads();

    /* --- Seed hash --------------------------------------------------- *
     * seed = keccak_f800(header || nonce || {0...0})                    *
     * Produces 8 words; seed[0:1] seeds the per-lane KISS-99 PRNG.     */
    uint32_t seed[8];
    {
        uint32_t zeros[8];
        #pragma unroll
        for (int i = 0; i < 8; i++) zeros[i] = 0u;
        keccak_f800_progpow(header, nonce, zeros, seed);
    }

    /* --- Mix initialisation ------------------------------------------ *
     * Each lane gets a distinct mix[PROGPOW_REGS] via KISS-99.          */
    const uint64_t seed64 = (uint64_t)seed[0] | ((uint64_t)seed[1] << 32u);
    uint32_t mix[PROGPOW_REGS];
    fill_mix(seed64, lane_id, mix);

    /* --- ProgPoW inner loop ------------------------------------------ *
     * Period-specific progPowLoop() is prepended by kawpow_build_kernel_source()
     * via ProgPow::getKern().  PROGPOW_CNT_DAG = 64 iterations.        */
    bool hack_false = false;
    #pragma unroll 1
    for (uint32_t loop = 0; loop < PROGPOW_CNT_DAG; loop++)
        progPowLoop(loop, mix, g_dag, c_dag, hack_false);

    /* --- Mix reduction ----------------------------------------------- *
     * Reduce mix[PROGPOW_REGS=32] → mix_hash[8] via FNV-1a.            *
     * 4 mix words fold into each mix_hash word.                         */
    uint32_t mix_hash[8];
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        mix_hash[i] = 0x811c9dc5u;
        for (int j = i * 4; j < (i + 1) * 4 && j < PROGPOW_REGS; j++)
            mix_hash[i] = fnv1a(mix_hash[i], mix[j]);
    }

    /* --- Final hash -------------------------------------------------- *
     * final = keccak_f800(header || nonce || mix_hash)                  *
     * Boundary check: first 64 bits of final hash ≤ target.            *
     * This matches the KawPoW / Ravencoin consensus rule.               */
    uint32_t final_hash[8];
    keccak_f800_progpow(header, nonce, mix_hash, final_hash);

    const uint64_t result64 =
        (uint64_t)final_hash[0] | ((uint64_t)final_hash[1] << 32u);
    if (result64 > target) return;

    /* --- Record solution --------------------------------------------- *
     * Store warp_id (= gid / KAWPOW_LANES) so the host recovers:
     *   winning_nonce = start_nonce + result.gid                        */
    const uint32_t warp_id = gid / KAWPOW_LANES;
    const uint32_t slot = atomicInc((uint32_t*)&g_output->count, 0xffffffffu);
    if (slot >= KAWPOW_MAX_RESULTS) return;
    g_output->result[slot].gid = warp_id;
    #pragma unroll
    for (int i = 0; i < 8; i++) g_output->result[slot].mix[i] = mix_hash[i];
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
