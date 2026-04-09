#pragma once
/**
 * etchash_fnv.cuh
 *
 * FNV-1 hash helpers used by the ETCHash DAG-generation and search kernels.
 * Adapted from etcminer fnv.cuh (GPL-3.0).
 */

#define ETCHASH_FNV_PRIME 0x01000193U

/* Scalar FNV-1 mix. */
#define etchash_fnv(x, y) ((x) * ETCHASH_FNV_PRIME ^ (y))

/* Vector (uint4) FNV-1 mix. */
DEV_INLINE uint4 etchash_fnv4(uint4 a, uint4 b)
{
    uint4 c;
    c.x = a.x * ETCHASH_FNV_PRIME ^ b.x;
    c.y = a.y * ETCHASH_FNV_PRIME ^ b.y;
    c.z = a.z * ETCHASH_FNV_PRIME ^ b.z;
    c.w = a.w * ETCHASH_FNV_PRIME ^ b.w;
    return c;
}

/* Reduce a uint4 to a single uint32 via three FNV mixes. */
DEV_INLINE uint32_t etchash_fnv_reduce(uint4 v)
{
    return etchash_fnv(etchash_fnv(etchash_fnv(v.x, v.y), v.z), v.w);
}
