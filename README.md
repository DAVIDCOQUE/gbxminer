# GBXminer - Fastest nVidia GPU Miner

**GBXminer** is a fast nVidia GPU miner supporting multiple algorithms, built from a fork of ccminer.

## Supported Algorithms

- **NeoScrypt** (primary) - GoByte, Feathercoin
- **Lyra2**: lyra2, lyra2v2, lyra2v3, lyra2z
- **Quark/Qubit**: quark, qubit, nist5
- **Groestl Family**: groestl, myr-gr
- **Skein Family**: skein, skein2
- **Blake Family**: decred
- **SHA-256**: sha256d, sha256t, sha256q
- **Scrypt**: scrypt, scrypt-jane
- **Cryptonight**: cryptonight, cryptolight, monero, graft, stellite
- **Other**: allium, bmw, dmd-gr, equihash, fugue256, heavy, hsr, jackpot, jha, lbry, luffa, mjollnir, pentablake, sia, sonoa, whirlcoin, whirlpool, wildkeccak, zr5, keccak, keccakc

## Features

- Support for all modern nVidia GPUs (GTX 900 series to RTX 5090)
- Built with CUDA 12 for maximum performance
- Available for Linux (x86_64) and Windows (x86_64)
- Built-in API for monitoring and control
- GPU overclocking and power management support
- stratum and getblocktemplate (GBT) protocol support

## Performance

GBXminer is designed to be the fastest miner available on GitHub for many algorithms, with:
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
- NVIDIA GPU: Maxwell architecture or newer.
- NVIDIA Driver 525.60+ recommended (Required for CUDA 12 compatibility).
- CUDA Runtime: libcudart.so.12 (Install via 'cuda-runtime-12' or full Toolkit).
- Jansson (libjansson4) - For JSON parsing.
- Curl (libcurl4) - For network communication.
- OpenSSL (libcrypto.so.3) - For secure connections.

### Build Requirements (for building from source)
- CUDA Toolkit 12.0+
- OpenSSL Development (libssl-dev)
- Curl Development (libcurl4-openssl-dev)
- Jansson Development (libjansson-dev)
- pthreads
- GCC/G++ with C++11 support


## Usage

### Linux
```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password
```

### Windows
```cmd
gbxminer.exe -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password
```

### Benchmark Mode
```bash
./gbxminer --benchmark -a neoscrypt --time-limit=60
```

### Multiple GPUs
```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password -d 0,1,2
```

### API for Monitoring (default port 4068)
```bash
# Enable API on default port
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password -b 127.0.0.1:4068

# Enable remote API access
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password -b 0.0.0.0:4068 --api-remote
```

### Options
```
General:
  -a, --algo=ALGO       Specify algorithm (neoscrypt, x16r, x16s, quark, etc.)
  -o, --url=URL         Pool URL
  -u, --user=USER      Wallet/username
  -p, --pass=PASSWORD  Password/worker name
  -O, --userpass=U:P   username:password pair
  -x, --proxy=URL      Connect through proxy (http://host:port)
  -c, --config=FILE    Load JSON configuration file
  -B, --background     Run in background
  -V, --version        Show version
  -h, --help           Show help

GPU Options:
  -d, --devices=DEV    GPU device IDs (0,1,2,...)
  -i, --intensity=N    GPU intensity 8.0-25.0 (default: auto)
  -t, --threads=N      Number of miner threads (default: number of GPUs)
  -l, --launch-config  Kernel launch configuration per GPU
  -L, --lookup-gap     Memory lookup gap for scrypt/lyra2

Networking:
  -r, --retries=N      Number of retries (default: unlimited)
  -R, --retry-pause=N Pause between retries in seconds (default: 30)
  -T, --timeout=N     Network timeout in seconds (default: 300)
  -s, --scantime=N    Upper bound on time scanning work (default: 60)
  -n, --ndevs         List CUDA devices
  -N, --statsavg=N    Number of samples for hashrate (default: 30)

Monitoring:
  -b, --api-bind=PORT  API bind address (default: 127.0.0.1:4068)
      --api-remote     Allow remote control
      --api-allow=IP   Allowed API clients (IP/mask)
  -q, --quiet         Disable per-thread hashmeter output
      --no-color       Disable colored output

Benchmark:
      --benchmark      Run in benchmark mode
      --time-limit=N   Benchmark duration in seconds

GPU Tuning:
      --gpu-clock=N    Set GPU engine clock (MHz)
      --mem-clock=N    Set GPU memory clock (MHz)
      --plimit=W       Set power limit ( watts or %)
      --tlimit=N       Set thermal limit (degrees C)
      --max-temp=N     Stop mining if GPU exceeds temp
      --led=N          Set GPU LED level (0-255)

Debug:
  -D, --debug         Enable debug output
  -P, --protocol-dump Verbose protocol dump
```

## Configuration File

GBXminer supports JSON configuration files:
```json
{
  "pools": [
    {
      "url": "stratum+tcp://pool.example.com:3333",
      "user": "wallet.address",
      "pass": "worker_name",
      "algo": "neoscrypt"
    }
  ],
  "devices": [0, 1],
  "gpu-engine": [1000, 1050],
  "gpu-memclock": [2000, 2100],
  "api-bind": "127.0.0.1:4068"
}
```

Then run:
```bash
./gbxminer -c config.json
```

## Troubleshooting

- **No GPU detected**: Ensure NVIDIA driver is installed and `nvidia-smi` works
- **CUDA error**: Ensure CUDA Toolkit 12.0+ is installed and accessible
- **Compilation errors**: Ensure all build requirements are installed (see Build Requirements)
- **API not responding**: Check firewall settings if binding to non-localhost

## Donation

Bitcoin: 1JZTdGfCAgRmLo4vsgMTJ57dCiDT24gT6f (d0wn3d)

GoByte: GT6XDe4RsS8vGpqW5mU3nywkCAanZZ1bT8 (d0wn3d)

## Credits

This project was built on the shoulders of giants:
- Original CUDA project by Christian Buchner & Christian H.
- ccminer by Tanguy Pruvot
- Additional algos by djm34 and alexis78
