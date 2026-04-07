# GBXminer GPU Mining Roadmap

## Algorithms to Keep & Add (The 2026 GPU Focus)

### NeoScrypt (GoByte)
- **Status**: Retained for Ecosystem Support
- **Characteristics**:
  - Legacy algorithm maintained specifically to support the GoByte network.
- **Use Case**: Dedicated mining for GoByte communities.

### kHeavyHash (Kaspa)
- **Status**: Kept for high-efficiency/donation bursts
- **Characteristics**:
  - Optimized for modern NVIDIA GPUs with extremely low power consumption.
  - ASICs run the network today, while GPUs remain technically viable but economically weak.
- **Use Case**: Primary choice for 15-minute donation windows due to rapid startup and low heat generation.

### KawPow (Ravencoin / Neurai)
- **Status**: Standard "heavy" GPU algorithm
- **Characteristics**:
  - High computational intensity that generates significant heat.
  - Very ASIC-resistant.
  - Requires at least 6+ GB VRAM.
- **Use Case**: When ASIC resistance is prioritized over power efficiency.

### Etchash (Ethereum Classic)
- **Status**: Direct successor to ETH mining
- **Characteristics**:
  - Maintains the Ethereum Classic ecosystem.
  - Requires 5+ GB VRAM for DAG files.
- **Use Case**: Profitable operation with efficient graphics cards.

### Autolykos2 (Ergo)
- **Status**: Very efficient for mid-range NVIDIA cards
- **Characteristics**:
  - ASIC-resistant algorithm.
  - Good balance of efficiency and profitability.
- **Use Case**: Mid-range NVIDIA GPU mining.

### ZelHash (Flux)
- **Status**: Equihash variant stable on Linux
- **Characteristics**:
  - Rewards memory bandwidth.
  - Requires 6+ GB VRAM for optimal performance.
- **Use Case**: Flux mining with Linux stability.

### FiroPow (Firo)
- **Status**: **ADD**
- **Characteristics**:
  - Highly GPU-friendly and ASIC-resistant algorithm that replaced MTP.
- **Use Case**: Modern privacy coin mining alternative.

---

## Algorithms to Remove (Dead, ASIC-Dominated, or CPU-Only)

### Cryptonight Family (Monero, Graft, Stellite)
- **Reason for removal**: Monero utilizes RandomX, which is mined on CPUs. Trying to mine this algorithm with a modern GPU is mathematically obsolete and wastes user electricity.

### X Series (X11, X11Evo, X12-X17)
- **Reason for removal**: X11 is utilized by Dash, which supports ASIC mining. No modern GPU can compete with ASIC hardware on the X-series algorithms.

### Scrypt & Scrypt-Jane (Litecoin, Dogecoin)
- **Reason for removal**: Litecoin uses the Scrypt algorithm. Dogecoin and Litecoin utilize Scrypt-based mining, which is heavily dominated by ASIC machines.

### Blake Family (Blake2b, Blake2s, Decred)
- **Reason for removal**: 100% ASIC territory. Leaving these in the miner only provides false hope to users who will see zero shares accepted.

### Equihash (Original) & Zcash
- **Reason for removal**: Zcash remains a top choice for miners interested in Equihash, but base Equihash has heavy ASIC presence. The roadmap should strictly focus on GPU-friendly variants like ZelHash.

### Groestl, Skein, Quark, Qubit
- **Reason for removal**: "Ghost" algorithms. The networks securing these are largely dead, abandoned, or overrun by ASICs. Removing these will drastically clean up the CUDA kernels and reduce binary size.

---

## Recommended Configuration (2026)
For maximum usability with modern NVIDIA GPUs (RTX 3000/4000 series):
- **Primary algorithm**: Autolykos2 or ZelHash (best balance of GPU profitability vs. ASIC interference).
- **Secondary**: KawPow (for strict ASIC-resistant opportunities).
- **Donation Operations**: kHeavyHash (for rapid, low-impact developer fee bursts).
- **Legacy Focus**: NeoScrypt (strictly for GoByte) and Etchash (for Ethereum Classic).
