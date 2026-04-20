# GBXminer - Fastest nVidia GPU Miner

**GBXminer** is a fast nVidia GPU miner supporting multiple algorithms, built from a fork of ccminer.

## Supported Algorithms

| Algorithm | Coin(s) | Flag | VRAM | Notes |
|---|---|---|---|---|
| NeoScrypt | GoByte | `-a neoscrypt` | 1 GB | **Primary — permanent** |
| ETCHash | Ethereum Classic | `-a etchash`, `-a etc` | 5 GB+ | ECIP-1099, epoch every 60 000 blocks |
| KawPow | Ravencoin, Neurai | `-a kawpow`, `-a rvn`, `-a ravencoin` | 6 GB+ | ProgPoW; requires `--with-nvrtc` |
| Autolykos v2 | Ergo | `-a autolykos2` | 3 GB+ | k-sum BLAKE2b-256, no epoch |
| kHeavyHash | Kaspa | `-a kheavyhash`, `-a kaspa`, `-a kas` | 1 GB | No DAG, matrix per block |
| ZelHash | Flux | `-a zelhash`, `-a flux`, `-a zel` | 6 GB+ | Equihash 125,4 |
| FiroPow | Firo | `-a firopow`, `-a firo`, `-a zcoin` | 4 GB+ | ProgPoW; requires `--with-nvrtc` |
| Equihash | Zcash, KMD, HUSH | `-a equihash` | 1 GB+ | Equihash 200,9 |
| Lyra2 family | various | `-a lyra2`, `-a lyra2v2`, `-a lyra2v3`, `-a lyra2z` | — | — |
| Allium | Garlic | `-a allium` | — | — |
| BMW | Midnight | `-a bmw` | — | — |
| DMD-GR | Diamond | `-a dmd-gr` | — | — |
| Fugue256 | Fuguecoin | `-a fugue256` | — | — |
| Jackpot / JHA | Sweepcoin | `-a jackpot`, `-a jha` | — | — |
| Keccak | Maxcoin | `-a keccak` | — | — |
| LBRY | LBRY Credits | `-a lbry` | — | — |
| Luffa | Joincoin | `-a luffa` | — | — |
| NIST5 | TalkCoin | `-a nist5` | — | — |
| SHA-256d / SHA-256t | various | `-a sha256d`, `-a sha256t` | — | — |
| Whirlpool / Whirlcoin | Joincoin | `-a whirlpool`, `-a whirlcoin` | — | — |

## Features

- Support for all modern nVidia GPUs (GTX 900 series to RTX 5090)
- Built with CUDA 12 for maximum performance
- Available for Linux (x86_64) and Windows (x86_64)
- Built-in API for monitoring and control
- GPU overclocking and power management support
- Stratum and getblocktemplate (GBT) protocol support

## Supported nVidia Architectures

| Architecture | GPUs |
|---|---|
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
- NVIDIA GPU: Maxwell architecture or newer
- NVIDIA Driver 525.60+ (required for CUDA 12 compatibility)
- CUDA Runtime: libcudart.so.12
- Jansson (libjansson4)
- Curl (libcurl4)
- OpenSSL (libcrypto.so.3)

### Build Requirements
- CUDA Toolkit 12.0+
- OpenSSL Development (libssl-dev)
- Curl Development (libcurl4-openssl-dev)
- Jansson Development (libjansson-dev)
- pthreads
- GCC/G++ with C++11 support
- nvrtc (required for KawPow and FiroPow only — already enabled in `configure.sh`)

## Usage

### Basic
```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password
```

### Multiple GPUs
```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password -d 0,1,2
```

### Benchmark
```bash
./gbxminer --benchmark -a neoscrypt --time-limit=60
```

### API Monitoring (default port 4068)
```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password -b 127.0.0.1:4068
```

### Options
```
General:
  -a, --algo=ALGO       Specify algorithm
  -o, --url=URL         Pool URL
  -u, --user=USER       Wallet/username
  -p, --pass=PASSWORD   Password/worker name
  -O, --userpass=U:P    username:password pair
  -x, --proxy=URL       Connect through proxy
  -c, --config=FILE     Load JSON configuration file
  -B, --background      Run in background
  -V, --version         Show version
  -h, --help            Show help

GPU Options:
  -d, --devices=DEV     GPU device IDs (0,1,2,...)
  -i, --intensity=N     GPU intensity 8.0-25.0 (default: auto)
  -t, --threads=N       Number of miner threads (default: number of GPUs)
  -l, --launch-config   Kernel launch configuration per GPU
  -L, --lookup-gap      Memory lookup gap

Networking:
  -r, --retries=N       Number of retries (default: unlimited)
  -R, --retry-pause=N   Pause between retries in seconds (default: 30)
  -T, --timeout=N       Network timeout in seconds (default: 300)
  -s, --scantime=N      Upper bound on time scanning work (default: 60)
  -n, --ndevs           List CUDA devices
  -N, --statsavg=N      Number of samples for hashrate (default: 30)

Monitoring:
  -b, --api-bind=PORT   API bind address (default: 127.0.0.1:4068)
      --api-remote      Allow remote control
      --api-allow=IP    Allowed API clients (IP/mask)
  -q, --quiet           Disable per-thread hashmeter output
      --no-color        Disable colored output

Benchmark:
      --benchmark       Run in benchmark mode
      --time-limit=N    Benchmark duration in seconds

GPU Tuning:
      --gpu-clock=N     Set GPU engine clock (MHz)
      --mem-clock=N     Set GPU memory clock (MHz)
      --plimit=W        Set power limit (watts or %)
      --tlimit=N        Set thermal limit (degrees C)
      --max-temp=N      Stop mining if GPU exceeds temp

Debug:
  -D, --debug           Enable debug output
  -P, --protocol-dump   Verbose protocol dump
```

## Configuration File

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
  "api-bind": "127.0.0.1:4068"
}
```

```bash
./gbxminer -c config.json
```

## Release History

| Version | Date | Summary |
|---|---|---|
| v1.2.1 | 2026 | Added kHeavyHash, ZelHash, FiroPow. Removed Groestl, Skein, Quark families. Fixed KawPow/FiroPow NVRTC build. |
| v1.1.0 | 2026 | Added ETCHash, KawPow, Autolykos v2. Removed X-series, CryptoNight, Scrypt, Blake-ASIC. |
| v1.0.1 | Apr 2026 | Rebranded from ccminer to GBXminer. CUDA 12 support. |

## Donation

Bitcoin: 1JZTdGfCAgRmLo4vsgMTJ57dCiDT24gT6f (d0wn3d)

GoByte: GT6XDe4RsS8vGpqW5mU3nywkCAanZZ1bT8 (d0wn3d)

## Credits

- Original CUDA project by Christian Buchner & Christian H.
- ccminer by Tanguy Pruvot
- Additional algos by djm34 and alexis78
