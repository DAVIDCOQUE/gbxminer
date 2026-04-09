#pragma once
/**
 * etchash_cuda_miner_kernel_globals.h
 *
 * CUDA __constant__ memory declarations for the ETCHash kernel.
 * Included only from .cu translation units.
 *
 * d_dag        - device pointer to the DAG (128-byte nodes)
 * d_dag_size   - number of 128-byte DAG items (not byte count)
 * d_light      - device pointer to the light cache (64-byte nodes)
 * d_light_size - number of 64-byte light-cache items
 * d_header     - 32-byte block header (Keccak seed)
 * d_target     - 64-bit difficulty target (big-endian boundary)
 */

__constant__ uint32_t       d_etc_dag_size;
__constant__ etc_hash128_t* d_etc_dag;
__constant__ uint32_t       d_etc_light_size;
__constant__ etc_hash64_t*  d_etc_light;
__constant__ etc_hash32_t   d_etc_header;
__constant__ uint64_t       d_etc_target;

/* ------------------------------------------------------------------ */
/*  Warp-shuffle portability                                            */
/* ------------------------------------------------------------------ */
#if (__CUDACC_VER_MAJOR__ > 8)
#  define ETC_SHFL(x, y, z) __shfl_sync(0xFFFFFFFFU, (x), (y), (z))
#else
#  define ETC_SHFL(x, y, z) __shfl((x), (y), (z))
#endif

/* ------------------------------------------------------------------ */
/*  LDG (read-only cache load, sm_32+)                                  */
/* ------------------------------------------------------------------ */
#if (__CUDA_ARCH__ >= 320)
#  define ETC_LDG(x) __ldg(&(x))
#else
#  define ETC_LDG(x) (x)
#endif

/* ------------------------------------------------------------------ */
/*  Device inline annotation                                            */
/* ------------------------------------------------------------------ */
#ifndef DEV_INLINE
#  define DEV_INLINE __device__ __forceinline__
#endif
