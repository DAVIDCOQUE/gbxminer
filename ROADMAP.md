# GBXminer GPU Mining Roadmap

---

## v1.1.0 — Released

Added ETCHash, KawPow, and Autolykos v2.  Removed all ASIC-dominated
and CPU-only algorithm families.

### Added in v1.1.0

| Algorithm | Coin(s) | VRAM | Notes |
|---|---|---|---|
| ETCHash | Ethereum Classic | 5 GB+ | ECIP-1099, epoch every 60 000 blocks |
| KawPow | Ravencoin, Neurai | 6 GB+ | ProgPoW, period=3, requires `--with-nvrtc` |
| Autolykos v2 | Ergo | 3 GB+ | k-sum BLAKE2b-256, no epoch, per-block table |

### Removed in v1.1.0

| Family | Reason |
|---|---|
| X-series (X11–X17, hsr, sonoa, zr5) | Full ASIC domination since 2018 |
| Blake-ASIC (decred, pentablake, vanilla) | 100% ASIC territory |
| CryptoNight (cryptonight, cryptolight, monero, graft, stellite, wildkeccak) | Monero → RandomX (CPU-only) in 2019 |
| Scrypt / Scrypt-Jane | ASIC-dominated since 2014 |

---

## v1.2.x — Current Development Branch

Adds kHeavyHash, ZelHash, and FiroPow.  Removes the remaining ghost
algorithm families still compiled in the binary from v1.1.0.

### Added in v1.2.x

#### kHeavyHash (Kaspa)
- **Status**: **ADDED** ✅
- No DAG, no epoch.  The 64×64 matrix is derived from each block header
  on-device in microseconds via xoshiro256** PRNG.  Near-instant startup;
  ideal for donation-burst windows.
- Requires ≥ 1 GB VRAM.
- Aliases: `-a kaspa`, `-a kas`
- New files: `kheavyhash/`

#### ZelHash (Flux)
- **Status**: **ADDED** ✅
- Equihash 125,4.  Reuses the existing `cuda_equi.cu` templated solver via
  CONFIG_MODE_4 (RB=5, SM=10, SSM=6, THREADS=512).  Solution is 52 bytes
  (vs 1344 for Equihash 200,9).
- Requires ≥ 6 GB VRAM.
- Aliases: `-a flux`, `-a zel`
- New files: `equi/zelhash.cpp`; modified `equi/equihash.h`, `equi/equi.cpp`,
  `equi/cuda_equi.cu`, `equi/eqcuda.hpp`

#### FiroPow (Firo)
- **Status**: **ADDED** ✅
- ProgPoW with EPOCH_LENGTH=1300 and PERIOD=13 (vs 7500/3 for KawPow).
  Shares `kapow/ProgPow.cpp` unchanged; only the two period/epoch constants
  differ.  Requires `--with-nvrtc` at configure time.
- Requires ≥ 4 GB VRAM.
- Aliases: `-a firo`, `-a zcoin`
- New files: `firopow/`

### Removed in v1.2.x

The following families were still compiled in v1.1.0 but unreachable via
the algo enum.  They are excised in v1.2.x:

| Family | Reason |
|---|---|
| Groestl / Myriad-Groestl | Dead or ASIC-dominated networks |
| Skein / Skein2 | Dead networks (Woodcoin irrelevant) |
| Quark / Qubit / Keccakc | Ghost networks, zero viable GPU hashrate |

---

## Permanent

### NeoScrypt (GoByte)
- **Status**: Retained ✅ — **will never be removed**
- GoByte's primary proof-of-work.  Protected by a hard invariant in the
  unit test suite (`test_neoscrypt_permanent`).

---

## Master Status Table

| Algorithm | Coin(s) | VRAM | Version |
|---|---|---|---|
| NeoScrypt | GoByte | 1 GB | Permanent ✅ |
| ETCHash | Ethereum Classic | 5 GB+ | v1.1.0 ✅ |
| KawPow | Ravencoin, Neurai | 6 GB+ | v1.1.0 ✅ |
| Autolykos v2 | Ergo | 3 GB+ | v1.1.0 ✅ |
| kHeavyHash | Kaspa | 1 GB | v1.2.x ✅ |
| ZelHash | Flux | 6 GB+ | v1.2.x ✅ |
| FiroPow | Firo | 4 GB+ | v1.2.x ✅ |
| X-series (X11–X17) | — | — | Removed v1.1.0 ✅ |
| CryptoNight family | — | — | Removed v1.1.0 ✅ |
| Scrypt / Scrypt-Jane | — | — | Removed v1.1.0 ✅ |
| Blake-ASIC | — | — | Removed v1.1.0 ✅ |
| Groestl / Skein / Quark | — | — | Removed v1.2.x ✅ |

---

## Recommended Configuration (2026)

For RTX 3000/4000 series GPUs:

| Goal | Algorithm | Why |
|---|---|---|
| Best efficiency | Autolykos v2 (Ergo) | No epoch rebuild; low VRAM; good ERG profitability |
| ASIC resistance | KawPow (Ravencoin) | Maximum resistance; high VRAM and power draw |
| Stable DAG | ETCHash (Ethereum Classic) | Predictable DAG; low management overhead |
| Donation/burst | kHeavyHash (Kaspa) | Near-instant startup; minimal heat; 1 GB VRAM |
| Privacy coin | FiroPow (Firo) | ASIC-resistant ProgPoW replacement for MTP |
| GoByte ecosystem | NeoScrypt | Always available; GoByte primary PoW |
