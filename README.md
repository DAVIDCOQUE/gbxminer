# GBXminer - Fastest NeoScrypt Miner

**GBXminer** is the fastest NeoScrypt nVidia GPU miner, built from a fork of ccminer (by tpruvot).

## Features

- Optimized for NeoScrypt algorithm only (for now)
- Support for all modern nVidia GPUs (GTX 900 series to RTX 5090)
- Built with CUDA 12 for maximum performance
- Linux optimized (tested on Ubuntu 24.04)

## Performance

GBXminer is designed to be the fastest NeoScrypt miner available on GitHub, with:
- Optimized CUDA kernels
- Support for all major GPU architectures
- Efficient memory management

## Supported nVidia Architectures

| Architecture | GPUs |
|--------------|------|
| sm_30 | Kepler (GTX 600 series) |
| sm_35 | Kepler (GTX 700 series) |
| sm_37 | Kepler (Tesla K80) |
| sm_50 | GTX 900 series |
| sm_52 | Maxwell GM20x |
| sm_53 | Maxwell (Tegra X1) |
| sm_60 | GTX 1000 series |
| sm_61 | Pascal GP104/106 |
| sm_62 | Pascal GP100 |
| sm_70 | Volta (V100) |
| sm_72 | Volta (faster) |
| sm_75 | Turing (RTX 2000 series) |
| sm_80 | Ampere A100 |
| sm_86 | Ampere (RTX 3000 series) |
| sm_87 | Ampere (Tesla A30) |
| sm_89 | Ada Lovelace (RTX 4000 series) |
| sm_90 | Hopper (H100, RTX 5000 series) |

## Build Requirements

- CUDA Toolkit 12.0+
- OpenSSL
- Curl
- pthreads

### Compile on Linux

```bash
./autogen.sh
./configure
make
```

## Usage

```bash
./gbxminer -a neoscrypt -o pool-url -u wallet-address -p password
```

## Donation

BTC: 1JZTdGfCAgRmLo4vsgMTJ57dCiDT24gT6f (d0wn3d)

This project was built on the shoulders of giants:
- Original CUDA project by Christian Buchner & Christian H.
- ccminer by Tanguy Pruvot
- Additional algos by djm34 and alexis78
