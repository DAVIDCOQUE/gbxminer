/**
 * kheavyhash_cuda.cu
 *
 * CUDA kernel for kHeavyHash (Kaspa proof-of-work).
 *
 * Algorithm
 * ---------
 * Each thread tests one nonce.  The 72-byte block header has the 8-byte
 * nonce at bytes [32..39].  The GPU writes the nonce into a copy of the
 * header and runs:
 *
 *   1. pre_hash  = keccak256(header[0:72])
 *   2. M         = 64x64 matrix seeded from pre_hash via xoshiro256**
 *   3. product_i = Σ_j M[i][j] * pre_hash_nibble[j]   (mod 2^17)
 *   4. heavyhash = keccak256(pre_hash XOR product)
 *   5. if heavyhash < target → record nonce in d_result
 *
 * The matrix is computed on-chip; no DAG or global-memory precompute
 * step is needed.  This makes kHeavyHash exceptionally cheap to start.
 *
 * Keccak-256 is implemented as the raw Keccak permutation (not SHA3-256;
 * Kaspa uses the original Keccak without the domain-separation suffix).
 *
 * References
 * ----------
 * Kaspa KIP-0001: https://github.com/kaspanet/kips/blob/main/kip-0001.md
 * xoshiro256**: https://prng.di.unimi.it/xoshiro256starstar.c
 */

#include <stdint.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include "kheavyhash_cuda.h"

/* ------------------------------------------------------------------ */
/*  Result buffer (one per GPU thread)                                  */
/* ------------------------------------------------------------------ */

static uint32_t *d_result[16] = { nullptr };
static bool      s_init[16]   = { false };

void kheavyhash_cuda_init(int thr_id)
{
    if (s_init[thr_id]) return;
    cudaMalloc((void**)&d_result[thr_id], 2 * sizeof(uint32_t));
    s_init[thr_id] = true;
}

void kheavyhash_cuda_free(int thr_id)
{
    if (!s_init[thr_id]) return;
    cudaFree(d_result[thr_id]);
    d_result[thr_id] = nullptr;
    s_init[thr_id]   = false;
}

/* ------------------------------------------------------------------ */
/*  Keccak-256 (original, no domain separation — as used by Kaspa)     */
/* ------------------------------------------------------------------ */

__device__ __constant__
static const uint64_t keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808AULL, 0x8000000080008000ULL,
    0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL,
};

__device__ static inline uint64_t rotl64(uint64_t x, int n)
{
    return (x << n) | (x >> (64 - n));
}

__device__ static void keccak_f1600(uint64_t st[25])
{
    for (int r = 0; r < 24; r++) {
        /* θ */
        uint64_t bc[5];
        bc[0] = st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20];
        bc[1] = st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21];
        bc[2] = st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22];
        bc[3] = st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23];
        bc[4] = st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24];
        for (int i = 0; i < 5; i++) {
            uint64_t t = bc[(i+4)%5] ^ rotl64(bc[(i+1)%5], 1);
            for (int j = 0; j < 25; j += 5) st[j+i] ^= t;
        }
        /* ρ and π */
        uint64_t t = st[1];
        const int rho[24] = {1,62,28,27,36,44,6,55,20,3,10,43,25,39,41,45,15,21,8,18,2,61,56,14};
        const int pi[24]  = {10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1};
        for (int i = 0; i < 24; i++) {
            uint64_t tmp = st[pi[i]];
            st[pi[i]] = rotl64(t, rho[i]);
            t = tmp;
        }
        /* χ */
        for (int j = 0; j < 25; j += 5) {
            uint64_t b[5];
            for (int i = 0; i < 5; i++) b[i] = st[j+i];
            for (int i = 0; i < 5; i++)
                st[j+i] = b[i] ^ ((~b[(i+1)%5]) & b[(i+2)%5]);
        }
        /* ι */
        st[0] ^= keccak_rc[r];
    }
}

/**
 * keccak256_device — raw Keccak-256 (no SHA3 padding suffix).
 * Kaspa uses 0x01 padding (original Keccak), not 0x06 (SHA3).
 */
__device__ static void keccak256_device(const uint8_t *in, uint32_t len,
                                         uint8_t out[32])
{
    uint64_t st[25] = { 0 };
    const uint32_t rate = 136; /* 1088 bits / 8 = 136 bytes for keccak-256 */

    uint32_t i = 0;
    while (i + rate <= len) {
        for (uint32_t j = 0; j < rate/8; j++)
            st[j] ^= ((const uint64_t*)in)[(i/8)+j];
        i += rate;
        keccak_f1600(st);
    }
    /* padding */
    uint8_t last[136] = { 0 };
    uint32_t rem = len - i;
    for (uint32_t j = 0; j < rem; j++) last[j] = in[i+j];
    last[rem]      = 0x01;   /* original Keccak padding */
    last[rate - 1] ^= 0x80;
    for (uint32_t j = 0; j < rate/8; j++)
        st[j] ^= ((uint64_t*)last)[j];
    keccak_f1600(st);

    for (uint32_t j = 0; j < 4; j++)
        ((uint64_t*)out)[j] = st[j];
}

/* ------------------------------------------------------------------ */
/*  xoshiro256** PRNG for matrix generation                             */
/* ------------------------------------------------------------------ */

__device__ static inline uint64_t xoshiro_rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

__device__ static inline uint64_t xoshiro_next(uint64_t s[4])
{
    uint64_t result = xoshiro_rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1];
    s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t;
    s[3] = xoshiro_rotl(s[3], 45);
    return result;
}

/* ------------------------------------------------------------------ */
/*  kHeavyHash kernel                                                   */
/* ------------------------------------------------------------------ */

__global__ void kheavyhash_kernel(const uint32_t *header_in,  /* 18 u32 */
                                   uint64_t        target,
                                   uint32_t        start_nonce,
                                   uint32_t       *d_result)
{
    const uint32_t nonce = start_nonce + blockIdx.x * blockDim.x + threadIdx.x;

    /* --- Step 0: Build header copy with this nonce ------------------- */
    uint8_t hdr[72];
    for (int i = 0; i < 18; i++)
        ((uint32_t*)hdr)[i] = header_in[i];
    /* Nonce occupies bytes 32..39 (two little-endian uint32) */
    ((uint32_t*)hdr)[8]  = nonce;
    ((uint32_t*)hdr)[9]  = 0;   /* upper 32 bits of nonce (we test 32-bit nonces) */

    /* --- Step 1: pre_hash = keccak256(header) ----------------------- */
    uint8_t pre_hash[32];
    keccak256_device(hdr, 72, pre_hash);

    /* --- Step 2: Derive 64x64 matrix from pre_hash via xoshiro256** - */
    /* Seed state from the four 64-bit words of pre_hash */
    uint64_t xstate[4];
    for (int i = 0; i < 4; i++)
        xstate[i] = ((uint64_t*)pre_hash)[i];

    /*
     * The matrix elements are 4-bit nibbles (0..15); the product uses
     * integer arithmetic mod 2^17.  We store the full uint16_t matrix row
     * in shared memory to allow fast column reads.
     * Matrix M[i][j] = 4-bit nibble from xoshiro stream.
     */
    __shared__ uint8_t M[64][64];   /* 4 KiB shared memory */

    /* Each thread fills one 8-column chunk of its assigned row          */
    /* (64 threads/block: thread t fills rows t, t+blockDim, … — but    */
    /* since blockDim is set to 64 each thread fills exactly its row.)   */
    {
        int row = threadIdx.x;
        /* Generate 32 bytes (256 bits) from xoshiro to get 64 nibbles */
        for (int col = 0; col < 64; col += 16) {
            uint64_t rnd = xoshiro_next(xstate);
            for (int k = 0; k < 16 && (col+k) < 64; k++)
                M[row][col+k] = (rnd >> (4*k)) & 0xF;
        }
    }
    __syncthreads();

    /* --- Step 3: product_i = Σ_j M[i][j] * nibble_j(pre_hash) ------- */
    /*
     * pre_hash nibbles: treat the 32-byte hash as 64 nibbles
     *   nibble[j] = (pre_hash[j/2] >> ((j%2)*4)) & 0xF
     */
    uint8_t ph_nibbles[64];
    for (int j = 0; j < 64; j++)
        ph_nibbles[j] = (pre_hash[j/2] >> ((j%2)*4)) & 0xF;

    uint8_t product[32] = { 0 };   /* 64 output nibbles stored as 32 bytes */
    for (int i = 0; i < 64; i++) {
        uint32_t acc = 0;
        for (int j = 0; j < 64; j++)
            acc += (uint32_t)M[i][j] * (uint32_t)ph_nibbles[j];
        acc &= 0x1FFFF;  /* mod 2^17 */
        /* Store as nibble in product */
        uint8_t nib = (uint8_t)(acc & 0xF);
        if (i & 1) product[i/2] |= (nib << 4);
        else        product[i/2]  =  nib;
    }

    /* --- Step 4: heavyhash = keccak256(pre_hash XOR product) --------- */
    uint8_t xored[32];
    for (int i = 0; i < 32; i++) xored[i] = pre_hash[i] ^ product[i];

    uint8_t heavyhash[32];
    keccak256_device(xored, 32, heavyhash);

    /* --- Step 5: Check against target (first 8 bytes, little-endian) - */
    uint64_t h64;
    for (int i = 0; i < 8; i++)
        ((uint8_t*)&h64)[i] = heavyhash[i];

    if (h64 < target) {
        /* Record: [0] = nonce, [1] = found flag */
        atomicExch(&d_result[1], 1);
        atomicExch(&d_result[0], nonce);
    }
}

/* ------------------------------------------------------------------ */
/*  Host-side launch wrapper                                            */
/* ------------------------------------------------------------------ */

int kheavyhash_run(int thr_id,
                   const uint32_t *h_header,
                   uint64_t        target,
                   uint32_t        start_nonce,
                   uint32_t        nonces_done,
                   uint32_t       *found_nonce)
{
    const uint32_t threads_per_block = 64;
    const uint32_t blocks = (nonces_done + threads_per_block - 1)
                             / threads_per_block;

    /* Upload header */
    uint32_t *d_header = nullptr;
    cudaMalloc((void**)&d_header, 18 * sizeof(uint32_t));
    cudaMemcpy(d_header, h_header, 18 * sizeof(uint32_t),
               cudaMemcpyHostToDevice);

    /* Clear result */
    cudaMemset(d_result[thr_id], 0, 2 * sizeof(uint32_t));

    kheavyhash_kernel<<<blocks, threads_per_block>>>(
        d_header, target, start_nonce, d_result[thr_id]);

    cudaFree(d_header);
    cudaDeviceSynchronize();

    uint32_t h_result[2] = { 0, 0 };
    cudaMemcpy(h_result, d_result[thr_id], 2 * sizeof(uint32_t),
               cudaMemcpyDeviceToHost);

    if (h_result[1]) {
        *found_nonce = h_result[0];
        return 1;
    }
    return 0;
}
