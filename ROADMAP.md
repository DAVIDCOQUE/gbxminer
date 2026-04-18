# GBXminer GPU Mining Roadmap

## v1.1.x — 2026 GPU Refocus

This roadmap tracks the ongoing effort to remove ASIC-dominated or CPU-only
algorithm families and replace them with actively GPU-minable alternatives.
All changes target NVIDIA GPUs (SM 5.0+) on Linux, macOS, and Windows.

---

## Algorithms — Retained

### NeoScrypt (GoByte)
- **Status**: Retained ✅ — **permanent, protected by unit test invariant**
- This algorithm is GoByte's primary proof-of-work. It will never be removed.

---

## Algorithms — Added in v1.1.x

### ETCHash (Ethereum Classic)
- **Status**: **ADDED** ✅ — v1.1.0
- Epoch boundary doubled to every 60 000 blocks (ECIP-1099, activated at block
  11 700 000). Same DAG growth curve as original Ethash.
- Requires ≥ 5 GB VRAM.
- Cannibalised from etcminer (GPL-3.0).
- New files: `etchash/`

### KawPow (Ravencoin, Neurai)
- **Status**: **ADDED** ✅ — v1.1.0
- Ravencoin's ProgPoW variant (KAWPOW). Inner-loop program changes every 3
  blocks and is JIT-compiled via NVRTC. Extremely ASIC-resistant.
- Requires ≥ 6 GB VRAM. Requires `--with-nvrtc` at configure time; builds to
  a clean no-op stub without it.
- Cannibalised from KapowMiner (GPL-3.0).
- New files: `kapow/`

### Autolykos v2 (Ergo)
- **Status**: **ADDED** ✅ — v1.1.0
- k-sum puzzle over N = 2²⁶ BLAKE2b-256 prehash entries (~2 GiB VRAM).
  Table rebuilt per block (no epoch concept); ~1–2 s rebuild on RTX 3080.
- Requires ≥ 3 GB VRAM.
- Cannibalised from Autolykosminer by mhssamadani (GPL-3.0).
  `reduction.cu` and `compaction.cu` written from scratch (not in upstream
  archive).
- New files: `autolykos2/`

---

## Algorithms — Planned (next milestones)

### kHeavyHash (Kaspa)
- **Status**: 🔲 PENDING — v1.2.x
- Kaspa's proof-of-work. Hashes the block header through a sparse matrix
  multiplication followed by HeavyHash (keccak + custom matrix step).
  Very low memory footprint, near-instant kernel startup.
- GPU-viable despite ASIC presence; well-suited to donation-burst windows.
- Requires ≥ 1 GB VRAM (no DAG or epoch management).
- Upstream reference: `KaspaMiner` / `lolMiner` open-source kernel (GPL-3.0).
- **Implementation plan**: `kheavyhash/` directory, single `.cu` kernel.

### ZelHash / MiniZcash (Flux)
- **Status**: 🔲 PENDING — v1.2.x
- Equihash 125,4 variant used by Flux. The equihash base code is already
  present in `equi/`; ZelHash requires only the Flux personalisation string
  (`"ZcashPoW"` → `"FluxPoW"`) and Flux stratum extensions.
- Requires ≥ 6 GB VRAM.
- **Implementation plan**: extend `equi/equi-stratum.cpp` and
  `equi/cuda_equi.cu` with the ZelHash personalisation; add
  `ALGO_ZELHASH` dispatch in `gbxminer.cpp`.

### FiroPow (Firo)
- **Status**: 🔲 PENDING — v1.2.x
- Firo's ProgPoW derivative (replaced MTP in 2021). Parameters differ from
  KawPow: `PROGPOW_PERIOD = 13`, `EPOCH_LENGTH = 1300`, shorter DAG growth.
- ASIC-resistant. Requires ≥ 4 GB VRAM.
- Can reuse the KawPow NVRTC/ProgPoW infrastructure (`kapow/ProgPow.cpp`)
  with a separate parameter set; no full kernel rewrite needed.
- **Implementation plan**: `firopow/` directory, thin wrapper over
  `kapow/ProgPow.cpp` with Firo-specific constants, new `ALGO_FIROPOW`
  dispatch.

---

## Algorithms — Removed in v1.1.x

| Family | Coins | Reason |
|---|---|---|
| X-series (X11–X17) | Dash and derivatives | Full ASIC domination since 2018 |
| X11-dependent (hsr, sonoa, zr5) | Various | ASIC-dominated; share X11 internals |
| Blake-ASIC (decred, pentablake, vanilla) | Decred, Vertcoin | 100% ASIC territory |
| CryptoNight family (cryptonight, cryptolight, monero, graft, stellite, wildkeccak) | XMR and forks | Monero migrated to RandomX (CPU-only) in 2019 |
| Scrypt / Scrypt-Jane | Litecoin, Dogecoin | ASIC-dominated since 2014 |

### Still in binary — planned removal in v1.2.x

The following families remain compiled but are unreachable via the algo enum.
They will be excised in v1.2.x once the new algo additions are complete:

- **Groestl / Myriad-Groestl** — dead or ASIC-dominated networks
- **Skein / Skein2** — dead networks
- **Quark / Qubit / Keccakc** — ghost networks with zero viable GPU hashrate

---

## Master Implementation Status

| Algorithm | Coin(s) | VRAM | v1.1.x Status |
|---|---|---|---|
| NeoScrypt | GoByte | 1 GB | Retained ✅ |
| ETCHash | Ethereum Classic | 5 GB+ | Added ✅ |
| KawPow | Ravencoin, Neurai | 6 GB+ | Added ✅ |
| Autolykos v2 | Ergo | 3 GB+ | Added ✅ |
| kHeavyHash | Kaspa | 1 GB | 🔲 Pending |
| ZelHash | Flux | 6 GB+ | 🔲 Pending |
| FiroPow | Firo | 4 GB+ | 🔲 Pending |
| X-series (X11–X17) | — | — | Removed ✅ |
| Blake-ASIC | — | — | Removed ✅ |
| CryptoNight family | — | — | Removed ✅ |
| Scrypt / Scrypt-Jane | — | — | Removed ✅ |
| Groestl / Skein / Quark | — | — | Planned removal (v1.2.x) |

---

## Recommended Configuration (2026)

For RTX 3000/4000 series GPUs:

- **Primary** — Autolykos v2 (Ergo): best efficiency-to-profitability ratio
  for mid-range cards; no epoch rebuilds during a mining session.
- **Heavy GPU** — KawPow (Ravencoin): maximum ASIC resistance; high VRAM
  and power draw.
- **Stable DAG** — ETCHash (Ethereum Classic): predictable DAG, low
  management overhead.
- **Donation/burst** — kHeavyHash (Kaspa): near-instant startup, minimal
  heat; ideal for 15-minute developer-fee windows *(pending)*.
- **GoByte ecosystem** — NeoScrypt: always available regardless of market
  conditions.
