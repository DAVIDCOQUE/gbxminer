#pragma once
/**
 * etchash_keccak.cuh
 *
 * Keccak-f[1600] device functions for the ETCHash search and DAG kernels.
 *
 * Adapted from etcminer keccak.cuh (GPL-3.0).
 * Key changes vs upstream:
 *   - All SHFL → ETC_SHFL  (defined in etchash_cuda_miner_kernel_globals.h)
 *   - All LDG  → ETC_LDG
 *   - Removed dependency on etcminer cuda_helper.h; ROL2/ROR8 defined inline.
 */

/* ------------------------------------------------------------------ */
/*  uint2 rotate helpers                                                */
/* ------------------------------------------------------------------ */
DEV_INLINE uint2 ROL2(const uint2 a, const int offset)
{
    uint2 result;
    if (offset < 32) {
        asm("shf.l.wrap.b32 %0, %1, %2, %3;"
            : "=r"(result.x) : "r"(a.x), "r"(a.y), "r"(offset));
        asm("shf.l.wrap.b32 %0, %1, %2, %3;"
            : "=r"(result.y) : "r"(a.y), "r"(a.x), "r"(offset));
    } else {
        asm("shf.l.wrap.b32 %0, %1, %2, %3;"
            : "=r"(result.x) : "r"(a.y), "r"(a.x), "r"(offset - 32));
        asm("shf.l.wrap.b32 %0, %1, %2, %3;"
            : "=r"(result.y) : "r"(a.x), "r"(a.y), "r"(offset - 32));
    }
    return result;
}

DEV_INLINE uint2 ROL8(const uint2 a)
{
    /* byte rotate left 8 == bit rotate left 8 within the 64-bit word */
    uint2 result;
    asm("prmt.b32 %0, %1, %2, 0x6543;" : "=r"(result.x) : "r"(a.x), "r"(a.y));
    asm("prmt.b32 %0, %1, %2, 0x2107;" : "=r"(result.y) : "r"(a.x), "r"(a.y));
    return result;
}

DEV_INLINE uint2 ROR8(const uint2 a)
{
    uint2 result;
    asm("prmt.b32 %0, %1, %2, 0x0765;" : "=r"(result.x) : "r"(a.x), "r"(a.y));
    asm("prmt.b32 %0, %1, %2, 0x4321;" : "=r"(result.y) : "r"(a.x), "r"(a.y));
    return result;
}

/* ------------------------------------------------------------------ */
/*  Vectorise / devectorise 64-bit ↔ uint2                             */
/* ------------------------------------------------------------------ */
DEV_INLINE uint2 vectorize(const uint64_t x)
{
    uint2 result;
    asm("mov.b64 {%0,%1},%2;" : "=r"(result.x), "=r"(result.y) : "l"(x));
    return result;
}

DEV_INLINE uint64_t devectorize(const uint2 x)
{
    uint64_t result;
    asm("mov.b64 %0,{%1,%2};" : "=l"(result) : "r"(x.x), "r"(x.y));
    return result;
}

DEV_INLINE void vectorize2(uint2 v1, uint2 v2, uint4& result)
{
    result.x = v1.x; result.y = v1.y;
    result.z = v2.x; result.w = v2.y;
}

DEV_INLINE void devectorize2(uint4 src, uint2& v1, uint2& v2)
{
    v1.x = src.x; v1.y = src.y;
    v2.x = src.z; v2.y = src.w;
}

DEV_INLINE uint64_t cuda_swab64(const uint64_t x)
{
    uint64_t result;
    asm("{ .reg .b32 r0,r1;\n\t"
        "  mov.b64 {r0,r1}, %1;\n\t"
        "  prmt.b32 r0, r0, 0, 0x0123;\n\t"
        "  prmt.b32 r1, r1, 0, 0x0123;\n\t"
        "  mov.b64 %0, {r1,r0}; }"
        : "=l"(result) : "l"(x));
    return result;
}

/* ------------------------------------------------------------------ */
/*  Keccak round constants                                              */
/* ------------------------------------------------------------------ */
__device__ __constant__ uint2 const keccak_rc[24] = {
    {0x00000001, 0x00000000}, {0x00008082, 0x00000000},
    {0x0000808a, 0x80000000}, {0x80008000, 0x80000000},
    {0x0000808b, 0x00000000}, {0x80000001, 0x00000000},
    {0x80008081, 0x80000000}, {0x00008009, 0x80000000},
    {0x0000008a, 0x00000000}, {0x00000088, 0x00000000},
    {0x80008009, 0x00000000}, {0x8000000a, 0x00000000},
    {0x8000808b, 0x00000000}, {0x0000008b, 0x80000000},
    {0x00008089, 0x80000000}, {0x00008003, 0x80000000},
    {0x00008002, 0x80000000}, {0x00000080, 0x80000000},
    {0x0000800a, 0x00000000}, {0x8000000a, 0x80000000},
    {0x80008081, 0x80000000}, {0x00008080, 0x80000000},
    {0x80000001, 0x00000000}, {0x80008008, 0x80000000}
};

/* ------------------------------------------------------------------ */
/*  2-way, 3-way and 5-way XOR (PTX lop3 on sm_50+)                   */
/* ------------------------------------------------------------------ */
DEV_INLINE uint2 xor2(const uint2 a, const uint2 b)
{
#if __CUDA_ARCH__ >= 500
    uint2 r;
    asm("lop3.b32 %0,%2,%3,%4,0x96;\n\t"
        "lop3.b32 %1,%5,%6,%7,0x96;"
        : "=r"(r.x), "=r"(r.y)
        : "r"(a.x), "r"(b.x), "r"(0),
          "r"(a.y), "r"(b.y), "r"(0));
    return r;
#else
    return make_uint2(a.x ^ b.x, a.y ^ b.y);
#endif
}

DEV_INLINE uint2 not2(const uint2 a)
{
    return make_uint2(~a.x, ~a.y);
}

DEV_INLINE uint2 xor3(const uint2 a, const uint2 b, const uint2 c)
{
#if __CUDA_ARCH__ >= 500
    uint2 r;
    asm("lop3.b32 %0,%2,%3,%4,0x96;\n\t"
        "lop3.b32 %1,%5,%6,%7,0x96;"
        : "=r"(r.x), "=r"(r.y)
        : "r"(a.x), "r"(b.x), "r"(c.x),
          "r"(a.y), "r"(b.y), "r"(c.y));
    return r;
#else
    return xor2(xor2(a, b), c);
#endif
}

DEV_INLINE uint2 xor5(const uint2 a, const uint2 b, const uint2 c,
                      const uint2 d, const uint2 e)
{
    return xor3(xor3(a, b, c), d, e);
}

DEV_INLINE uint2 chi(const uint2 a, const uint2 b, const uint2 c)
{
#if __CUDA_ARCH__ >= 500
    uint2 r;
    /* 0xD2 = a ^ ((~b) & c) */
    asm("lop3.b32 %0,%2,%3,%4,0xD2;\n\t"
        "lop3.b32 %1,%5,%6,%7,0xD2;"
        : "=r"(r.x), "=r"(r.y)
        : "r"(a.x), "r"(b.x), "r"(c.x),
          "r"(a.y), "r"(b.y), "r"(c.y));
    return r;
#else
    return xor2(a, make_uint2((~b.x & c.x), (~b.y & c.y)));
#endif
}

/* ------------------------------------------------------------------ */
/*  keccak_f1600_init: absorb header and nonce, run 23+partial rounds   */
/* ------------------------------------------------------------------ */
DEV_INLINE void keccak_f1600_init(uint2* state)
{
    uint2 s[25], t[5], u, v;
    const uint2 z = make_uint2(0, 0);

    devectorize2(d_etc_header.uint4s[0], s[0], s[1]);
    devectorize2(d_etc_header.uint4s[1], s[2], s[3]);
    s[4] = state[4];            /* nonce injected by caller */
    s[5] = make_uint2(1, 0);    /* SHA3-512 padding        */
    s[6] = z; s[7] = z;
    s[8] = make_uint2(0, 0x80000000);
    for (uint32_t i = 9; i < 25; i++) s[i] = z;

    /* theta */
    t[0].x = s[0].x ^ s[5].x;  t[0].y = s[0].y;
    t[1] = s[1];
    t[2] = s[2];
    t[3].x = s[3].x;            t[3].y = s[3].y ^ s[8].y;
    t[4] = s[4];

#define ETC_THETA_STEP(dst_lo, dst_hi, src_a, src_b) \
    u = xor2(t[dst_lo], ROL2(t[dst_hi], 1)); \
    s[src_a].x ^= u.x; s[src_a].y ^= u.y; \
    s[src_b].x ^= u.x; s[src_b].y ^= u.y;

    u = xor2(t[4], ROL2(t[1], 1));
    s[0].x ^= u.x; s[0].y ^= u.y; s[5].x ^= u.x; s[5].y ^= u.y;
    s[10].x ^= u.x; s[10].y ^= u.y; s[15].x ^= u.x; s[15].y ^= u.y;
    s[20].x ^= u.x; s[20].y ^= u.y;
    u = xor2(t[0], ROL2(t[2], 1));
    s[1].x ^= u.x; s[1].y ^= u.y; s[6].x ^= u.x; s[6].y ^= u.y;
    s[11].x ^= u.x; s[11].y ^= u.y; s[16].x ^= u.x; s[16].y ^= u.y;
    s[21].x ^= u.x; s[21].y ^= u.y;
    u = xor2(t[1], ROL2(t[3], 1));
    s[2].x ^= u.x; s[2].y ^= u.y; s[7].x ^= u.x; s[7].y ^= u.y;
    s[12].x ^= u.x; s[12].y ^= u.y; s[17].x ^= u.x; s[17].y ^= u.y;
    s[22].x ^= u.x; s[22].y ^= u.y;
    u = xor2(t[2], ROL2(t[4], 1));
    s[3].x ^= u.x; s[3].y ^= u.y; s[8].x ^= u.x; s[8].y ^= u.y;
    s[13].x ^= u.x; s[13].y ^= u.y; s[18].x ^= u.x; s[18].y ^= u.y;
    s[23].x ^= u.x; s[23].y ^= u.y;
    u = xor2(t[3], ROL2(t[0], 1));
    s[4].x ^= u.x; s[4].y ^= u.y; s[9].x ^= u.x; s[9].y ^= u.y;
    s[14].x ^= u.x; s[14].y ^= u.y; s[19].x ^= u.x; s[19].y ^= u.y;
    s[24].x ^= u.x; s[24].y ^= u.y;

    /* rho-pi */
    u = s[1];
    s[1]=ROL2(s[6],44); s[6]=ROL2(s[9],20); s[9]=ROL2(s[22],61);
    s[22]=ROL2(s[14],39); s[14]=ROL2(s[20],18); s[20]=ROL2(s[2],62);
    s[2]=ROL2(s[12],43); s[12]=ROL2(s[13],25); s[13]=ROL8(s[19]);
    s[19]=ROR8(s[23]); s[23]=ROL2(s[15],41); s[15]=ROL2(s[4],27);
    s[4]=ROL2(s[24],14); s[24]=ROL2(s[21],2); s[21]=ROL2(s[8],55);
    s[8]=ROL2(s[16],45); s[16]=ROL2(s[5],36); s[5]=ROL2(s[3],28);
    s[3]=ROL2(s[18],21); s[18]=ROL2(s[17],15); s[17]=ROL2(s[11],10);
    s[11]=ROL2(s[7],6); s[7]=ROL2(s[10],3); s[10]=ROL2(u,1);

    /* chi + iota round 0 */
#define ETC_CHI5(base) \
    u=s[base]; v=s[(base)+1]; \
    s[base]  =chi(s[base],  s[(base)+1],s[(base)+2]); \
    s[(base)+1]=chi(s[(base)+1],s[(base)+2],s[(base)+3]); \
    s[(base)+2]=chi(s[(base)+2],s[(base)+3],s[(base)+4]); \
    s[(base)+3]=chi(s[(base)+3],s[(base)+4],u); \
    s[(base)+4]=chi(s[(base)+4],u,v);

    ETC_CHI5(0) ETC_CHI5(5) ETC_CHI5(10) ETC_CHI5(15) ETC_CHI5(20)
    s[0].x ^= keccak_rc[0].x; s[0].y ^= keccak_rc[0].y;

    for (int i = 1; i < 23; i++) {
        t[0]=xor5(s[0],s[5],s[10],s[15],s[20]);
        t[1]=xor5(s[1],s[6],s[11],s[16],s[21]);
        t[2]=xor5(s[2],s[7],s[12],s[17],s[22]);
        t[3]=xor5(s[3],s[8],s[13],s[18],s[23]);
        t[4]=xor5(s[4],s[9],s[14],s[19],s[24]);
        u=xor2(t[4],ROL2(t[1],1));
        s[0].x ^= u.x; s[0].y ^= u.y; s[5].x ^= u.x; s[5].y ^= u.y;
        s[10].x ^= u.x; s[10].y ^= u.y; s[15].x ^= u.x; s[15].y ^= u.y;
        s[20].x ^= u.x; s[20].y ^= u.y;
        u=xor2(t[0],ROL2(t[2],1));
        s[1].x ^= u.x; s[1].y ^= u.y; s[6].x ^= u.x; s[6].y ^= u.y;
        s[11].x ^= u.x; s[11].y ^= u.y; s[16].x ^= u.x; s[16].y ^= u.y;
        s[21].x ^= u.x; s[21].y ^= u.y;
        u=xor2(t[1],ROL2(t[3],1));
        s[2].x ^= u.x; s[2].y ^= u.y; s[7].x ^= u.x; s[7].y ^= u.y;
        s[12].x ^= u.x; s[12].y ^= u.y; s[17].x ^= u.x; s[17].y ^= u.y;
        s[22].x ^= u.x; s[22].y ^= u.y;
        u=xor2(t[2],ROL2(t[4],1));
        s[3].x ^= u.x; s[3].y ^= u.y; s[8].x ^= u.x; s[8].y ^= u.y;
        s[13].x ^= u.x; s[13].y ^= u.y; s[18].x ^= u.x; s[18].y ^= u.y;
        s[23].x ^= u.x; s[23].y ^= u.y;
        u=xor2(t[3],ROL2(t[0],1));
        s[4].x ^= u.x; s[4].y ^= u.y; s[9].x ^= u.x; s[9].y ^= u.y;
        s[14].x ^= u.x; s[14].y ^= u.y; s[19].x ^= u.x; s[19].y ^= u.y;
        s[24].x ^= u.x; s[24].y ^= u.y;
        u=s[1];
        s[1]=ROL2(s[6],44); s[6]=ROL2(s[9],20); s[9]=ROL2(s[22],61);
        s[22]=ROL2(s[14],39); s[14]=ROL2(s[20],18); s[20]=ROL2(s[2],62);
        s[2]=ROL2(s[12],43); s[12]=ROL2(s[13],25); s[13]=ROL8(s[19]);
        s[19]=ROR8(s[23]); s[23]=ROL2(s[15],41); s[15]=ROL2(s[4],27);
        s[4]=ROL2(s[24],14); s[24]=ROL2(s[21],2); s[21]=ROL2(s[8],55);
        s[8]=ROL2(s[16],45); s[16]=ROL2(s[5],36); s[5]=ROL2(s[3],28);
        s[3]=ROL2(s[18],21); s[18]=ROL2(s[17],15); s[17]=ROL2(s[11],10);
        s[11]=ROL2(s[7],6); s[7]=ROL2(s[10],3); s[10]=ROL2(u,1);
        ETC_CHI5(0) ETC_CHI5(5) ETC_CHI5(10) ETC_CHI5(15) ETC_CHI5(20)
        s[0].x ^= keccak_rc[i].x; s[0].y ^= keccak_rc[i].y;
    }
    /* partial final theta+rho-pi before the DAG loop */
    t[0]=xor5(s[0],s[5],s[10],s[15],s[20]);
    t[1]=xor5(s[1],s[6],s[11],s[16],s[21]);
    t[2]=xor5(s[2],s[7],s[12],s[17],s[22]);
    t[3]=xor5(s[3],s[8],s[13],s[18],s[23]);
    t[4]=xor5(s[4],s[9],s[14],s[19],s[24]);
    u=xor2(t[4],ROL2(t[1],1)); s[0].x ^= u.x; s[0].y ^= u.y; s[10].x ^= u.x; s[10].y ^= u.y;
    u=xor2(t[0],ROL2(t[2],1)); s[6].x ^= u.x; s[6].y ^= u.y; s[16].x ^= u.x; s[16].y ^= u.y;
    u=xor2(t[1],ROL2(t[3],1)); s[12].x ^= u.x; s[12].y ^= u.y; s[22].x ^= u.x; s[22].y ^= u.y;
    u=xor2(t[2],ROL2(t[4],1)); s[3].x ^= u.x; s[3].y ^= u.y; s[18].x ^= u.x; s[18].y ^= u.y;
    u=xor2(t[3],ROL2(t[0],1)); s[9].x ^= u.x; s[9].y ^= u.y; s[24].x ^= u.x; s[24].y ^= u.y;
    u=s[1];
    s[1]=ROL2(s[6],44); s[6]=ROL2(s[9],20); s[9]=ROL2(s[22],61);
    s[2]=ROL2(s[12],43); s[4]=ROL2(s[24],14); s[8]=ROL2(s[16],45);
    s[5]=ROL2(s[3],28); s[3]=ROL2(s[18],21); s[7]=ROL2(s[10],3);
    u=s[0]; v=s[1];
    s[0]=chi(s[0],s[1],s[2]); s[1]=chi(s[1],s[2],s[3]);
    s[2]=chi(s[2],s[3],s[4]); s[3]=chi(s[3],s[4],u);
    s[4]=chi(s[4],u,v);
    s[5]=chi(s[5],s[6],s[7]); s[6]=chi(s[6],s[7],s[8]);
    s[7]=chi(s[7],s[8],s[9]);
    s[0].x ^= keccak_rc[23].x; s[0].y ^= keccak_rc[23].y;

    for (int i = 0; i < 12; i++) state[i] = s[i];
}

/* ------------------------------------------------------------------ */
/*  keccak_f1600_final: absorb mix, produce 64-bit result               */
/* ------------------------------------------------------------------ */
DEV_INLINE uint64_t keccak_f1600_final(uint2* state)
{
    uint2 s[25], t[5], u, v;
    const uint2 z = make_uint2(0, 0);

    for (int i = 0; i < 12; i++) s[i] = state[i];
    s[12]=make_uint2(1,0); s[13]=z; s[14]=z; s[15]=z;
    s[16]=make_uint2(0,0x80000000);
    for (uint32_t i = 17; i < 25; i++) s[i] = z;

    for (int i = 0; i < 23; i++) {
        t[0]=xor5(s[0],s[5],s[10],s[15],s[20]);
        t[1]=xor5(s[1],s[6],s[11],s[16],s[21]);
        t[2]=xor5(s[2],s[7],s[12],s[17],s[22]);
        t[3]=xor5(s[3],s[8],s[13],s[18],s[23]);
        t[4]=xor5(s[4],s[9],s[14],s[19],s[24]);
        u=xor2(t[4],ROL2(t[1],1));
        s[0].x ^= u.x; s[0].y ^= u.y; s[5].x ^= u.x; s[5].y ^= u.y;
        s[10].x ^= u.x; s[10].y ^= u.y; s[15].x ^= u.x; s[15].y ^= u.y;
        s[20].x ^= u.x; s[20].y ^= u.y;
        u=xor2(t[0],ROL2(t[2],1));
        s[1].x ^= u.x; s[1].y ^= u.y; s[6].x ^= u.x; s[6].y ^= u.y;
        s[11].x ^= u.x; s[11].y ^= u.y; s[16].x ^= u.x; s[16].y ^= u.y;
        s[21].x ^= u.x; s[21].y ^= u.y;
        u=xor2(t[1],ROL2(t[3],1));
        s[2].x ^= u.x; s[2].y ^= u.y; s[7].x ^= u.x; s[7].y ^= u.y;
        s[12].x ^= u.x; s[12].y ^= u.y; s[17].x ^= u.x; s[17].y ^= u.y;
        s[22].x ^= u.x; s[22].y ^= u.y;
        u=xor2(t[2],ROL2(t[4],1));
        s[3].x ^= u.x; s[3].y ^= u.y; s[8].x ^= u.x; s[8].y ^= u.y;
        s[13].x ^= u.x; s[13].y ^= u.y; s[18].x ^= u.x; s[18].y ^= u.y;
        s[23].x ^= u.x; s[23].y ^= u.y;
        u=xor2(t[3],ROL2(t[0],1));
        s[4].x ^= u.x; s[4].y ^= u.y; s[9].x ^= u.x; s[9].y ^= u.y;
        s[14].x ^= u.x; s[14].y ^= u.y; s[19].x ^= u.x; s[19].y ^= u.y;
        s[24].x ^= u.x; s[24].y ^= u.y;
        u=s[1];
        s[1]=ROL2(s[6],44); s[6]=ROL2(s[9],20); s[9]=ROL2(s[22],61);
        s[22]=ROL2(s[14],39); s[14]=ROL2(s[20],18); s[20]=ROL2(s[2],62);
        s[2]=ROL2(s[12],43); s[12]=ROL2(s[13],25); s[13]=ROL8(s[19]);
        s[19]=ROR8(s[23]); s[23]=ROL2(s[15],41); s[15]=ROL2(s[4],27);
        s[4]=ROL2(s[24],14); s[24]=ROL2(s[21],2); s[21]=ROL2(s[8],55);
        s[8]=ROL2(s[16],45); s[16]=ROL2(s[5],36); s[5]=ROL2(s[3],28);
        s[3]=ROL2(s[18],21); s[18]=ROL2(s[17],15); s[17]=ROL2(s[11],10);
        s[11]=ROL2(s[7],6); s[7]=ROL2(s[10],3); s[10]=ROL2(u,1);
        ETC_CHI5(0) ETC_CHI5(5) ETC_CHI5(10) ETC_CHI5(15) ETC_CHI5(20)
        s[0].x ^= keccak_rc[i].x; s[0].y ^= keccak_rc[i].y;
    }
    t[0]=xor5(s[0],s[5],s[10],s[15],s[20]);
    t[1]=xor5(s[1],s[6],s[11],s[16],s[21]);
    t[2]=xor5(s[2],s[7],s[12],s[17],s[22]);
    s[0]=xor3(s[0],t[4],ROL2(t[1],1));
    s[6]=xor3(s[6],t[0],ROL2(t[2],1));
    s[12]=xor3(s[12],t[1],ROL2(t[3],1));
    s[1]=ROL2(s[6],44); s[2]=ROL2(s[12],43);
    s[0]=chi(s[0],s[1],s[2]);
    return devectorize(xor2(s[0], keccak_rc[23]));
}

/* ------------------------------------------------------------------ */
/*  SHA3-512 in-place (for DAG item generation)                         */
/* ------------------------------------------------------------------ */
DEV_INLINE void SHA3_512(uint2* s)
{
    uint2 t[5], u, v;
    for (uint32_t i = 8; i < 25; i++) s[i] = make_uint2(0, 0);
    s[8].x = 1; s[8].y = 0x80000000;

    for (int i = 0; i < 23; i++) {
        t[0]=xor5(s[0],s[5],s[10],s[15],s[20]);
        t[1]=xor5(s[1],s[6],s[11],s[16],s[21]);
        t[2]=xor5(s[2],s[7],s[12],s[17],s[22]);
        t[3]=xor5(s[3],s[8],s[13],s[18],s[23]);
        t[4]=xor5(s[4],s[9],s[14],s[19],s[24]);
        u=xor2(t[4],ROL2(t[1],1));
        s[0].x ^= u.x; s[0].y ^= u.y; s[5].x ^= u.x; s[5].y ^= u.y;
        s[10].x ^= u.x; s[10].y ^= u.y; s[15].x ^= u.x; s[15].y ^= u.y;
        s[20].x ^= u.x; s[20].y ^= u.y;
        u=xor2(t[0],ROL2(t[2],1));
        s[1].x ^= u.x; s[1].y ^= u.y; s[6].x ^= u.x; s[6].y ^= u.y;
        s[11].x ^= u.x; s[11].y ^= u.y; s[16].x ^= u.x; s[16].y ^= u.y;
        s[21].x ^= u.x; s[21].y ^= u.y;
        u=xor2(t[1],ROL2(t[3],1));
        s[2].x ^= u.x; s[2].y ^= u.y; s[7].x ^= u.x; s[7].y ^= u.y;
        s[12].x ^= u.x; s[12].y ^= u.y; s[17].x ^= u.x; s[17].y ^= u.y;
        s[22].x ^= u.x; s[22].y ^= u.y;
        u=xor2(t[2],ROL2(t[4],1));
        s[3].x ^= u.x; s[3].y ^= u.y; s[8].x ^= u.x; s[8].y ^= u.y;
        s[13].x ^= u.x; s[13].y ^= u.y; s[18].x ^= u.x; s[18].y ^= u.y;
        s[23].x ^= u.x; s[23].y ^= u.y;
        u=xor2(t[3],ROL2(t[0],1));
        s[4].x ^= u.x; s[4].y ^= u.y; s[9].x ^= u.x; s[9].y ^= u.y;
        s[14].x ^= u.x; s[14].y ^= u.y; s[19].x ^= u.x; s[19].y ^= u.y;
        s[24].x ^= u.x; s[24].y ^= u.y;
        u=s[1];
        s[1]=ROL2(s[6],44); s[6]=ROL2(s[9],20); s[9]=ROL2(s[22],61);
        s[22]=ROL2(s[14],39); s[14]=ROL2(s[20],18); s[20]=ROL2(s[2],62);
        s[2]=ROL2(s[12],43); s[12]=ROL2(s[13],25); s[13]=ROL2(s[19],8);
        s[19]=ROL2(s[23],56); s[23]=ROL2(s[15],41); s[15]=ROL2(s[4],27);
        s[4]=ROL2(s[24],14); s[24]=ROL2(s[21],2); s[21]=ROL2(s[8],55);
        s[8]=ROL2(s[16],45); s[16]=ROL2(s[5],36); s[5]=ROL2(s[3],28);
        s[3]=ROL2(s[18],21); s[18]=ROL2(s[17],15); s[17]=ROL2(s[11],10);
        s[11]=ROL2(s[7],6); s[7]=ROL2(s[10],3); s[10]=ROL2(u,1);
        ETC_CHI5(0) ETC_CHI5(5) ETC_CHI5(10) ETC_CHI5(15) ETC_CHI5(20)
        s[0].x ^= ETC_LDG(keccak_rc[i]).x; s[0].y ^= ETC_LDG(keccak_rc[i]).y;
    }
    t[0]=xor5(s[0],s[5],s[10],s[15],s[20]);
    t[1]=xor5(s[1],s[6],s[11],s[16],s[21]);
    t[2]=xor5(s[2],s[7],s[12],s[17],s[22]);
    t[3]=xor5(s[3],s[8],s[13],s[18],s[23]);
    t[4]=xor5(s[4],s[9],s[14],s[19],s[24]);
    u=xor2(t[4],ROL2(t[1],1)); s[0].x ^= u.x; s[0].y ^= u.y; s[10].x ^= u.x; s[10].y ^= u.y;
    u=xor2(t[0],ROL2(t[2],1)); s[6].x ^= u.x; s[6].y ^= u.y; s[16].x ^= u.x; s[16].y ^= u.y;
    u=xor2(t[1],ROL2(t[3],1)); s[12].x ^= u.x; s[12].y ^= u.y; s[22].x ^= u.x; s[22].y ^= u.y;
    u=xor2(t[2],ROL2(t[4],1)); s[3].x ^= u.x; s[3].y ^= u.y; s[18].x ^= u.x; s[18].y ^= u.y;
    u=xor2(t[3],ROL2(t[0],1)); s[9].x ^= u.x; s[9].y ^= u.y; s[24].x ^= u.x; s[24].y ^= u.y;
    u=s[1];
    s[1]=ROL2(s[6],44); s[6]=ROL2(s[9],20); s[9]=ROL2(s[22],61);
    s[2]=ROL2(s[12],43); s[4]=ROL2(s[24],14); s[8]=ROL2(s[16],45);
    s[5]=ROL2(s[3],28); s[3]=ROL2(s[18],21); s[7]=ROL2(s[10],3);
    u=s[0]; v=s[1];
    s[0]=chi(s[0],s[1],s[2]); s[1]=chi(s[1],s[2],s[3]);
    s[2]=chi(s[2],s[3],s[4]); s[3]=chi(s[3],s[4],u); s[4]=chi(s[4],u,v);
    s[5]=chi(s[5],s[6],s[7]); s[6]=chi(s[6],s[7],s[8]);
    s[7]=chi(s[7],s[8],s[9]);
    s[0].x ^= ETC_LDG(keccak_rc[23]).x; s[0].y ^= ETC_LDG(keccak_rc[23]).y;
}

#undef ETC_CHI5
#undef ETC_THETA_STEP
