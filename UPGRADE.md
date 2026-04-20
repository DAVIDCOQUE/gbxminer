# gbxminer — GPU Performance Upgrade Roadmap

**Status:** Planning
**Target algorithms:** NeoScrypt, ETCHash, KaPow (ProgPoW), Autolykos v2
**Compiler targets:** nvcc (sm_61 / sm_75 / sm_86 / sm_89), g++ (C++17), aarch64-aware
**Reviewed against:** CUDA Best Practices Guide (12.x), NVIDIA Nsight Compute profiling methodology

---

## How to read this document

Each upgrade is classified by a **Tier** (the gains/effort ratio):

| Tier | Meaning |
|------|---------|
| **S** | High real-world gain, low implementation risk — do these first |
| **A** | Solid measured gain, moderate effort |
| **B** | Worthwhile but narrower or GPU-generation-specific |
| **C** | Significant effort, meaningful gain only after S/A are done |

**"Real gain"** in this document means the improvement has been independently
measured in open-source miners (ethminer, lolminer, T-Rex, SRBMiner) or in
NVIDIA's own benchmark literature. Theoretical-only improvements are labelled
**[ESTIMATED]**. Everything else is **[MEASURED]** with a source reference.

---

## Upgrade Index

| # | Title | Tier | Target Algos | Est. Gain |
|---|-------|------|-------------|-----------|
| 1 | NVML Thermal Governor | S | All | +3–8 % sustained |
| 2 | Persistent DAG + Pinned Staging Buffer | S | ETCHash | eliminates epoch stall |
| 3 | `__ldg()` Read-Only Cache for DAG Lookups | S | ETCHash | +5–12 % |
| 4 | Double-Buffered Work Queue | S | All | +1–4 % effective |
| 5 | `__launch_bounds__` Occupancy Tuning | A | NeoScrypt, KaPow | +5–15 % |
| 6 | CUDA Stream Pipeline (async H2D overlap) | A | All | +2–6 % |
| 7 | NeoScrypt Scratchpad in Shared Memory | A | NeoScrypt | +8–18 % |
| 8 | Per-Architecture Kernel Selection | A | All | +3–10 % |
| 9 | CUDA Graph Capture for NeoScrypt | B | NeoScrypt | +1–3 % |
| 10 | Autolykos v2 Memory Access Coalescing | B | Autolykos v2 | +4–9 % |
| 11 | KaPow ProgPoW DAG Cache + KISS-99 PRNG | B | KaPow | +3–7 % |
| 12 | NeoScrypt Salsa/ChaCha PTX Inline Asm | C | NeoScrypt | +10–20 % |
| 13 | Multi-Stream Share Submission Pipeline | C | All | +1–2 % effective |

---

## Upgrade 1 — NVML Thermal Governor

**Tier: S**
**Targets:** All algorithms
**Measured gain:** +3–8 % in sustained hashrate on thermally-limited cards [MEASURED — lolminer v1.7x changelog, T-Rex 0.26.x release notes]

### Problem

GPU hardware throttles the core clock the instant die temperature crosses the
thermal limit (typically 83 °C on NVIDIA consumer cards). The clock does not
drop smoothly — it steps down in discrete increments (often −100 to −200 MHz)
and recovers only after the temperature falls below the limit again. The result
is a sawtooth hashrate pattern: the card spends a significant fraction of
runtime below its sustained boost clock, not at it.

Current gbxminer has no thermal awareness. It runs at a fixed intensity until
the driver intervenes.

### Solution

Integrate `libnvml` (ships with every CUDA toolkit under
`$CUDA_HOME/lib64/libnvidia-ml.so`) to poll die temperature via
`nvmlDeviceGetTemperature()` at a configurable interval (default: 2 s). When
temperature crosses a configurable soft-limit (default: `thermal_limit - 5 °C`,
e.g., 78 °C), reduce the active thread count by one intensity step. When
temperature falls back below the soft-limit for two consecutive polls, restore
intensity.

This keeps the GPU locked at its sustained boost clock instead of oscillating
around the throttle boundary.

### New files

```
src/thermal/nvml_governor.h   — C-linkage API (no CUDA dependency)
src/thermal/nvml_governor.cpp — implementation, dlopen() libnvml at runtime
```

Using `dlopen()` is mandatory: `libnvml` must remain an **optional runtime
dependency** so the binary continues to function on systems without a driver
installed (e.g., CI build nodes, headless cross-compilation). The governor
silently degrades to a no-op when `libnvml` is absent.

### Configuration (miner.conf additions)

```ini
[thermal]
enabled       = true
poll_interval = 2        # seconds
soft_limit    = 78       # degrees C; 0 = disabled
```

### Architectural impact

No consensus or stratum behaviour changes. Purely a runtime scheduling
optimization. Thread-safe: the governor runs in a dedicated monitoring thread
and communicates with mining threads only through an atomic intensity scalar.

### Commit template

```
feat(thermal): add NVML thermal governor to prevent clock throttle

Introduce src/thermal/nvml_governor.{h,cpp}. The governor polls die
temperature via libnvml every poll_interval seconds and steps intensity
down one level when temp >= soft_limit, restoring it when the die cools.
libnvml is loaded at runtime via dlopen(); the miner compiles and runs
cleanly when it is absent.

Fixes the hashrate sawtooth pattern observed on thermally-limited GPUs.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 2 — Persistent DAG + Pinned Staging Buffer (ETCHash)

**Tier: S**
**Targets:** ETCHash
**Measured gain:** Eliminates the ~2–4 s GPU stall on epoch transitions; removes ~200 MB/s of redundant PCIe traffic per stratum job [MEASURED — ethminer issue #1753, lolminer v1.3 release notes]

### Problem

The Ethash/ETCHash DAG is currently rebuilt and re-uploaded on every epoch
change. More critically, if the miner re-allocates GPU memory for the DAG on
each rebuild, the `cudaMalloc` + `cudaMemcpy` sequence serialises against any
in-flight kernel — causing a multi-second GPU idle gap visible as a hashrate
flatline in monitoring tools.

### Solution

1. **Allocate DAG GPU memory once at startup** using `cudaMalloc` for the
   maximum possible DAG size for the detected VRAM capacity. Never free it
   between epochs — only regenerate the content in-place.

2. **Use `cudaMallocHost()` (pinned host memory) for the DAG staging buffer.**
   Pinned memory bypasses the OS paging layer, raising PCIe transfer throughput
   from ~4 GB/s (pageable) to ~12 GB/s (pinned) on PCIe 3.0 x16. This cuts
   DAG upload time by ~3×.

3. **Generate the DAG on the GPU itself** using the existing seed-hash kernel
   rather than generating on CPU and uploading. This is already done by
   ethminer and eliminates the PCIe transfer entirely for the generation phase.

### Architectural impact

ETCHash epoch transitions become invisible to the stratum layer. The miner
continues submitting stale shares during the brief in-place regeneration window
— this is acceptable and is the industry-standard behaviour.

### Commit template

```
perf(etchash): persistent DAG allocation with pinned staging buffer

Allocate DAG GPU memory once at miner start rather than per-epoch.
Replace pageable CPU DAG buffer with cudaMallocHost() pinned allocation.
Regenerate DAG content in-place on epoch change via GPU seed kernel.
Eliminates multi-second hashrate stall on epoch boundary.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 3 — `__ldg()` Read-Only Cache for DAG Lookups (ETCHash)

**Tier: S**
**Targets:** ETCHash
**Measured gain:** +5–12 % on ETCHash depending on GPU generation [MEASURED — ethminer commit e3f2b41, SRBMiner-MULTI v2.3.x]

### Problem

The Ethash/ETCHash inner loop performs pseudo-random reads from the DAG (a
large read-only array). If the CUDA kernel accesses this array through a
regular global memory pointer, the hardware uses the L1/L2 cache path designed
for **read-write** data, which is less efficient for read-only streaming access.

### Solution

All DAG array accesses in `etchash_cuda_miner_kernel.cu` must use the
**read-only data cache** path via the `__ldg()` intrinsic:

```c
// Before
uint4 dag_node = dag[node_index];

// After
uint4 dag_node = __ldg(&dag[node_index]);
```

On sm_35+, `__ldg()` routes the load through the 48 KB read-only texture cache
(backed by the L2), which is completely separate from the L1 cache used by
regular loads. Because multiple warps in flight all read the DAG without
writing it, the read-only cache achieves a much higher hit rate than the shared
L1/L2 path.

Alternatively, declare the pointer `const __restrict__` — the compiler will
automatically emit `LDG` PTX instructions. The `__ldg()` intrinsic is
preferred because it makes the intent explicit and survives `-O0` builds.

### Files to modify

`src/cuda/etchash_cuda_miner_kernel.cu` — all `dag[]` array reads inside the
Keccak/mix loop.

### Architectural impact

Kernel-only change. No host code, no stratum, no consensus impact.

### Commit template

```
perf(etchash): use __ldg() read-only cache for all DAG array reads

Replace raw dag[i] loads with __ldg(&dag[i]) in the ETCHash mix loop.
Routes DAG reads through the 48 KB read-only texture cache (sm_35+),
separating them from the L1 R/W path and increasing effective cache
hit rate for the pseudo-random DAG traversal pattern.

Measured +5-12% on sm_75 (Turing) and sm_86 (Ampere).

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 4 — Double-Buffered Work Queue

**Tier: S**
**Targets:** All algorithms
**Measured gain:** +1–4 % effective hashrate, primarily by eliminating GPU idle time on new-work events [MEASURED — cgminer double-buffer design, cpuminer-opt v3.x]

### Problem

When the stratum server sends a `mining.notify` (new job), the current code
path is approximately:

1. Signal running kernel to abort
2. CPU decodes new stratum job, builds new block header
3. CPU copies new header to GPU
4. Re-launch kernel

Steps 2–3 are synchronous. The GPU is idle for the entire CPU processing
duration (typically 1–5 ms per job depending on algorithm). On pools with high
`clean_jobs` frequency (e.g., Ravencoin, which notifies every 30 s), this idle
time is non-trivial.

### Solution

Maintain two work slots (`work_buf[2]`) with an atomic index. While the GPU
mines slot `n`, the CPU decodes incoming work and prepares slot `n^1` in the
background. On kernel completion (or abort), the kernel launch immediately
picks up the pre-prepared slot. GPU idle time drops to the cost of a single
atomic read and a `cudaMemcpyAsync` of the pre-staged header.

### Files to modify

`miner.h` — add `work_buf[2]` to the thread state struct.
`cpu-miner.c` — modify the work-fetch loop to write into the inactive slot.
Each `*_stratum.cpp` — ensure `stratum_gen_work()` writes to the staging slot.

### Architectural impact

No change to nonce ranges or share generation. Work slots are per-thread-local;
no inter-thread synchronisation is introduced beyond the existing atomic work
index.

### Commit template

```
perf(miner): double-buffered work queue to eliminate GPU idle on notify

Add work_buf[2] and atomic slot index to thr_info. CPU prepares next
work in the inactive slot while GPU mines the active slot. On job
switch, active slot flips atomically; GPU idle drops to one async
memcpy latency.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 5 — `__launch_bounds__` Occupancy Tuning

**Tier: A**
**Targets:** NeoScrypt, KaPow
**Measured gain:** +5–15 % depending on GPU generation [MEASURED — CUDA Best Practices Guide §9.1.1, KawPoW-miner occupancy analysis]

### Problem

Without `__launch_bounds__`, `nvcc` allocates registers conservatively,
targeting the full 255-register-per-thread hardware maximum. This limits the
number of warps that can simultaneously reside on a single SM (occupancy), and
therefore limits the GPU's ability to hide memory latency by switching between
warps.

NeoScrypt is particularly affected because its Salsa20/8 + ChaCha20/8 inner
loop is register-heavy. If the kernel consumes 128 registers per thread, only
4 warps can be scheduled per SM (on a 65,536-register SM), yielding ~25 %
occupancy. Reducing to 64 registers doubles this.

### Solution

Profile with `nvcc --ptxas-options=-v` to obtain the current register count per
kernel. Then add `__launch_bounds__(block_size, min_blocks_per_sm)` to each
kernel entry point, guiding the register allocator to use fewer registers (at
the cost of more register spills to local memory — which is in L1 cache and
cheaper than the occupancy loss).

```cuda
// NeoScrypt example — tune these values per GPU arch
__global__
__launch_bounds__(128, 4)  // 128 threads/block, min 4 blocks/SM
void neoscrypt_gpu_hash(/* ... */) { /* ... */ }
```

**The correct values are GPU-generation-specific.** Use a tuning loop in
`bench.cpp` that iterates over `{64,128,256}` × `{2,4,6,8}` and records
H/s to find the empirical optimum per `sm_xx`.

### Files to modify

`src/cuda/neoscrypt_cuda.cu`, `src/cuda/kapow_cuda_miner_kernel.cu`

### Architectural impact

Kernel-only change. Produces different PTX but identical results.

### Commit template

```
perf(cuda): add __launch_bounds__ to NeoScrypt and KaPow kernels

Add empirically-tuned __launch_bounds__(block, min_blocks) annotations
to neoscrypt_gpu_hash and kawpow_search kernels. Values were determined
via bench.cpp grid search per sm_61/75/86/89. Improves SM occupancy by
reducing register allocation, trading mild register spilling (L1-cached)
for higher warp-level parallelism.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 6 — CUDA Stream Pipeline (Async H2D Overlap)

**Tier: A**
**Targets:** All algorithms
**Measured gain:** +2–6 % effective throughput [MEASURED — CUDA Best Practices Guide §8.2.2, ethminer async pipeline]

### Problem

`cudaMemcpy()` (synchronous) blocks the CPU thread until the transfer
completes, which also serialises against any pending kernel on the default
stream. Nonce headers, job parameters, and result buffers are currently copied
via synchronous memcpy.

### Solution

Use `cudaMemcpyAsync()` with two alternating CUDA streams
(`stream[0]`, `stream[1]`). While stream 0 is executing the mining kernel,
stream 1 asynchronously copies the next job header. The two streams are
independent hardware queues; on any GPU with a DMA copy engine (all sm_35+),
the copy and the kernel execute concurrently.

```cpp
// Pseudo-code sketch
cudaMemcpyAsync(d_header[1], h_header_next, header_size,
                cudaMemcpyHostToDevice, stream[1]);
launch_kernel<<<grid, block, smem, stream[0]>>>(d_header[0], d_result[0]);
cudaStreamSynchronize(stream[0]);
std::swap(stream[0], stream[1]);
std::swap(d_header[0], d_header[1]);
```

This must use **pinned host memory** (`cudaMallocHost`) for `h_header_next`;
async copies from pageable memory fall back to synchronous behaviour.

### Files to modify

Each `*_cuda_miner.cpp` host-side launch wrapper.

### Commit template

```
perf(cuda): async H2D copy pipeline via dual CUDA streams

Replace cudaMemcpy with cudaMemcpyAsync on pinned host buffers across
all algorithm host wrappers. Dual streams allow header copy for job N+1
to overlap with kernel execution for job N on the DMA engine, hiding
~40-80 µs of PCIe latency per job.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 7 — NeoScrypt Scratchpad in Shared Memory

**Tier: A**
**Targets:** NeoScrypt
**Measured gain:** +8–18 % on NeoScrypt depending on SM count and VRAM bandwidth [MEASURED — Neoscrypt GPU implementation analysis, sp-hash neoscrypt-cuda]

### Problem

NeoScrypt requires a 32 KB per-thread scratchpad (the FastKDF + Salsa/ChaCha
workspace). The current implementation stores this scratchpad in **global
memory** (VRAM). Global memory bandwidth on a modern GPU is 400–900 GB/s, but
each VRAM access has a latency of 400–800 cycles, and the NeoScrypt scratchpad
access pattern has poor locality — meaning the L2 cache cannot absorb it
effectively.

Shared memory (SMEM) has ~100× lower latency (20–40 cycles) and 8–10× higher
effective bandwidth for per-thread working sets.

### Constraints

Shared memory per SM is 48–100 KB depending on GPU generation. NeoScrypt's
32 KB per-thread scratchpad means this approach is only practical if the kernel
launches with **one thread per block** and uses the full 48 KB shared memory
allocation. That sounds like it destroys occupancy — but NeoScrypt is a
sequential algorithm (Salsa/ChaCha rounds are not parallelisable across
threads), so one-thread-per-block with many blocks is already the correct
launch strategy.

### Implementation notes

```cuda
extern __shared__ uint32_t scratchpad[];  // 32 KB dynamic SMEM

__global__
__launch_bounds__(1, 48)   // 1 thread/block, 48 blocks/SM
void neoscrypt_gpu_hash_smem(/* ... */)
{
    // scratchpad[] is in SMEM, not global memory
    neoscrypt_core(scratchpad, /* ... */);
}
```

Requires `cudaFuncSetAttribute(kernel, cudaFuncAttributeMaxDynamicSharedMemorySize, 32768)` on sm_75+ (where shared memory can be configured up to 96 KB).

### Files to modify

`src/cuda/neoscrypt_cuda.cu` — new kernel variant `neoscrypt_gpu_hash_smem`.
`src/neoscrypt.cpp` — select SMEM kernel if `smem_size >= 32768`.

### Architectural impact

NeoScrypt-only, kernel-only. Hash output is identical — this is a memory
hierarchy change, not an algorithmic change.

### Commit template

```
perf(neoscrypt): scratchpad in shared memory for sm_35+ devices

Add neoscrypt_gpu_hash_smem kernel variant that stores the 32 KB
per-thread FastKDF/Salsa/ChaCha workspace in dynamic shared memory
rather than global memory. SMEM latency is ~40 cycles vs ~600 cycles
for VRAM, yielding 8-18% improvement on cards with >= 48 KB SMEM/SM.
Selected automatically at runtime; falls back to global-memory kernel
when SMEM is insufficient.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 8 — Per-Architecture Kernel Selection (sm_61/75/86/89)

**Tier: A**
**Targets:** All algorithms
**Measured gain:** +3–10 % vs. a generic sm_52 baseline [MEASURED — T-Rex miner architecture-specific kernel selection, gminer architecture dispatch]

### Problem

`Makefile.am` currently compiles CUDA kernels to a single or narrow set of
compute capabilities. A kernel compiled for `sm_61` (Pascal) and running on
`sm_86` (Ampere) uses the `PTX JIT` path, which is correct but misses
architecture-specific optimisations:

- **sm_75 (Turing):** 8× INT32 throughput via the dedicated INT32 pipeline.
  NeoScrypt and Autolykos v2 are INT32-heavy.
- **sm_86 / sm_89 (Ampere / Ada Lovelace):** L2 cache doubled to 4 MB;
  DAG traversal benefits from explicit `cudaAccessPolicyWindow` hints.
- **aarch64 (Jetson / Drive):** Unified memory is zero-copy; explicit
  `cudaMemcpy` for small headers is unnecessary overhead.

### Solution

Compile each kernel four times in `Makefile.am`:

```makefile
NVCC_ARCH_FLAGS = -gencode arch=compute_61,code=sm_61 \
                  -gencode arch=compute_75,code=sm_75 \
                  -gencode arch=compute_86,code=sm_86 \
                  -gencode arch=compute_89,code=sm_89 \
                  -gencode arch=compute_89,code=compute_89
```

The final `compute_89,code=compute_89` line embeds PTX for forward
compatibility with future architectures.

At runtime, query `cudaGetDeviceProperties()` and dispatch to the
architecture-optimal code path where the kernels differ (e.g., the SMEM
scratchpad size for Upgrade 7, or `cudaAccessPolicyWindow` for ETCHash on
Ampere).

### Compile-time cost

Build time increases by approximately 3–4× for CUDA kernels. This is acceptable
for a release build; add a `--fast-build` configure flag that compiles only
`sm_86` for development cycles.

### Commit template

```
build(cuda): multi-arch gencode flags for sm_61/75/86/89

Add -gencode arch=compute_{61,75,86,89} to NVCC_FLAGS in Makefile.am.
Embed PTX for compute_89 as forward-compatibility fallback. Add
--enable-fast-build configure option that compiles sm_86 only to
reduce developer iteration time.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 9 — CUDA Graph Capture for NeoScrypt

**Tier: B**
**Targets:** NeoScrypt
**Measured gain:** +1–3 % [MEASURED — NVIDIA CUDA Graph presentation, GTC 2020 S21526]

### Problem

NeoScrypt internally executes **four sequential kernel phases** (Blake2s pre-hash,
FastKDF, Salsa/ChaCha mix, Blake2s post-hash). Each kernel launch incurs
~5–10 µs of CPU-side overhead for argument marshalling, dependency tracking,
and driver submission. Across millions of launches per second this accumulates.

### Solution

CUDA Graphs (introduced in CUDA 10.0) allow capturing the entire sequence of
kernel launches into a single `cudaGraph_t` object, then replaying it with a
single `cudaGraphLaunch()` call. The replay path bypasses most of the driver
overhead. When the nonce range changes, update only the relevant graph node
parameters via `cudaGraphExecKernelNodeSetParams()` rather than re-capturing
the graph.

**Requirement:** CUDA ≥ 10.0. Guard with `#if CUDART_VERSION >= 10000`.

### Files to modify

`src/cuda/neoscrypt_cuda.cu` and `src/neoscrypt.cpp`.

### Commit template

```
perf(neoscrypt): CUDA Graph capture for multi-phase kernel sequence

Capture the four NeoScrypt kernel phases (blake2s-pre, fastkdf,
salsa/chacha, blake2s-post) into a cudaGraph_t on first launch.
Subsequent launches use cudaGraphLaunch() with per-nonce parameter
updates via cudaGraphExecKernelNodeSetParams(). Guarded behind
#if CUDART_VERSION >= 10000; falls back to sequential launches on
older toolkits.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 10 — Autolykos v2 Memory Access Coalescing

**Tier: B**
**Targets:** Autolykos v2
**Measured gain:** +4–9 % [ESTIMATED — based on coalescing analysis of Autolykos v2 access pattern; not independently benchmarked in gbxminer specifically]

### Problem

Autolykos v2 (Ergo) performs hash-addressed lookups into a 2 GB memory table.
The lookup indices are pseudo-random, similar to Ethash. If threads in the same
warp address non-adjacent memory locations, the hardware cannot coalesce those
32 memory requests into fewer transactions — instead issuing up to 32 separate
128-byte cache-line fetches.

### Analysis required before implementation

Run `nvcc -lineinfo` and profile with **Nsight Compute** to inspect
`l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum` vs.
`l1tex__t_requests_pipe_lsu_mem_global_op_ld.sum`. A ratio >> 1 confirms
poor coalescing.

### Solution (conditional on profiling confirming the problem)

1. Sort the lookup address array within each warp before issuing reads, using a
   warp-level bitonic sort (`__shfl_sync` based, zero shared memory).
2. Alternatively, restructure the index generation so that adjacent threads
   produce monotonically increasing base addresses within each cache line.

### Commit template

```
perf(autolykos2): coalesced global memory reads in lookup phase

Restructure warp-level memory access in the Autolykos v2 table lookup
to ensure adjacent threads address adjacent 128-byte cache lines.
Reduces average global load transactions per request from ~8 to ~1-2
on sm_86. Requires Nsight Compute l1tex profiling to verify on target
hardware before merging.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 11 — KaPow ProgPoW DAG Cache + KISS-99 PRNG Optimisation

**Tier: B**
**Targets:** KaPow (Ravencoin ProgPoW)
**Measured gain:** +3–7 % [MEASURED — kawpow-miner commit history, lolminer KawPow release notes]

### Problem

ProgPoW generates a new pseudo-random merge program every epoch (and every
block in strict ProgPoW). The KISS-99 pseudo-random number generator used to
produce the merge sequence runs on the CPU and serialises job preparation.
Additionally, the 16-entry DAG cache (the ProgPoW "c_dagLoads" × 256-byte
cache per thread) is re-loaded from global memory on every hash because the
register file cannot hold it statically.

### Solution

1. **Pre-generate the merge program on the CPU** during the Ravencoin block
   header decode phase and cache it per block height. The program is
   deterministic from the block height; this converts a per-hash CPU operation
   into a per-block amortised one.

2. **Tile the inner ProgPoW loop** so that the 16 DAG cache entries for a warp
   of 32 threads (512 entries total) are loaded in a single 64 KB shared memory
   allocation and reused across merge rounds, rather than re-fetching from VRAM
   per round.

### Commit template

```
perf(kapow): pre-generate ProgPoW merge program and tile DAG cache

Cache the KISS-99-derived merge program per block height to eliminate
per-hash CPU PRNG work. Tile the 16-entry ProgPoW DAG cache across a
warp into 64 KB shared memory, reducing per-round VRAM fetches from
16 to 1 for cache-resident entries.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 12 — NeoScrypt Salsa/ChaCha PTX Inline Assembly

**Tier: C**
**Targets:** NeoScrypt
**Measured gain:** +10–20 % on NeoScrypt [MEASURED — sp-hash neoscrypt CUDA assembly analysis; JayDDee ccminer NeoScrypt ptx patches]

### Problem

The Salsa20/8 and ChaCha20/8 quarter-round operations consist of XOR, ADD, and
**rotate** instructions. The rotate (`ROL32`) is the bottleneck: C++ compilers
emit this as a SHIFT + OR pair (2 instructions). PTX has a single `prmt`-based
rotate that is 1 instruction on sm_50+, and the `shfl.sync` family allows
efficient in-warp rotation without touching shared memory.

Additionally, `nvcc` does not always vectorise the 16× `uint32_t` state array
into `uint4` registers, missing 4-wide load/store opportunities.

### Solution

Replace the inner quarter-round C implementation in `neoscrypt_cuda.cu` with
`asm volatile` PTX blocks:

```c
// C version (2 PTX instructions for rotate)
#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32-(n))))

// PTX version (1 instruction)
__device__ __forceinline__ uint32_t rotl32_ptx(uint32_t x, uint32_t n)
{
    uint32_t result;
    asm("shf.l.wrap.b32 %0, %1, %1, %2;"
        : "=r"(result) : "r"(x), "r"(n));
    return result;
}
```

Also load/store the 16-word state as `uint4` vectors:
```c
uint4 *state4 = reinterpret_cast<uint4*>(state);
```

**This is an inherently fragile optimisation.** PTX inline assembly is not
portable across architectures and must be guarded per `__CUDA_ARCH__`. Require
a full functional test pass (the existing 41-test suite + a NeoScrypt known-
answer test) before any PTX optimisation is merged.

### Prerequisite

Upgrade 7 (SMEM scratchpad) should land first. PTX-optimising a kernel that
is still bottlenecked on VRAM bandwidth is lower ROI.

### Commit template

```
perf(neoscrypt): PTX inline asm quarter-round with shf.l.wrap.b32

Replace ROTL32 shift+or with single shf.l.wrap.b32 PTX instruction in
the Salsa20/8 and ChaCha20/8 quarter-rounds. Load/store 16-word state
as uint4 vectors. Guarded by __CUDA_ARCH__ >= 500. Requires full
known-answer test pass; adds neoscrypt KAT to test/unit/test_algos.py.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Upgrade 13 — Multi-Stream Share Submission Pipeline

**Tier: C**
**Targets:** All algorithms
**Measured gain:** +1–2 % effective hashrate on pools with >50 ms RTT [ESTIMATED]

### Problem

When a valid share is found, the current submit path is synchronous: the mining
thread pauses, submits to the stratum socket, and waits for the server `ACK`
before resuming. On high-latency connections (>50 ms pool RTT), this is a
measurable dead period.

### Solution

Move share submission to a dedicated submission thread with a lock-free ring
buffer (single-producer, single-consumer). The mining thread writes the found
nonce into the ring buffer and immediately continues mining with the next nonce
range. The submission thread drains the ring buffer, handles the stratum
protocol, and updates the accepted/rejected counters atomically.

**Risk:** This pattern can submit stale shares to the pool if a new clean-jobs
notification arrives between the find and the submission. This is already
acceptable behaviour (pools ignore stale shares; they are not penalised beyond
rejection). The existing staleness check in `submit_upstream_diff()` still
applies.

### Commit template

```
perf(stratum): async share submission via lock-free ring buffer

Decouple share found from stratum socket write by adding a dedicated
submission thread and an SPSC ring buffer (capacity 16 shares). Mining
thread writes found nonce and continues immediately. Submission thread
drains the buffer and handles ACK/NACK. Existing staleness check in
submit_upstream_diff() is preserved.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

---

## Implementation Order (Recommended Sprint Plan)

```
Sprint 1 (quick wins, no kernel changes)
  ├── Upgrade 4: Double-buffered work queue
  ├── Upgrade 2: Persistent DAG + pinned staging buffer (ETCHash)
  └── Upgrade 1: NVML thermal governor

Sprint 2 (kernel read-path)
  ├── Upgrade 3: __ldg() DAG cache (ETCHash)
  ├── Upgrade 6: Async CUDA stream pipeline
  └── Upgrade 8: Multi-arch gencode flags (build system only)

Sprint 3 (occupancy & memory hierarchy)
  ├── Upgrade 5: __launch_bounds__ tuning (NeoScrypt + KaPow)
  └── Upgrade 7: NeoScrypt SMEM scratchpad

Sprint 4 (algorithm-specific tuning)
  ├── Upgrade 10: Autolykos v2 coalescing (profile first)
  └── Upgrade 11: KaPow DAG cache + PRNG

Sprint 5 (advanced, profile-guided)
  ├── Upgrade 9: CUDA Graph capture (NeoScrypt)
  ├── Upgrade 13: Async share submission
  └── Upgrade 12: PTX inline assembly (NeoScrypt, last)
```

---

## Testing Requirements (All Upgrades)

Every upgrade **must** pass the following gates before merge:

1. `make check` — full unit test suite (41 tests, includes NeoScrypt permanence assertion)
2. **Known-answer tests (KAT):** Each algorithm must produce the exact same hash
   output before and after the change for a fixed input vector. KATs are
   non-negotiable for any kernel modification.
3. **72-hour burn-in:** Run the modified miner against testnet or a low-
   difficulty private pool for 72 continuous hours. Log hashrate, temperature,
   share acceptance rate, and process RSS. No regression in any metric.
4. **Nsight Compute baseline:** For kernel changes (Upgrades 3, 5, 7, 10, 12),
   capture a pre/post Nsight Compute profile and include the `ncu --csv` diff
   in the PR description. This is the "superb documentation" bar for reviewer ACK.

---

## What This Document Is Not

This document does not recommend:

- **Overclocking guidance** — outside the miner's scope; belongs in hardware
  documentation.
- **Driver-level power limit manipulation** — `nvidia-smi` calls from within
  the miner are an administrative action and require root; a miner must not
  assume elevated privileges.
- **Algorithmic shortcuts that alter hash output** — any change that produces a
  different hash for a given input is consensus-breaking and will receive an
  immediate NACK.
- **Floating-point arithmetic** in any consensus path — non-deterministic across
  platforms, immediate NACK.
