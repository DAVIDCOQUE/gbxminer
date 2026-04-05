# GBXminer - Fastest NeoScrypt Miner

**GBXminer** is the fastest NeoScrypt nVidia GPU miner, built from a fork of ccminer.

## Features

- Optimized for NeoScrypt algorithm only
- Support for all modern nVidia GPUs (GTX 900 series to RTX 5090)
- Built with CUDA 12 for maximum performance
- Available for Linux (x86_64) and Windows (x86_64)

## Performance

GBXminer is designed to be the fastest NeoScrypt miner available on GitHub, with:
- Optimized CUDA kernels
- Support for all major GPU architectures
- Efficient memory management

## Supported nVidia Architectures

| Architecture | GPUs |
|--------------|------|
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

## Requirements

### Runtime Requirements
- NVIDIA GPU with NeoScrypt support
- NVIDIA Driver (460+ recommended)
- CUDA Toolkit 12.0+ runtime (included with driver)

### Build Requirements (for building from source)
- CUDA Toolkit 12.0+
- OpenSSL development libraries
- Curl development libraries
- pthreads
- GCC/G++ with C++11 support

## Downloads

Pre-built binaries for v1.0.0:
- **Linux x86_64**: `gbxminer-linux-x64-v1.0.0`
- **Windows x86_64**: `gbxminer-windows-x64-v1.0.0.exe`

## Usage

### Linux
```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password
```

### Windows
```cmd
gbxminer.exe -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password
```

### Example with NiceHash
```bash
./gbxminer -a neoscrypt -o stratum+tcp://neoscrypt.eu.nicehash.com:3344 -u WALLET_ADDRESS.WORKER_NAME -p x
```

### Options
```
-a, --algo=ALGO       Specify algorithm (neoscrypt)
-o, --url=URL         Pool URL
-u, --user=USER       Wallet/username
-p, --pass=PASSWORD   Password/worker name
-i, --intensity=N    GPU intensity (default: auto)
-d, --devices=DEV     GPU device IDs (0,1,2,...)
    --benchmark       Run benchmark mode
    --time-limit=N    Benchmark time in seconds
-h, --help            Show help
-V, --version         Show version
```

## Building from Source

### Linux
```bash
./autogen.sh
./configure
make
```

### Static Build (Linux)
```bash
make LDFLAGS="-static-libstdc++"
```

## Donation

BTC: 1JZTdGfCAgRmLo4vsgMTJ57dCiDT24gT6f (d0wn3d)

## Credits

This project was built on the shoulders of giants:
- Original CUDA project by Christian Buchner & Christian H.
- ccminer by Tanguy Pruvot
- Additional algos by djm34 and alexis78

## License

GPL v3 - See LICENSE file