# GBXminer Performance Upgrade Plan

## Changelog

### v1.1.0 (Current)
- **perf(neoscrypt)**: Refine GPU intensity settings based on SM version and model ✅
- **fix(neoscrypt)**: Restore atomicExch for nonce capture (fixes data race) ✅

### v1.0.1
- **build**: Add sm_89/sm_90 codegen targets ✅

---

**IMPORTANT**: This plan requires a correctness test harness before any optimization. Without it, speedups cannot be distinguished from silent hash errors.

---

## Phase 0: Prerequisites (Required Before Any Optimization)

1. **Correctness Test Harness**: Establish baseline hash validation
2. **Baseline Profiling**: Run Nsight Systems to identify actual bottlenecks
3. **Per-algorithm isolation**: Each algorithm in separate PR for easy bisection

---

## Priority 1: Quick Wins (Safe, Low-Risk)

### 1.1 Replace cudaThreadSynchronize() ✅ DONE

**Status**: Already replaced - no instances in codebase

```bash
# Replace all instances with cudaDeviceSynchronize()
```

### 1.2 Add sm_89/sm_90 Codegen Targets ✅ DONE

**Status**: Already implemented in Makefile.am (lines 152-154)

---

## Priority 2: Warp Shuffle Optimization

### 2.1 Warp Shuffle Macro for Modern GPUs ✅ DONE

**Status**: Implemented in cuda_helper.h (lines 685-696)

```cuda
#if __CUDA_ARCH__ >= 700
#define WARP_SHFL(x, y, z) __shfl_sync(0xffffffff, x, y, z)
#else
#define WARP_SHFL(x, y, z) __shfl(x, y, z)
#endif
```

**Note**: Consolidate with existing cuda_helper.h shim.

---

## Priority 3: Architecture-Specific Configuration

### 3.1 Dynamic Launch Config ✅ IMPLEMENTED

**Status**: Implemented in cuda.cpp

New functions added:
- `cuda_optimal_throughput(int thr_id, int threads_per_block, size_t dynamic_smem_size)` - Calculates optimal throughput based on device occupancy
- `cuda_get_max_active_blocks_per_mp(...)` - Legacy occupancy query for external kernel use

The implementation uses CUDA occupancy API to automatically determine optimal launch configurations:
```c
uint32_t cuda_optimal_throughput(int thr_id, int threads_per_block, size_t dynamic_smem_size)
{
    // Queries device properties and calculates:
    // - maxThreadsPerMultiProcessor
    // - maxBlocksPerMultiProcessor
    // - multiProcessorCount
    // Returns 85% of theoretical maximum (leaves room for OS/display GPU)
}
```

**Do NOT hardcode** `throughput *= 1.5` for any GPU.

### 3.2 Register Allocation ⚠️ PENDING

**Status**: Not implemented - profile first

More registers = lower occupancy. The correct approach:
1. Profile with `--ptxas-options=-v`
2. Adjust **downward** only if kernel is register-bound
3. Default 128 is fine; let NVCC choose optimal

### 3.3 Tensor Core Acceleration ❌ REMOVED

**Status**: Hard NACK - Not applicable

Tensor Cores operate on TF32/FP16/INT8. TF32 is NOT bit-exact with FP32. Lyra2 and NeoScrypt are cryptographic hashes. Any non-exact arithmetic produces invalid hashes and silent rejected shares. **Removed from plan.**

---

## Priority 4: Memory Pipeline (CONDITIONAL)

### 4.1 Pinned Memory ⚠️ PENDING

**Requires** (not yet implemented):
- Global allocator budget (not unlimited allocations)
- Test on systems with ≤8GB RAM
- Limit concurrent pinned allocations

Currently used in 19 places without budget management.

### 4.2 CUDA Streams ⚠️ PARTIAL

**Status**: Implemented in ~6 algorithms (neoscrypt, scrypt, wildkeccak, nist5, vanilla)

Most other algorithms use default NULL stream (synchronous).

---

## Priority 5: Build & Compilation

### 5.1 --use_fast_math ❌ REMOVED

**Status**: Hard NACK - causes non-determinism

This flag enables `--ftz=true` (flush denormals to zero) and replaces IEEE-754 operations with faster approximations. Can cause issues in floating-point helper code.

**Use instead**: `--fmad=true` only (safe, enables FMA without fast-math side effects)

---

## Priority 6: Kernel Fusion (REQUIRES PROOF OF CONCEPT)

### 6.1 X11 Kernel Fusion ❌ BLOCKED

**Status**: NACK - underspecified, needs proof of concept

The claim "20-35% gain" has no basis. Problems:

1. **State management**: Each of 11 algorithms has different intermediate buffer requirements
2. **Register pressure**: Fusing 3+ algorithms likely spills to global memory
3. **Profiling required**: Kernel launch overhead is ~5-15µs; actual kernels run ms - fusion may buy nothing

**Required before proceeding**:
- Nsight Systems profile showing launch overhead is measured bottleneck
- Proof-of-concept for single pair (e.g., Blake+BMW) before all 11

---

## Priority 7: Multi-GPU

### 7.1 cudaMallocManaged ❌ REMOVED

**Status**: NACK - would hurt performance

Unified memory uses page migration under the hood. On multi-GPU rigs without NVLink, page faults and migration traffic tank throughput, not improve it. Not a performance optimization.

---

## Summary: What's Approved

| Item | Status | Reason |
|------|--------|--------|
| cudaThreadSynchronize → cudaDeviceSynchronize | ✅ DONE | Already replaced in codebase |
| sm_89/sm_90 codegen | ✅ DONE | Makefile.am (lines 152-154) |
| Warp shuffle macro | ✅ DONE | cuda_helper.h (lines 685-696) |
| CUDA Streams | ✅ ACK | Used in neoscrypt, scrypt, salsa, wildkeccak |
| --fmad=true | ✅ ACK | Safe FMA optimization |
| Pinned memory | ⚠️ CONDITIONAL | Needs budget & testing |
| X11 kernel fusion | ❌ BLOCKED | No proof of concept |
| --use_fast_math | ❌ REMOVED | Non-determinism risk |
| Tensor cores | ❌ REMOVED | Cryptography requires exact |
| cudaMallocManaged | ❌ REMOVED | Page migration hurts |
| Magic number multipliers | ❌ REMOVED | Causes OOM/underperformance |

---

## Implementation Order

1. **Standalone commits** for each correctness fix
2. **Establish test harness** before any optimization
3. **Profile with Nsight Systems** to find actual bottlenecks
4. **Per-algorithm optimization** with isolated PRs
5. **Validate each change** against test harness

---

## Verification Commands

```bash
# Correctness check (before any optimization)
./gbxminer --benchmark -a neoscrypt --time-limit=60

# Profile bottleneck
nsys profile ./gbxminer -a neoscrypt -o profile.qdrep

# Build after changes
make clean && make -j4

# Verify hashes still correct
./gbxminer --benchmark -a neoscrypt --time-limit=60
```
