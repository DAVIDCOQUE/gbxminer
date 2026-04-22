
// originally from djm34 - github.com/djm34/ccminer-sp-neoscrypt
// kernel code from Nanashi Meiyo-Meijin 1.7.6-r10 (July 2016)

#include <stdio.h>
#include <memory.h>

#include <cuda_helper.h>
#include <cuda_vector_uint2x4.h>
#include "cuda_vectors.h"

typedef uint48 uint4x2;

#include "miner.h"

#ifdef __INTELLISENSE__
#define __CUDA_ARCH__ 500
#define __byte_perm(x,y,c) x
#define __shfl(x,y,c) x
#define atomicExch(p,x) x
#endif

static uint32_t* d_NNonce[MAX_GPUS];

__device__ uint2x4* W;
__device__ uint2x4* Tr;
__device__ uint2x4* Tr2;
__device__ uint2x4* Input;

__constant__ uint32_t c_data[64];
__constant__ uint32_t c_target[2];
__constant__ uint32_t key_init[16];
__constant__ uint32_t input_init[16];

static const __constant__ uint8 BLAKE2S_IV_Vec = {
	0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

static const uint8 BLAKE2S_IV_Vechost = {
	0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

static const uint32_t BLAKE2S_SIGMA_host[10][16] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

__constant__ uint32_t BLAKE2S_SIGMA[10][16] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

#define BLOCK_SIZE         64U
#define BLAKE2S_BLOCK_SIZE 64U
#define BLAKE2S_OUT_SIZE   32U

#define SALSA(a,b,c,d) { \
	t = rotateL(a+d,  7U); b ^= t; \
	t = rotateL(b+a,  9U); c ^= t; \
	t = rotateL(c+b, 13U); d ^= t; \
	t = rotateL(d+c, 18U); a ^= t; \
}

#define shf_r_clamp32(out,a,b,shift) \
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(out) : "r"(a), "r"(b), "r"(shift));

#if __CUDA_ARCH__ >= 700
__device__ __forceinline__ uint32_t WarpShuffle(uint32_t a, uint32_t b, uint32_t c)
{
	return __shfl_sync(0xffffffff, a, b, c);
}

__device__ __forceinline__ void WarpShuffle3(uint32_t &a1, uint32_t &a2, uint32_t &a3, uint32_t b1, uint32_t b2, uint32_t b3, uint32_t c)
{
	a1 = __shfl_sync(0xffffffff, a1, b1, c);
	a2 = __shfl_sync(0xffffffff, a2, b2, c);
	a3 = __shfl_sync(0xffffffff, a3, b3, c);
}

#elif __CUDA_ARCH__ >= 300
__device__ __forceinline__ uint32_t WarpShuffle(uint32_t a, uint32_t b, uint32_t c)
{
	return __shfl(a, b, c);
}

__device__ __forceinline__ void WarpShuffle3(uint32_t &a1, uint32_t &a2, uint32_t &a3, uint32_t b1, uint32_t b2, uint32_t b3, uint32_t c)
{
	a1 = WarpShuffle(a1, b1, c);
	a2 = WarpShuffle(a2, b2, c);
	a3 = WarpShuffle(a3, b3, c);
}

#else
__device__ __forceinline__ uint32_t WarpShuffle(uint32_t a, uint32_t b, uint32_t c)
{
	__shared__ uint32_t shared_mem[32];

	const uint32_t thread = blockDim.x * threadIdx.y + threadIdx.x;

	shared_mem[thread] = a;
	__threadfence_block();

	uint32_t result = shared_mem[(thread&~(c - 1)) + (b&(c - 1))];
	__threadfence_block();

	return result;
}

__device__ __forceinline__ void WarpShuffle3(uint32_t &a1, uint32_t &a2, uint32_t &a3, uint32_t b1, uint32_t b2, uint32_t b3, uint32_t c)
{
	__shared__ uint32_t shared_mem[32];

	const uint32_t thread = blockDim.x * threadIdx.y + threadIdx.x;

	shared_mem[thread] = a1;
	__threadfence_block();

	a1 = shared_mem[(thread&~(c - 1)) + (b1&(c - 1))];
	__threadfence_block();

	shared_mem[thread] = a2;
	__threadfence_block();

	a2 = shared_mem[(thread&~(c - 1)) + (b2&(c - 1))];
	__threadfence_block();

	shared_mem[thread] = a3;
	__threadfence_block();

	a3 = shared_mem[(thread&~(c - 1)) + (b3&(c - 1))];
	__threadfence_block();
}

#endif

#define CHACHA_STEP(a,b,c,d) { \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateL(b^c, 12); \
	a += b; d = __byte_perm(d^a, 0, 0x2103); \
	c += d; b = rotateL(b^c, 7); \
}

#if __CUDA_ARCH__ < 500

__device__ __forceinline__
static void shift256R4(uint32_t* ret, const uint8 &vec4, const uint32_t shift2)
{
#if __CUDA_ARCH__ >= 320
	uint32_t shift = 32U - shift2;
	asm("shf.r.clamp.b32 %0, 0, %1, %2;" : "=r"(ret[0]) : "r"(vec4.s0), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[1]) : "r"(vec4.s0), "r"(vec4.s1), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[2]) : "r"(vec4.s1), "r"(vec4.s2), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[3]) : "r"(vec4.s2), "r"(vec4.s3), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[4]) : "r"(vec4.s3), "r"(vec4.s4), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[5]) : "r"(vec4.s4), "r"(vec4.s5), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[6]) : "r"(vec4.s5), "r"(vec4.s6), "r"(shift));
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(ret[7]) : "r"(vec4.s6), "r"(vec4.s7), "r"(shift));
	asm("shr.b32         %0, %1, %2;"     : "=r"(ret[8]) : "r"(vec4.s7), "r"(shift));
#else
	// to check
	shift256R(ret, vec4, shift2);
#endif
}

#define BLAKE(a, b, c, d, key1, key2) { \
	a += key1; \
	a += b; d = rotateL(d^a, 16); \
	c += d; b = rotateR(b^c, 12); \
	a += key2; \
	a += b; d = rotateR(d^a, 8); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G(idx0, idx1, a, b, c, d, key) { \
	idx = BLAKE2S_SIGMA[idx0][idx1]; a += key[idx]; \
	a += b; d = rotateL(d^a, 16); \
	c += d; b = rotateR(b^c, 12); \
	idx = BLAKE2S_SIGMA[idx0][idx1 + 1]; a += key[idx]; \
	a += b; d = rotateR(d^a, 8); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE(idx0, idx1, a, b, c, d, key) { \
	a += key[idx0]; \
	a += b; d = rotateL(d^a, 16); \
	c += d; b = rotateR(b^c, 12); \
	a += key[idx1]; \
	a += b; d = rotateR(d^a, 8); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE0(idx0, idx1, a, b, c, d, key) { \
	a += b; d = rotateL(d^a, 16); \
	c += d; b = rotateR(b^c, 12); \
	a += b; d = rotateR(d^a, 8); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE1(idx0, idx1, a, b, c, d, key) { \
	a += key[idx0]; \
	a += b; d = rotateL(d^a, 16); \
	c += d; b = rotateR(b^c, 12); \
	a += b; d = rotateR(d^a, 8); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE2(idx0, idx1, a, b, c, d, key) { \
	a += b; d = rotateL(d^a, 16); \
	c += d; b = rotateR(b^c, 12); \
	a += key[idx1]; \
	a += b; d = rotateR(d^a, 8); \
	c += d; b = rotateR(b^c, 7); \
}

static __forceinline__ __device__
void Blake2S(uint32_t *out, const uint32_t* const __restrict__  inout, const  uint32_t * const __restrict__ TheKey)
{
	uint16 V;
	uint32_t idx;
	uint8 tmpblock;

	V.hi = BLAKE2S_IV_Vec;
	V.lo = BLAKE2S_IV_Vec;
	V.lo.s0 ^= 0x01012020;

	// Copy input block for later
	tmpblock = V.lo;

	V.hi.s4 ^= BLAKE2S_BLOCK_SIZE;

	//	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	BLAKE_G_PRE(0, 1, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE(2, 3, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(4, 5, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE(6, 7, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE0(8, 9, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE0(10, 11, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE0(12, 13, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE0(14, 15, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	BLAKE_G_PRE0(14, 10, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(4, 8, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE0(9, 15, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE2(13, 6, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE1(1, 12, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(0, 2, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE2(11, 7, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE(5, 3, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	BLAKE_G_PRE0(11, 8, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE2(12, 0, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(5, 2, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE0(15, 13, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE0(10, 14, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(3, 6, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE(7, 1, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE2(9, 4, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	BLAKE_G_PRE1(7, 9, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE(3, 1, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE0(13, 12, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE0(11, 14, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE(2, 6, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE1(5, 10, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE(4, 0, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE0(15, 8, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	BLAKE_G_PRE2(9, 0, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE(5, 7, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(2, 4, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE0(10, 15, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE2(14, 1, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE0(11, 12, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE1(6, 8, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE1(3, 13, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	BLAKE_G_PRE1(2, 12, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(6, 10, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE1(0, 11, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE2(8, 3, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE1(4, 13, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(7, 5, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE0(15, 14, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE1(1, 9, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	BLAKE_G_PRE2(12, 5, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(1, 15, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE0(14, 13, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE1(4, 10, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE(0, 7, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(6, 3, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE2(9, 2, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE0(8, 11, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	BLAKE_G_PRE0(13, 11, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(7, 14, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE2(12, 1, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE1(3, 9, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE(5, 0, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE2(15, 4, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE2(8, 6, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE(2, 10, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	BLAKE_G_PRE1(6, 15, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE0(14, 9, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE2(11, 3, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE1(0, 8, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE2(12, 2, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE2(13, 7, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE(1, 4, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE2(10, 5, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
	BLAKE_G_PRE2(10, 2, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE2(8, 4, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(7, 6, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE(1, 5, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE0(15, 11, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE0(9, 14, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE1(3, 12, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE2(13, 0, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);

	V.lo ^= V.hi ^ tmpblock;

	V.hi = BLAKE2S_IV_Vec;
	tmpblock = V.lo;

	V.hi.s4 ^= 128;
	V.hi.s6 = ~V.hi.s6;

	// { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	BLAKE_G_PRE(0, 1, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(2, 3, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(4, 5, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(6, 7, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(8, 9, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(10, 11, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(12, 13, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(14, 15, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	// { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	BLAKE_G_PRE(14, 10, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(4, 8, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(9, 15, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(13, 6, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(1, 12, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(0, 2, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(11, 7, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(5, 3, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	// { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	BLAKE_G_PRE(11, 8, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(12, 0, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(5, 2, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(15, 13, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(10, 14, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(3, 6, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(7, 1, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(9, 4, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	// { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	BLAKE_G_PRE(7, 9, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(3, 1, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(13, 12, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(11, 14, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(2, 6, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(5, 10, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(4, 0, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(15, 8, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);

	for (uint32_t x = 4U; x < 10U; x++)
	{
		BLAKE_G(x, 0x00, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
		BLAKE_G(x, 0x02, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
		BLAKE_G(x, 0x04, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
		BLAKE_G(x, 0x06, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
		BLAKE_G(x, 0x08, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
		BLAKE_G(x, 0x0A, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
		BLAKE_G(x, 0x0C, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
		BLAKE_G(x, 0x0E, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	}

	V.lo ^= V.hi ^ tmpblock;

	((uint8*)out)[0] = V.lo;
}
#endif

#if __CUDA_ARCH__ >= 500

#define BLAKE_G(idx0, idx1, a, b, c, d, key) { \
	idx = BLAKE2S_SIGMA[idx0][idx1]; a += key[idx]; \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateR(b^c, 12); \
	idx = BLAKE2S_SIGMA[idx0][idx1 + 1]; a += key[idx]; \
	a += b; d = __byte_perm(d^a, 0, 0x0321); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE(a, b, c, d, key1,key2) { \
	a += key1; \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateR(b^c, 12); \
	a += key2; \
	a += b; d = __byte_perm(d^a, 0, 0x0321); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE(idx0,idx1, a, b, c, d, key) { \
	a += key[idx0]; \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateR(b^c, 12); \
	a += key[idx1]; \
	a += b; d = __byte_perm(d^a, 0, 0x0321); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE0(idx0,idx1, a, b, c, d, key) { \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateR(b^c, 12); \
	a += b; d = __byte_perm(d^a, 0, 0x0321); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE1(idx0,idx1, a, b, c, d, key) { \
	a += key[idx0]; \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateR(b^c, 12); \
	a += b; d = __byte_perm(d^a, 0, 0x0321); \
	c += d; b = rotateR(b^c, 7); \
}

#define BLAKE_G_PRE2(idx0,idx1, a, b, c, d, key) { \
	a += b; d = __byte_perm(d^a, 0, 0x1032); \
	c += d; b = rotateR(b^c, 12); \
	a += key[idx1]; \
	a += b; d = __byte_perm(d^a, 0, 0x0321); \
	c += d; b = rotateR(b^c, 7); \
}

static __forceinline__ __device__
void Blake2S_v2(uint32_t *out, const uint32_t* __restrict__  inout, const  uint32_t * __restrict__ TheKey)
{
	uint16 V;
	uint8 tmpblock;

	V.hi = BLAKE2S_IV_Vec;
	V.lo = BLAKE2S_IV_Vec;
	V.lo.s0 ^= 0x01012020;

	// Copy input block for later
	tmpblock = V.lo;

	V.hi.s4 ^= BLAKE2S_BLOCK_SIZE;

	//	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	BLAKE_G_PRE(0, 1, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE(2, 3, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(4, 5, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE(6, 7, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE0(8, 9, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE0(10, 11, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE0(12, 13, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE0(14, 15, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	BLAKE_G_PRE0(14, 10, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(4, 8, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE0(9, 15, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE2(13, 6, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE1(1, 12, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(0, 2, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE2(11, 7, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE(5, 3, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	BLAKE_G_PRE0(11, 8, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE2(12, 0, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(5, 2, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE0(15, 13, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE0(10, 14, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(3, 6, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE(7, 1, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE2(9, 4, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	BLAKE_G_PRE1(7, 9, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE(3, 1, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE0(13, 12, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE0(11, 14, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE(2, 6, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE1(5, 10, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE(4, 0, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE0(15, 8, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	BLAKE_G_PRE2(9, 0, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE(5, 7, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(2, 4, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE0(10, 15, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE2(14, 1, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE0(11, 12, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE1(6, 8, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE1(3, 13, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	BLAKE_G_PRE1(2, 12, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(6, 10, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE1(0, 11, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE2(8, 3, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE1(4, 13, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(7, 5, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE0(15, 14, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE1(1, 9, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	BLAKE_G_PRE2(12, 5, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(1, 15, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE0(14, 13, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE1(4, 10, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE(0, 7, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE(6, 3, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE2(9, 2, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE0(8, 11, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	BLAKE_G_PRE0(13, 11, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE1(7, 14, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE2(12, 1, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE1(3, 9, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE(5, 0, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE2(15, 4, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE2(8, 6, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE(2, 10, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	BLAKE_G_PRE1(6, 15, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE0(14, 9, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE2(11, 3, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE1(0, 8, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE2(12, 2, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE2(13, 7, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE(1, 4, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE2(10, 5, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);
	// { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
	BLAKE_G_PRE2(10, 2, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, TheKey);
	BLAKE_G_PRE2(8, 4, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, TheKey);
	BLAKE_G_PRE(7, 6, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, TheKey);
	BLAKE_G_PRE(1, 5, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, TheKey);
	BLAKE_G_PRE0(15, 11, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, TheKey);
	BLAKE_G_PRE0(9, 14, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, TheKey);
	BLAKE_G_PRE1(3, 12, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, TheKey);
	BLAKE_G_PRE2(13, 0, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, TheKey);

	V.lo ^= V.hi;
	V.lo ^= tmpblock;

	V.hi = BLAKE2S_IV_Vec;
	tmpblock = V.lo;

	V.hi.s4 ^= 128;
	V.hi.s6 = ~V.hi.s6;

	// { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	BLAKE_G_PRE(0, 1, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(2, 3, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(4, 5, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(6, 7, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(8, 9, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(10, 11, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(12, 13, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(14, 15, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	// { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	BLAKE_G_PRE(14, 10, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(4, 8, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(9, 15, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(13, 6, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(1, 12, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(0, 2, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(11, 7, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(5, 3, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	// { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	BLAKE_G_PRE(11, 8, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(12, 0, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(5, 2, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(15, 13, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(10, 14, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(3, 6, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(7, 1, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(9, 4, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	// { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	BLAKE_G_PRE(7, 9, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
	BLAKE_G_PRE(3, 1, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
	BLAKE_G_PRE(13, 12, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
	BLAKE_G_PRE(11, 14, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
	BLAKE_G_PRE(2, 6, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
	BLAKE_G_PRE(5, 10, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
	BLAKE_G_PRE(4, 0, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
	BLAKE_G_PRE(15, 8, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);

	BLAKE(V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout[9], inout[0]);
	BLAKE(V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout[5], inout[7]);
	BLAKE(V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout[2], inout[4]);
	BLAKE(V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout[10], inout[15]);
	BLAKE(V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout[14], inout[1]);
	BLAKE(V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout[11], inout[12]);
	BLAKE(V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout[6], inout[8]);
	BLAKE(V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout[3], inout[13]);

	BLAKE(V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout[2], inout[12]);
	BLAKE(V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout[6], inout[10]);
	BLAKE(V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout[0], inout[11]);
	BLAKE(V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout[8], inout[3]);
	BLAKE(V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout[4], inout[13]);
	BLAKE(V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout[7], inout[5]);
	BLAKE(V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout[15], inout[14]);
	BLAKE(V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout[1], inout[9]);

	BLAKE(V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout[12], inout[5]);
	BLAKE(V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout[1], inout[15]);
	BLAKE(V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout[14], inout[13]);
	BLAKE(V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout[4], inout[10]);
	BLAKE(V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout[0], inout[7]);
	BLAKE(V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout[6], inout[3]);
	BLAKE(V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout[9], inout[2]);
	BLAKE(V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout[8], inout[11]);
	// 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10,
	BLAKE(V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout[13], inout[11]);
	BLAKE(V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout[7], inout[14]);
	BLAKE(V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout[12], inout[1]);
	BLAKE(V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout[3], inout[9]);
	BLAKE(V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout[5], inout[0]);
	BLAKE(V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout[15], inout[4]);
	BLAKE(V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout[8], inout[6]);
	BLAKE(V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout[2], inout[10]);
	// 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5,
	BLAKE(V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout[6], inout[15]);
	BLAKE(V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout[14], inout[9]);
	BLAKE(V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout[11], inout[3]);
	BLAKE(V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout[0], inout[8]);
	BLAKE(V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout[12], inout[2]);
	BLAKE(V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout[13], inout[7]);
	BLAKE(V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout[1], inout[4]);
	BLAKE(V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout[10], inout[5]);
	// 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0,
	BLAKE(V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout[10], inout[2]);
	BLAKE(V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout[8], inout[4]);
	BLAKE(V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout[7], inout[6]);
	BLAKE(V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout[1], inout[5]);
	BLAKE(V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout[15], inout[11]);
	BLAKE(V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout[9], inout[14]);
	BLAKE(V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout[3], inout[12]);
	BLAKE(V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout[13], inout[0]);

	V.lo ^= V.hi;
	V.lo ^= tmpblock;

	((uint8*)out)[0] = V.lo;
}

#endif /* __CUDA_ARCH__ >= 500 */

#define SALSA_CORE(state) { \
	uint32_t t; \
	SALSA(state.x, state.y, state.z, state.w); \
	WarpShuffle3(state.y, state.z, state.w, threadIdx.x + 3, threadIdx.x + 2, threadIdx.x + 1,4); \
	SALSA(state.x, state.w, state.z, state.y); \
	WarpShuffle3(state.y, state.z, state.w, threadIdx.x + 1, threadIdx.x + 2, threadIdx.x + 3,4); \
}

#define CHACHA_CORE_PARALLEL(state)	{ \
	CHACHA_STEP(state.x, state.y, state.z, state.w); \
	WarpShuffle3(state.y, state.z, state.w, threadIdx.x + 1, threadIdx.x + 2, threadIdx.x + 3,4); \
	CHACHA_STEP(state.x, state.y, state.z, state.w); \
	WarpShuffle3(state.y, state.z, state.w, threadIdx.x + 3, threadIdx.x + 2, threadIdx.x + 1,4); \
}

__forceinline__ __device__
uint4 salsa_small_scalar_rnd(const uint4 X)
{
	uint4 state = X;

	#pragma unroll 1
	for (int i = 0; i < 10; i++) {
		SALSA_CORE(state);
	}

	return (X + state);
}

__device__ __forceinline__
uint4 chacha_small_parallel_rnd(const uint4 X)
{
	uint4 state = X;

	#pragma unroll 1
	for (int i = 0; i < 10; i++) {
		CHACHA_CORE_PARALLEL(state);
	}
	return (X + state);
}

__device__ __forceinline__
void neoscrypt_chacha(uint4 XV[4])
{
	uint4 temp;

	XV[0] = chacha_small_parallel_rnd(XV[0] ^ XV[3]);
	temp = chacha_small_parallel_rnd(XV[1] ^ XV[0]);
	XV[1] = chacha_small_parallel_rnd(XV[2] ^ temp);
	XV[3] = chacha_small_parallel_rnd(XV[3] ^ XV[1]);
	XV[2] = temp;
}

__device__ __forceinline__
void neoscrypt_salsa(uint4 XV[4])
{
	uint4 temp;

	XV[0] = salsa_small_scalar_rnd(XV[0] ^ XV[3]);
	temp = salsa_small_scalar_rnd(XV[1] ^ XV[0]);
	XV[1] = salsa_small_scalar_rnd(XV[2] ^ temp);
	XV[3] = salsa_small_scalar_rnd(XV[3] ^ XV[1]);
	XV[2] = temp;
}


#if __CUDA_ARCH__ < 500
static __forceinline__ __device__
void fastkdf256_v1(const uint32_t thread, const uint32_t nonce, uint32_t* const s_data)
{
	uint2x4 output[8];
	uint32_t* B = (uint32_t*)&s_data[threadIdx.x * 64U];
	uint32_t qbuf, rbuf, bitbuf;
	uint32_t input[BLAKE2S_BLOCK_SIZE / 4];
	uint32_t key[BLAKE2S_BLOCK_SIZE / 4] = { 0 };

	const uint32_t data18 = c_data[18];
	const uint32_t data20 = c_data[0];

	((uintx64*)(B))[0] = ((uintx64*)c_data)[0];
	((uint32_t*)B)[19] = nonce;
	((uint32_t*)B)[39] = nonce;
	((uint32_t*)B)[59] = nonce;

	((uint816*)input)[0] = ((uint816*)input_init)[0];
	((uint4x2*)key)[0] = ((uint4x2*)key_init)[0];

	#pragma unroll 1
	for (int i = 0; i < 31; i++)
	{
		uint32_t bufidx = 0;
		#pragma unroll
		for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
		{
			uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
			bufhelper = bufhelper + (bufhelper >> 16);
			bufidx += bufhelper;
		}
		bufidx &= 0x000000ff;
		qbuf = bufidx >> 2;
		rbuf = bufidx & 3;
		bitbuf = rbuf << 3;

		uint32_t shifted[9];
		shift256R4(shifted, ((uint8*)input)[0], bitbuf);

		uint32_t temp[9];
		//#pragma unroll
		for (int k = 0; k < 9; k++)
		{
			uint32_t indice = (k + qbuf) & 0x3f;
			temp[k] = B[indice] ^ shifted[k];
			B[indice] = temp[k];
		}
#if __CUDA_ARCH__ >= 320  || !defined(__CUDA_ARCH__)
		uint32_t a = c_data[qbuf & 0x3f], b;
		//#pragma unroll
		for (int k = 0; k<16; k += 2)
		{
			b = c_data[(qbuf + k + 1) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k]) : "r"(a), "r"(b), "r"(bitbuf));
			a = c_data[(qbuf + k + 2) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k + 1]) : "r"(b), "r"(a), "r"(bitbuf));
		}

		const uint32_t noncepos = 19U - qbuf % 20U;
		if (noncepos <= 16U && qbuf < 60U)
		{
			if (noncepos != 0)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos - 1]) : "r"(data18), "r"(nonce), "r"(bitbuf));
			if (noncepos != 16U)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos]) : "r"(nonce), "r"(data20), "r"(bitbuf));
		}

		for (int k = 0; k<8; k++)
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[k]) : "r"(temp[k]), "r"(temp[k + 1]), "r"(bitbuf));
#else
		//#error SM 3.0 code missing here
		printf("", data18, data20);
#endif
		Blake2S(input, input, key);
	}

	uint32_t bufidx = 0;
	#pragma unroll
	for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
	{
		uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
		bufhelper = bufhelper + (bufhelper >> 16);
		bufidx += bufhelper;
	}
	bufidx &= 0x000000ff;
	qbuf = bufidx >> 2;
	rbuf = bufidx & 3;
	bitbuf = rbuf << 3;

#if __CUDA_ARCH__ >= 320
	for (int i = 0; i<64; i++)
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(((uint32_t*)output)[i]) : "r"(B[(qbuf + i) & 0x3f]), "r"(B[(qbuf + i + 1) & 0x3f4]), "r"(bitbuf));
#endif

	((ulonglong4*)output)[0] ^= ((ulonglong4*)input)[0];
	((uintx64*)output)[0] ^= ((uintx64*)c_data)[0];
	((uint32_t*)output)[19] ^= nonce;
	((uint32_t*)output)[39] ^= nonce;
	((uint32_t*)output)[59] ^= nonce;

	for (int i = 0; i<8; i++)
		(Input + 8U * thread)[i] = output[i];
}
#endif

#if __CUDA_ARCH__ >= 500
static __forceinline__ __device__
void fastkdf256_v2(const uint32_t thread, const uint32_t nonce, uint32_t* const s_data)
{
	const uint32_t data18 = c_data[18];
	const uint32_t data20 = c_data[0];
	uint32_t input[16];
	uint32_t key[16] = { 0 };
	uint32_t qbuf, rbuf, bitbuf;

	uint32_t* B = (uint32_t*)&s_data[threadIdx.x * 64U];
	((uintx64*)(B))[0] = ((uintx64*)c_data)[0];

	B[19] = nonce;
	B[39] = nonce;
	B[59] = nonce;

	{
		uint32_t bufidx = 0;
		#pragma unroll
		for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
		{
			uint32_t bufhelper = (input_init[x] & 0x00ff00ff) + ((input_init[x] & 0xff00ff00) >> 8);
			bufhelper = bufhelper + (bufhelper >> 16);
			bufidx += bufhelper;
		}
		bufidx &= 0x000000ff;
		qbuf = bufidx >> 2;
		rbuf = bufidx & 3;
		bitbuf = rbuf << 3;

		uint32_t temp[9];

		uint32_t shifted;
		uint32_t shift = 32U - bitbuf;
		asm("shl.b32         %0, %1, %2;"     : "=r"(shifted) : "r"(input_init[0]), "r"(bitbuf));
		temp[0] = B[(0 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[0]), "r"(input_init[1]), "r"(shift));
		temp[1] = B[(1 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[1]), "r"(input_init[2]), "r"(shift));
		temp[2] = B[(2 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[2]), "r"(input_init[3]), "r"(shift));
		temp[3] = B[(3 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[3]), "r"(input_init[4]), "r"(shift));
		temp[4] = B[(4 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[4]), "r"(input_init[5]), "r"(shift));
		temp[5] = B[(5 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[5]), "r"(input_init[6]), "r"(shift));
		temp[6] = B[(6 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input_init[6]), "r"(input_init[7]), "r"(shift));
		temp[7] = B[(7 + qbuf) & 0x3f] ^ shifted;
		asm("shr.b32         %0, %1, %2;"     : "=r"(shifted) : "r"(input_init[7]), "r"(shift));
		temp[8] = B[(8 + qbuf) & 0x3f] ^ shifted;

		uint32_t a = c_data[qbuf & 0x3f], b;

		#pragma unroll
		for (int k = 0; k<16; k += 2)
		{
			b = c_data[(qbuf + k + 1) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k]) : "r"(a), "r"(b), "r"(bitbuf));
			a = c_data[(qbuf + k + 2) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k + 1]) : "r"(b), "r"(a), "r"(bitbuf));
		}

		const uint32_t noncepos = 19 - qbuf % 20U;
		if (noncepos <= 16U && qbuf < 60U)
		{
			if (noncepos)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos - 1]) : "r"(data18), "r"(nonce), "r"(bitbuf));
			if (noncepos != 16U)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos]) : "r"(nonce), "r"(data20), "r"(bitbuf));
		}

		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[0]) : "r"(temp[0]), "r"(temp[1]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[1]) : "r"(temp[1]), "r"(temp[2]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[2]) : "r"(temp[2]), "r"(temp[3]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[3]) : "r"(temp[3]), "r"(temp[4]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[4]) : "r"(temp[4]), "r"(temp[5]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[5]) : "r"(temp[5]), "r"(temp[6]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[6]) : "r"(temp[6]), "r"(temp[7]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[7]) : "r"(temp[7]), "r"(temp[8]), "r"(bitbuf));

		Blake2S_v2(input, input, key);

		#pragma unroll
		for (int k = 0; k < 9; k++)
			B[(k + qbuf) & 0x3f] = temp[k];
	}

	for (int i = 1; i < 31; i++)
	{
		uint32_t bufidx = 0;
		#pragma unroll
		for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
		{
			uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
			bufhelper = bufhelper + (bufhelper >> 16);
			bufidx += bufhelper;
		}
		bufidx &= 0x000000ff;
		qbuf = bufidx >> 2;
		rbuf = bufidx & 3;
		bitbuf = rbuf << 3;

		uint32_t temp[9];

		uint32_t shifted;
		uint32_t shift = 32U - bitbuf;
		asm("shl.b32         %0, %1, %2;"     : "=r"(shifted) : "r"(input[0]), "r"(bitbuf));
		temp[0] = B[(0 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[0]), "r"(input[1]), "r"(shift));
		temp[1] = B[(1 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[1]), "r"(input[2]), "r"(shift));
		temp[2] = B[(2 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[2]), "r"(input[3]), "r"(shift));
		temp[3] = B[(3 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[3]), "r"(input[4]), "r"(shift));
		temp[4] = B[(4 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[4]), "r"(input[5]), "r"(shift));
		temp[5] = B[(5 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[5]), "r"(input[6]), "r"(shift));
		temp[6] = B[(6 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[6]), "r"(input[7]), "r"(shift));
		temp[7] = B[(7 + qbuf) & 0x3f] ^ shifted;
		asm("shr.b32         %0, %1, %2;"     : "=r"(shifted) : "r"(input[7]), "r"(shift));
		temp[8] = B[(8 + qbuf) & 0x3f] ^ shifted;

		uint32_t a = c_data[qbuf & 0x3f], b;

		#pragma unroll
		for (int k = 0; k<16; k += 2)
		{
			b = c_data[(qbuf + k + 1) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k]) : "r"(a), "r"(b), "r"(bitbuf));
			a = c_data[(qbuf + k + 2) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k + 1]) : "r"(b), "r"(a), "r"(bitbuf));
		}

		const uint32_t noncepos = 19 - qbuf % 20U;
		if (noncepos <= 16U && qbuf < 60U)
		{
			if (noncepos)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos - 1]) : "r"(data18), "r"(nonce), "r"(bitbuf));
			if (noncepos != 16U)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos]) : "r"(nonce), "r"(data20), "r"(bitbuf));
		}

		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[0]) : "r"(temp[0]), "r"(temp[1]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[1]) : "r"(temp[1]), "r"(temp[2]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[2]) : "r"(temp[2]), "r"(temp[3]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[3]) : "r"(temp[3]), "r"(temp[4]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[4]) : "r"(temp[4]), "r"(temp[5]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[5]) : "r"(temp[5]), "r"(temp[6]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[6]) : "r"(temp[6]), "r"(temp[7]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[7]) : "r"(temp[7]), "r"(temp[8]), "r"(bitbuf));

		Blake2S_v2(input, input, key);

		#pragma unroll
		for (int k = 0; k < 9; k++)
			B[(k + qbuf) & 0x3f] = temp[k];
	}

	{
		uint32_t bufidx = 0;
		#pragma unroll
		for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
		{
			uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
			bufhelper = bufhelper + (bufhelper >> 16);
			bufidx += bufhelper;
		}
		bufidx &= 0x000000ff;
		qbuf = bufidx >> 2;
		rbuf = bufidx & 3;
		bitbuf = rbuf << 3;
	}

	uint2x4 output[8];
	for (int i = 0; i<64; i++) {
		const uint32_t a = (qbuf + i) & 0x3f, b = (qbuf + i + 1) & 0x3f;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(((uint32_t*)output)[i]) : "r"(B[a]), "r"(B[b]), "r"(bitbuf));
	}

	output[0] ^= ((uint2x4*)input)[0];
	#pragma unroll
	for (int i = 0; i<8; i++)
		output[i] ^= ((uint2x4*)c_data)[i];

	((uint32_t*)output)[19] ^= nonce;
	((uint32_t*)output)[39] ^= nonce;
	((uint32_t*)output)[59] ^= nonce;
	((ulonglong16 *)(Input + 8U * thread))[0] = ((ulonglong16*)output)[0];
}
#endif

#if __CUDA_ARCH__ < 500
static __forceinline__ __device__
uint32_t fastkdf32_v1(uint32_t thread, const uint32_t nonce, uint32_t* const salt, uint32_t* const s_data)
{
	const uint32_t cdata7 = c_data[7];
	const uint32_t data18 = c_data[18];
	const uint32_t data20 = c_data[0];

	uint32_t* B0 = (uint32_t*)&s_data[threadIdx.x * 64U];
	((uintx64*)B0)[0] = ((uintx64*)salt)[0];

	uint32_t input[BLAKE2S_BLOCK_SIZE / 4];
	((uint816*)input)[0] = ((uint816*)c_data)[0];

	uint32_t key[BLAKE2S_BLOCK_SIZE / 4];
	((uint4x2*)key)[0] = ((uint4x2*)salt)[0];
	((uint4*)key)[2] = make_uint4(0, 0, 0, 0);
	((uint4*)key)[3] = make_uint4(0, 0, 0, 0);

	uint32_t qbuf, rbuf, bitbuf;
	uint32_t temp[9];

	#pragma unroll 1
	for (int i = 0; i < 31; i++)
	{
		Blake2S(input, input, key);

		uint32_t bufidx = 0;
		#pragma unroll
		for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
		{
			uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
			bufhelper = bufhelper + (bufhelper >> 16);
			bufidx += bufhelper;
		}
		bufidx &= 0x000000ff;
		qbuf = bufidx >> 2;
		rbuf = bufidx & 3;
		bitbuf = rbuf << 3;
		uint32_t shifted[9];

		shift256R4(shifted, ((uint8*)input)[0], bitbuf);

		for (int k = 0; k < 9; k++) {
			temp[k] = B0[(k + qbuf) & 0x3f];
		}

		((uint2x4*)temp)[0] ^= ((uint2x4*)shifted)[0];
		temp[8] ^= shifted[8];

#if __CUDA_ARCH__ >= 320 || !defined(__CUDA_ARCH__)
		uint32_t a = c_data[qbuf & 0x3f], b;
		//#pragma unroll
		for (int k = 0; k<16; k += 2)
		{
			b = c_data[(qbuf + k + 1) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k]) : "r"(a), "r"(b), "r"(bitbuf));
			a = c_data[(qbuf + k + 2) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k + 1]) : "r"(b), "r"(a), "r"(bitbuf));
		}

		const uint32_t noncepos = 19U - qbuf % 20U;
		if (noncepos <= 16U && qbuf < 60U)
		{
			if (noncepos != 0)	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos - 1]) : "r"(data18), "r"(nonce), "r"(bitbuf));
			if (noncepos != 16U)	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos]) : "r"(nonce), "r"(data20), "r"(bitbuf));
		}

		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[0]) : "r"(temp[0]), "r"(temp[1]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[1]) : "r"(temp[1]), "r"(temp[2]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[2]) : "r"(temp[2]), "r"(temp[3]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[3]) : "r"(temp[3]), "r"(temp[4]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[4]) : "r"(temp[4]), "r"(temp[5]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[5]) : "r"(temp[5]), "r"(temp[6]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[6]) : "r"(temp[6]), "r"(temp[7]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[7]) : "r"(temp[7]), "r"(temp[8]), "r"(bitbuf));
#else
		//#error SM 3.0 code missing here
		printf("", data18, data20);
#endif
		for (int k = 0; k < 9; k++) {
			B0[(k + qbuf) & 0x3f] = temp[k];
		}
	}

	Blake2S(input, input, key);

	uint32_t bufidx = 0;
	#pragma unroll
	for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
	{
		uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
		bufhelper = bufhelper + (bufhelper >> 16);
		bufidx += bufhelper;
	}
	bufidx &= 0x000000ff;
	qbuf = bufidx >> 2;
	rbuf = bufidx & 3;
	bitbuf = rbuf << 3;

	for (int k = 7; k < 9; k++) {
		temp[k] = B0[(k + qbuf) & 0x3f];
	}

	uint32_t output;
#if __CUDA_ARCH__ >= 320
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(output) : "r"(temp[7]), "r"(temp[8]), "r"(bitbuf));
#else
	output = (MAKE_ULONGLONG(temp[7], temp[8]) >> bitbuf); // to check maybe 7/8 reversed
#endif
	output ^= input[7] ^ cdata7;
	return output;
}
#endif

#if __CUDA_ARCH__ >= 500
static __forceinline__ __device__
uint32_t fastkdf32_v3(uint32_t thread, const uint32_t nonce, uint32_t* const salt, uint32_t* const s_data)
{
	const uint32_t cdata7 = c_data[7];
	const uint32_t data18 = c_data[18];
	const uint32_t data20 = c_data[0];

	uint32_t* B0 = (uint32_t*)&s_data[threadIdx.x * 64U];
	((uintx64*)B0)[0] = ((uintx64*)salt)[0];

	uint32_t input[BLAKE2S_BLOCK_SIZE / 4];
	((uint816*)input)[0] = ((uint816*)c_data)[0];

	uint32_t key[BLAKE2S_BLOCK_SIZE / 4];
	((uint4x2*)key)[0] = ((uint4x2*)salt)[0];
	((uint4*)key)[2] = make_uint4(0, 0, 0, 0);
	((uint4*)key)[3] = make_uint4(0, 0, 0, 0);

	uint32_t qbuf, rbuf, bitbuf;
	uint32_t temp[9];

	#pragma unroll 1
	for (int i = 0; i < 31; i++)
	{
		Blake2S_v2(input, input, key);

		uint32_t bufidx = 0;
		#pragma unroll
		for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
		{
			uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
			bufhelper = bufhelper + (bufhelper >> 16);
			bufidx += bufhelper;
		}
		bufidx &= 0x000000ff;
		qbuf = bufidx >> 2;
		rbuf = bufidx & 3;
		bitbuf = rbuf << 3;

		uint32_t shifted;
		uint32_t shift = 32U - bitbuf;
		asm("shl.b32         %0, %1, %2;"     : "=r"(shifted) : "r"(input[0]), "r"(bitbuf));
		temp[0] = B0[(0 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[0]), "r"(input[1]), "r"(shift));
		temp[1] = B0[(1 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[1]), "r"(input[2]), "r"(shift));
		temp[2] = B0[(2 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[2]), "r"(input[3]), "r"(shift));
		temp[3] = B0[(3 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[3]), "r"(input[4]), "r"(shift));
		temp[4] = B0[(4 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[4]), "r"(input[5]), "r"(shift));
		temp[5] = B0[(5 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[5]), "r"(input[6]), "r"(shift));
		temp[6] = B0[(6 + qbuf) & 0x3f] ^ shifted;
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(shifted) : "r"(input[6]), "r"(input[7]), "r"(shift));
		temp[7] = B0[(7 + qbuf) & 0x3f] ^ shifted;
		asm("shr.b32         %0, %1, %2;"     : "=r"(shifted) : "r"(input[7]), "r"(shift));
		temp[8] = B0[(8 + qbuf) & 0x3f] ^ shifted;

		uint32_t a = c_data[qbuf & 0x3f], b;
		#pragma unroll
		for (int k = 0; k<16; k += 2)
		{
			b = c_data[(qbuf + k + 1) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k]) : "r"(a), "r"(b), "r"(bitbuf));
			a = c_data[(qbuf + k + 2) & 0x3f];
			asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[k + 1]) : "r"(b), "r"(a), "r"(bitbuf));
		}

		const uint32_t noncepos = 19U - qbuf % 20U;
		if (noncepos <= 16U && qbuf < 60U)
		{
			if (noncepos != 0)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos - 1]) : "r"(data18), "r"(nonce), "r"(bitbuf));
			if (noncepos != 16U)
				asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(input[noncepos]) : "r"(nonce), "r"(data20), "r"(bitbuf));
		}

		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[0]) : "r"(temp[0]), "r"(temp[1]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[1]) : "r"(temp[1]), "r"(temp[2]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[2]) : "r"(temp[2]), "r"(temp[3]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[3]) : "r"(temp[3]), "r"(temp[4]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[4]) : "r"(temp[4]), "r"(temp[5]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[5]) : "r"(temp[5]), "r"(temp[6]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[6]) : "r"(temp[6]), "r"(temp[7]), "r"(bitbuf));
		asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(key[7]) : "r"(temp[7]), "r"(temp[8]), "r"(bitbuf));

		#pragma unroll
		for (int k = 0; k < 9; k++) {
			B0[(k + qbuf) & 0x3f] = temp[k];
		}
	}

	Blake2S_v2(input, input, key);

	uint32_t bufidx = 0;
	#pragma unroll
	for (int x = 0; x < BLAKE2S_OUT_SIZE / 4; ++x)
	{
		uint32_t bufhelper = (input[x] & 0x00ff00ff) + ((input[x] & 0xff00ff00) >> 8);
		bufhelper = bufhelper + (bufhelper >> 16);
		bufidx += bufhelper;
	}
	bufidx &= 0x000000ff;
	qbuf = bufidx >> 2;
	rbuf = bufidx & 3;
	bitbuf = rbuf << 3;

	temp[7] = B0[(qbuf + 7) & 0x3f];
	temp[8] = B0[(qbuf + 8) & 0x3f];

	uint32_t output;
	asm("shf.r.clamp.b32 %0, %1, %2, %3;" : "=r"(output) : "r"(temp[7]), "r"(temp[8]), "r"(bitbuf));
	output ^= input[7] ^ cdata7;
	return output;
}
#endif


#define BLAKE_Ghost(idx0, idx1, a, b, c, d, key) { \
	idx = BLAKE2S_SIGMA_host[idx0][idx1]; a += key[idx]; \
	a += b; d = ROTR32(d^a,16); \
	c += d; b = ROTR32(b^c, 12); \
	idx = BLAKE2S_SIGMA_host[idx0][idx1 + 1]; a += key[idx]; \
	a += b; d = ROTR32(d^a,8); \
	c += d; b = ROTR32(b^c, 7); \
}

static void Blake2Shost(uint32_t * inout, const uint32_t * inkey)
{
	uint16 V;
	uint32_t idx;
	uint8 tmpblock;

	V.hi = BLAKE2S_IV_Vechost;
	V.lo = BLAKE2S_IV_Vechost;
	V.lo.s0 ^= 0x01012020;

	// Copy input block for later
	tmpblock = V.lo;

	V.hi.s4 ^= BLAKE2S_BLOCK_SIZE;

	for (int x = 0; x < 10; ++x)
	{
		BLAKE_Ghost(x, 0x00, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inkey);
		BLAKE_Ghost(x, 0x02, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inkey);
		BLAKE_Ghost(x, 0x04, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inkey);
		BLAKE_Ghost(x, 0x06, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inkey);
		BLAKE_Ghost(x, 0x08, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inkey);
		BLAKE_Ghost(x, 0x0A, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inkey);
		BLAKE_Ghost(x, 0x0C, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inkey);
		BLAKE_Ghost(x, 0x0E, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inkey);
	}

	V.lo ^= V.hi;
	V.lo ^= tmpblock;

	V.hi = BLAKE2S_IV_Vechost;
	tmpblock = V.lo;

	V.hi.s4 ^= 128;
	V.hi.s6 = ~V.hi.s6;

	for (int x = 0; x < 10; ++x)
	{
		BLAKE_Ghost(x, 0x00, V.lo.s0, V.lo.s4, V.hi.s0, V.hi.s4, inout);
		BLAKE_Ghost(x, 0x02, V.lo.s1, V.lo.s5, V.hi.s1, V.hi.s5, inout);
		BLAKE_Ghost(x, 0x04, V.lo.s2, V.lo.s6, V.hi.s2, V.hi.s6, inout);
		BLAKE_Ghost(x, 0x06, V.lo.s3, V.lo.s7, V.hi.s3, V.hi.s7, inout);
		BLAKE_Ghost(x, 0x08, V.lo.s0, V.lo.s5, V.hi.s2, V.hi.s7, inout);
		BLAKE_Ghost(x, 0x0A, V.lo.s1, V.lo.s6, V.hi.s3, V.hi.s4, inout);
		BLAKE_Ghost(x, 0x0C, V.lo.s2, V.lo.s7, V.hi.s0, V.hi.s5, inout);
		BLAKE_Ghost(x, 0x0E, V.lo.s3, V.lo.s4, V.hi.s1, V.hi.s6, inout);
	}

	V.lo ^= V.hi ^ tmpblock;

	((uint8*)inout)[0] = V.lo;
}

#if __CUDA_ARCH__ >= 700
#define TPB 64
#define TPB2 128
#define START_MINBLOCKS 4
#define CHACHA_MINBLOCKS 4
#define END_MINBLOCKS 8
#elif __CUDA_ARCH__ >= 500
#define TPB 32
#define TPB2 64
#define START_MINBLOCKS 2
#define CHACHA_MINBLOCKS 2
#define END_MINBLOCKS 4
#else
#define TPB 32
#define TPB2 64
#define START_MINBLOCKS 1
#define CHACHA_MINBLOCKS 1
#define END_MINBLOCKS 2
#endif

__global__
__launch_bounds__(TPB2, START_MINBLOCKS)
void neoscrypt_gpu_hash_start(const int stratum, const uint32_t startNonce)
{
	__shared__ uint32_t s_data[64 * TPB2];

	const uint32_t thread = (blockDim.x * blockIdx.x + threadIdx.x);
	const uint32_t nonce = startNonce + thread;
	const uint32_t ZNonce = (stratum) ? cuda_swab32(nonce) : nonce; //freaking morons !!!

	__syncthreads();
#if __CUDA_ARCH__ < 500
	fastkdf256_v1(thread, ZNonce, s_data);
#else
	fastkdf256_v2(thread, ZNonce, s_data);
#endif
}

__global__
__launch_bounds__(TPB, CHACHA_MINBLOCKS)
void neoscrypt_gpu_hash_chacha1()
{
	const uint32_t thread = (blockDim.y * blockIdx.x + threadIdx.y);
	const uint32_t threads = (gridDim.x * blockDim.y);
	const uint32_t shiftTr = 8U * thread;

	uint4 X[4];
	#pragma unroll 4
	for (int i = 0; i < 4; i++)
	{
		X[i].x = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + 0 * 4 + threadIdx.x);
		X[i].y = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + 1 * 4 + threadIdx.x);
		X[i].z = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + 2 * 4 + threadIdx.x);
		X[i].w = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + 3 * 4 + threadIdx.x);
	}

	#pragma unroll 1
	for (int i = 0; i < 128; i++)
	{
		uint32_t offset = 8U * (thread + threads * i);
		for (int j = 0; j < 4; j++)
			((uint4*)(W + offset))[j * 4 + threadIdx.x] = X[j];
		neoscrypt_chacha(X);
	}

	#pragma unroll 1
	for (int t = 0; t < 128; t++)
	{
		uint32_t offset = 8U * (thread + threads * (WarpShuffle(X[3].x, 0, 4) & 0x7F));
		for (int j = 0; j < 4; j++)
			X[j] ^= ((uint4*)(W + offset))[j * 4 + threadIdx.x];
		neoscrypt_chacha(X);
	}

	#pragma unroll
	for (int i = 0; i < 4; i++)
	{
		*((uint32_t*)&(Tr + shiftTr)[i * 2] + 0 * 4 + threadIdx.x) = X[i].x;
		*((uint32_t*)&(Tr + shiftTr)[i * 2] + 1 * 4 + threadIdx.x) = X[i].y;
		*((uint32_t*)&(Tr + shiftTr)[i * 2] + 2 * 4 + threadIdx.x) = X[i].z;
		*((uint32_t*)&(Tr + shiftTr)[i * 2] + 3 * 4 + threadIdx.x) = X[i].w;
	}
}

__global__
__launch_bounds__(TPB, CHACHA_MINBLOCKS)
void neoscrypt_gpu_hash_salsa1()
{
	const uint32_t thread = (blockDim.y * blockIdx.x + threadIdx.y);
	const uint32_t threads = (gridDim.x * blockDim.y);
	const uint32_t shiftTr = 8U * thread;

	uint4 Z[4];
	#pragma unroll 4
	for (int i = 0; i < 4; i++)
	{
		Z[i].x = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + ((0 + threadIdx.x) & 3) * 4 + threadIdx.x);
		Z[i].y = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + ((1 + threadIdx.x) & 3) * 4 + threadIdx.x);
		Z[i].z = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + ((2 + threadIdx.x) & 3) * 4 + threadIdx.x);
		Z[i].w = __ldg((uint32_t*)&(Input + shiftTr)[i * 2] + ((3 + threadIdx.x) & 3) * 4 + threadIdx.x);
	}

	#pragma unroll 1
	for (int i = 0; i < 128; i++)
	{
		uint32_t offset = 8U * (thread + threads * i);
		for (int j = 0; j < 4; j++)
			((uint4*)(W + offset))[j * 4 + threadIdx.x] = Z[j];
		neoscrypt_salsa(Z);
	}

	#pragma unroll 1
	for (int t = 0; t < 128; t++)
	{
		uint32_t offset = 8U * (thread + threads * (WarpShuffle(Z[3].x, 0, 4) & 0x7F));
		for (int j = 0; j < 4; j++)
			Z[j] ^= ((uint4*)(W + offset))[j * 4 + threadIdx.x];
		neoscrypt_salsa(Z);
	}
	#pragma unroll
	for (int i = 0; i < 4; i++)
	{
		*((uint32_t*)&(Tr2 + shiftTr)[i * 2] + ((0 + threadIdx.x) & 3) * 4 + threadIdx.x) = Z[i].x;
		*((uint32_t*)&(Tr2 + shiftTr)[i * 2] + ((1 + threadIdx.x) & 3) * 4 + threadIdx.x) = Z[i].y;
		*((uint32_t*)&(Tr2 + shiftTr)[i * 2] + ((2 + threadIdx.x) & 3) * 4 + threadIdx.x) = Z[i].z;
		*((uint32_t*)&(Tr2 + shiftTr)[i * 2] + ((3 + threadIdx.x) & 3) * 4 + threadIdx.x) = Z[i].w;
	}
}

__global__
__launch_bounds__(TPB2, END_MINBLOCKS)
void neoscrypt_gpu_hash_ending(const int stratum, const uint32_t startNonce, uint32_t *resNonces)
{
	__shared__ uint32_t s_data[64 * TPB2];

	const uint32_t thread = (blockDim.x * blockIdx.x + threadIdx.x);
	const uint32_t shiftTr = thread * 8U;
	const uint32_t nonce = startNonce + thread;
	const uint32_t ZNonce = (stratum) ? cuda_swab32(nonce) : nonce;

	__syncthreads();

	uint2x4 Z[8];
#if __CUDA_ARCH__ >= 500
	#pragma unroll 8
	for (int i = 0; i<8; i++)
		Z[i] = __ldg4(&(Tr2 + shiftTr)[i]) ^ __ldg4(&(Tr + shiftTr)[i]);
#else
	#pragma unroll 8
	for (int i = 0; i<8; i++) {
		uint2x4 t1 = *(const uint2x4*)&(Tr2 + shiftTr)[i];
		uint2x4 t2 = *(const uint2x4*)&(Tr + shiftTr)[i];
		Z[i].x = t1.x ^ t2.x;
		Z[i].y = t1.y ^ t2.y;
		Z[i].z = t1.z ^ t2.z;
		Z[i].w = t1.w ^ t2.w;
	}
#endif

#if __CUDA_ARCH__ < 500
	uint32_t outbuf = fastkdf32_v1(thread, ZNonce, (uint32_t*)Z, s_data);
#else
	uint32_t outbuf = fastkdf32_v3(thread, ZNonce, (uint32_t*)Z, s_data);
#endif

	if (outbuf <= c_target[1])
	{
		// Use atomicExch so that if two threads both satisfy the target
		// in the same batch, neither write silently stomps the other.
		// The first thread to arrive stores its nonce in resNonces[0]
		// and reads back UINT32_MAX (the sentinel set by cudaMemsetAsync).
		// Any subsequent thread atomically swaps its own nonce in and
		// receives the previous winner, which it stores as resNonces[1]
		// for the host-side validator to check.
		uint32_t tmp = atomicExch(&resNonces[0], nonce);
		if (tmp != UINT32_MAX)
			resNonces[1] = tmp;
	}
}

static __thread uint32_t *hash1 = NULL;
static __thread uint32_t *Trans1 = NULL;
static __thread uint32_t *Trans2 = NULL;
static __thread uint32_t *Trans3 = NULL;
static __thread cudaStream_t neoscrypt_stream[MAX_GPUS] = { NULL };

__host__
void neoscrypt_init(int thr_id, uint32_t threads)
{
	cuda_get_arch(thr_id);

	CUDA_SAFE_CALL(cudaStreamCreate(&neoscrypt_stream[thr_id]));

	CUDA_SAFE_CALL(cudaMalloc(&d_NNonce[thr_id], 2 * sizeof(uint32_t)));
	CUDA_SAFE_CALL(cudaMalloc(&hash1, 32 * 128 * sizeof(uint64_t) * threads));
	CUDA_SAFE_CALL(cudaMalloc(&Trans1, 32 * sizeof(uint64_t) * threads));
	CUDA_SAFE_CALL(cudaMalloc(&Trans2, 32 * sizeof(uint64_t) * threads));
	CUDA_SAFE_CALL(cudaMalloc(&Trans3, 32 * sizeof(uint64_t) * threads));

	CUDA_SAFE_CALL(cudaMemcpyToSymbol(W, &hash1, sizeof(uint2x4*), 0, cudaMemcpyHostToDevice));
	CUDA_SAFE_CALL(cudaMemcpyToSymbol(Tr, &Trans1, sizeof(uint2x4*), 0, cudaMemcpyHostToDevice));
	CUDA_SAFE_CALL(cudaMemcpyToSymbol(Tr2, &Trans2, sizeof(uint2x4*), 0, cudaMemcpyHostToDevice));
	CUDA_SAFE_CALL(cudaMemcpyToSymbol(Input, &Trans3, sizeof(uint2x4*), 0, cudaMemcpyHostToDevice));
}

__host__
void neoscrypt_free(int thr_id)
{
#if CUDART_VERSION >= 10000
	neoscrypt_graph_init();
	neoscrypt_graph_destroy(thr_id);
#endif
	cudaFree(d_NNonce[thr_id]);

	cudaFree(hash1);
	cudaFree(Trans1);
	cudaFree(Trans2);
	cudaFree(Trans3);

	if (neoscrypt_stream[thr_id]) {
		cudaStreamDestroy(neoscrypt_stream[thr_id]);
		neoscrypt_stream[thr_id] = NULL;
	}
}

/* -------------------------------------------------------------------------
 * CUDA Graph state for neoscrypt_hash_k4
 *
 * NeoScrypt's four-kernel pipeline incurs ~5–10 µs of CPU-side driver
 * overhead per kernel launch: argument marshalling, dependency tracking,
 * and command-queue submission.  With four kernel launches plus a memset
 * and a memcpy per scan window, this accumulates to ~30–60 µs of CPU work
 * at the start of every window.
 *
 * CUDA Graphs (CUDA 10.0+) allow capturing the entire sequence —
 * memset → hash_start → hash_salsa1 → hash_chacha1 → hash_ending → memcpy
 * — into a single cudaGraph_t object.  Subsequent launches replay the
 * captured graph with a single cudaGraphLaunch() call, bypassing most of
 * the per-kernel driver overhead.
 *
 * Parameters that change every scan window (startNounce, the nonce-buffer
 * reset value) are updated via cudaGraphExecKernelNodeSetParams() and
 * cudaGraphExecMemsetNodeSetParams() without re-capturing the graph.
 *
 * The graph is (re-)captured when:
 *   - It has never been captured for this thread (first call).
 *   - The 'stratum' flag changes (changes endianness of the nonce).
 *   - The grid/block dimensions change (intensity change at runtime).
 *
 * Requires CUDART_VERSION >= 10000.  Falls back to the original sequential
 * launch path when built against older toolkits.
 * ------------------------------------------------------------------------- */
#if CUDART_VERSION >= 10000

struct NeoScryptGraph {
	cudaGraph_t         graph;
	cudaGraphExec_t     instance;

	/* Handles to the four kernel nodes and the memset node, so we can
	 * update their parameters without re-capturing the graph.          */
	cudaGraphNode_t     node_memset;
	cudaGraphNode_t     node_start;
	cudaGraphNode_t     node_salsa;
	cudaGraphNode_t     node_chacha;
	cudaGraphNode_t     node_ending;
	cudaGraphNode_t     node_memcpy;

	/* Cached launch geometry for invalidation detection.               */
	uint32_t            last_threads;
	int                 last_stratum;
	bool                valid;
};

/* One graph state per GPU thread slot. */
static __thread NeoScryptGraph s_graph[MAX_GPUS];
static __thread bool           s_graph_init = false;

static void neoscrypt_graph_init(void)
{
	if (s_graph_init) return;
	for (int i = 0; i < MAX_GPUS; i++) {
		s_graph[i].graph       = nullptr;
		s_graph[i].instance    = nullptr;
		s_graph[i].valid       = false;
		s_graph[i].last_threads = 0;
		s_graph[i].last_stratum = -1;
	}
	s_graph_init = true;
}

static void neoscrypt_graph_destroy(int thr_id)
{
	NeoScryptGraph* g = &s_graph[thr_id];
	if (g->instance) { cudaGraphExecDestroy(g->instance); g->instance = nullptr; }
	if (g->graph)    { cudaGraphDestroy(g->graph);        g->graph    = nullptr; }
	g->valid = false;
}

/* Capture the entire kernel sequence for this set of parameters.
 * Called on first use and whenever geometry or stratum flag changes.    */
static bool neoscrypt_graph_capture(
	int thr_id, uint32_t threads, uint32_t startNounce,
	uint32_t *resNonces, bool stratum,
	const dim3& grid2, const dim3& block2,
	const dim3& grid3, const dim3& block3,
	cudaStream_t stream)
{
	neoscrypt_graph_destroy(thr_id);
	NeoScryptGraph* g = &s_graph[thr_id];

	/* Begin stream capture: all operations on 'stream' between Begin and
	 * End are recorded into a graph rather than executed immediately.    */
	if (cudaStreamBeginCapture(stream, cudaStreamCaptureModeGlobal) != cudaSuccess)
		return false;

	/* --- record the sequence --- */
	cudaMemsetAsync(d_NNonce[thr_id], 0xff, 2 * sizeof(uint32_t), stream);

	neoscrypt_gpu_hash_start <<<grid2, block2, 0, stream>>> ((int)stratum, startNounce);
	neoscrypt_gpu_hash_salsa1 <<<grid3, block3, 0, stream>>> ();
	neoscrypt_gpu_hash_chacha1 <<<grid3, block3, 0, stream>>> ();
	neoscrypt_gpu_hash_ending <<<grid2, block2, 0, stream>>> ((int)stratum, startNounce, d_NNonce[thr_id]);

	cudaMemcpyAsync(resNonces, d_NNonce[thr_id], 2 * sizeof(uint32_t),
	                cudaMemcpyDeviceToHost, stream);

	if (cudaStreamEndCapture(stream, &g->graph) != cudaSuccess)
		return false;

	/* Instantiate the graph — this JIT-compiles it for the current device. */
	if (cudaGraphInstantiate(&g->instance, g->graph, nullptr, nullptr, 0) != cudaSuccess) {
		cudaGraphDestroy(g->graph);
		g->graph = nullptr;
		return false;
	}

	/* Walk the graph node list to find each node by type and kernel name,
	 * so we can update them cheaply per scan window.                     */
	size_t num_nodes = 0;
	cudaGraphGetNodes(g->graph, nullptr, &num_nodes);
	cudaGraphNode_t* nodes = (cudaGraphNode_t*)malloc(num_nodes * sizeof(cudaGraphNode_t));
	if (!nodes) { neoscrypt_graph_destroy(thr_id); return false; }
	cudaGraphGetNodes(g->graph, nodes, &num_nodes);

	g->node_memset = nullptr;
	g->node_start  = nullptr;
	g->node_salsa  = nullptr;
	g->node_chacha = nullptr;
	g->node_ending = nullptr;
	g->node_memcpy = nullptr;

	for (size_t i = 0; i < num_nodes; i++) {
		cudaGraphNodeType t;
		cudaGraphNodeGetType(nodes[i], &t);
		if (t == cudaGraphNodeTypeMemset && !g->node_memset) {
			g->node_memset = nodes[i];
		} else if (t == cudaGraphNodeTypeMemcpy && !g->node_memcpy) {
			g->node_memcpy = nodes[i];
		} else if (t == cudaGraphNodeTypeKernel) {
			cudaKernelNodeParams kp;
			cudaGraphKernelNodeGetParams(nodes[i], &kp);
			if      (kp.func == (void*)neoscrypt_gpu_hash_start  && !g->node_start)  g->node_start  = nodes[i];
			else if (kp.func == (void*)neoscrypt_gpu_hash_salsa1 && !g->node_salsa)  g->node_salsa  = nodes[i];
			else if (kp.func == (void*)neoscrypt_gpu_hash_chacha1&& !g->node_chacha) g->node_chacha = nodes[i];
			else if (kp.func == (void*)neoscrypt_gpu_hash_ending && !g->node_ending) g->node_ending = nodes[i];
		}
	}
	free(nodes);

	g->last_threads = threads;
	g->last_stratum = (int)stratum;
	g->valid        = true;
	return true;
}

#endif /* CUDART_VERSION >= 10000 */

__host__
void neoscrypt_hash_k4(int thr_id, uint32_t threads, uint32_t startNounce, uint32_t *resNonces, bool stratum)
{
	cudaStream_t stream = neoscrypt_stream[thr_id];

	const int threadsperblock2 = TPB2;
	dim3 grid2((threads + threadsperblock2 - 1) / threadsperblock2);
	dim3 block2(threadsperblock2);

	const int threadsperblock = TPB;
	dim3 grid3((threads * 4 + threadsperblock - 1) / threadsperblock);
	dim3 block3(4, threadsperblock >> 2);

#if CUDART_VERSION >= 10000
	neoscrypt_graph_init();
	NeoScryptGraph* g = &s_graph[thr_id];

	/* Invalidate and re-capture if geometry or stratum flag changed. */
	bool need_capture = !g->valid
	                 || g->last_threads != threads
	                 || g->last_stratum != (int)stratum;

	if (need_capture) {
		if (!neoscrypt_graph_capture(thr_id, threads, startNounce, resNonces,
		                             stratum, grid2, block2, grid3, block3, stream)) {
			/* Graph capture failed — fall through to sequential launch. */
			applog(LOG_DEBUG, "GPU #%d: NeoScrypt CUDA Graph capture failed, "
			       "using sequential launch", device_map[thr_id]);
			g->valid = false;
			goto sequential_launch;
		}
		applog(LOG_DEBUG, "GPU #%d: NeoScrypt CUDA Graph captured "
		       "(threads=%u, stratum=%d)",
		       device_map[thr_id], threads, (int)stratum);
	}

	if (g->valid) {
		/* Update only the nodes whose parameters change each scan window.
		 *
		 * node_memset: reset value is always 0xff, size always 8 bytes —
		 *   unchanged, no update needed.
		 *
		 * node_start: startNounce changes every window. */
		if (g->node_start) {
			cudaKernelNodeParams kp;
			cudaGraphKernelNodeGetParams(g->node_start, &kp);
			/* kp.kernelParams[1] is startNounce (second arg). */
			*(uint32_t*)kp.kernelParams[1] = startNounce;
			cudaGraphExecKernelNodeSetParams(g->instance, g->node_start, &kp);
		}
		/* node_ending: startNounce changes every window. */
		if (g->node_ending) {
			cudaKernelNodeParams kp;
			cudaGraphKernelNodeGetParams(g->node_ending, &kp);
			*(uint32_t*)kp.kernelParams[1] = startNounce;
			cudaGraphExecKernelNodeSetParams(g->instance, g->node_ending, &kp);
		}

		/* Launch the entire captured sequence in one driver call. */
		CUDA_SAFE_CALL(cudaGraphLaunch(g->instance, stream));
		CUDA_SAFE_CALL(cudaStreamSynchronize(stream));
		return;
	}

sequential_launch:
#endif /* CUDART_VERSION >= 10000 */

	// Hoist the stream handle so the nonce-buffer reset can be queued
	// asynchronously. The old cudaMemset() was an implicit synchronisation
	// barrier: it forced the CPU to wait for all outstanding GPU work before
	// the reset was issued, then the kernel chain could not start until the
	// blocking memset returned. Using cudaMemsetAsync() on the same stream
	// keeps the entire sequence — reset → hash_start → salsa → chacha →
	// ending → memcpy — as a single, uninterrupted command queue.
	CUDA_SAFE_CALL(cudaMemsetAsync(d_NNonce[thr_id], 0xff, 2 * sizeof(uint32_t), stream));

	neoscrypt_gpu_hash_start <<<grid2, block2, 0, stream>>> (stratum, startNounce);

	neoscrypt_gpu_hash_salsa1 <<<grid3, block3, 0, stream>>> ();
	neoscrypt_gpu_hash_chacha1 <<<grid3, block3, 0, stream>>> ();

	neoscrypt_gpu_hash_ending <<<grid2, block2, 0, stream>>> (stratum, startNounce, d_NNonce[thr_id]);

	CUDA_SAFE_CALL(cudaMemcpyAsync(resNonces, d_NNonce[thr_id], 2 * sizeof(uint32_t), cudaMemcpyDeviceToHost, stream));
	CUDA_SAFE_CALL(cudaStreamSynchronize(stream));
}

__host__
void neoscrypt_setBlockTarget(uint32_t* const pdata, uint32_t* const target)
{
	uint32_t PaddedMessage[64];
	uint32_t input[16], key[16] = { 0 };

	for (int i = 0; i < 19; i++)
	{
		PaddedMessage[i] = pdata[i];
		PaddedMessage[i + 20] = pdata[i];
		PaddedMessage[i + 40] = pdata[i];
	}
	for (int i = 0; i<4; i++)
		PaddedMessage[i + 60] = pdata[i];

	PaddedMessage[19] = 0;
	PaddedMessage[39] = 0;
	PaddedMessage[59] = 0;

	((uint16*)input)[0] = ((uint16*)pdata)[0];
	((uint8*)key)[0] = ((uint8*)pdata)[0];

	Blake2Shost(input, key);

	cudaMemcpyToSymbol(input_init, input, 64, 0, cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(key_init, key, 64, 0, cudaMemcpyHostToDevice);

	cudaMemcpyToSymbol(c_target, &target[6], 2 * sizeof(uint32_t), 0, cudaMemcpyHostToDevice);
	cudaMemcpyToSymbol(c_data, PaddedMessage, 64 * sizeof(uint32_t), 0, cudaMemcpyHostToDevice);
	CUDA_SAFE_CALL(cudaGetLastError());
}

