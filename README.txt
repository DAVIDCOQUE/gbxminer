
GBXminer 1.2.5                    " - Fastest nVidia GPU Miner"
---------------------------------------------------------------

***************************************************************
If you find this tool useful and like to support its continuous
          development, then consider a donation.

GBXMiner by d0wn3d@github:
  BTC  : 1JZTdGfCAgRmLo4vsgMTJ57dCiDT24gT6f
  GBX  : GT6XDe4RsS8vGpqW5mU3nywkCAanZZ1bT8

Original ccminer by tpruvot:
  BTC  : 1AJdfCpLWPNoAMDfHF1wD5y8VgKSSTHxPo

***************************************************************

>>> Introduction <<<

GBXminer is a CUDA 12 accelerated mining application supporting multiple algorithms.
Built from a fork of ccminer with optimizations for modern nVidia GPUs (GTX 900 to RTX 5000 series).

This version supports:
- NeoScrypt (GoByte) - PRIMARY, will never be removed
- ETCHash, KawPow, Autolykos v2 (added v1.1.0)
- kHeavyHash, ZelHash, FiroPow (added v1.2.1)
- And other algorithms (see below)


>>> Supported Algorithms <<<

  -a, --algo=ALGO       specify the algorithm to use

                          # Primary Algorithm (Permanent)
                          neoscrypt     GoByte primary PoW — will never be removed

                          # Added in v1.2.1
                          kheavyhash    Kaspa (aliases: kaspa, kas)
                                          No DAG, no epoch. 64x64 matrix per block.
                                          Minimum 1 GB VRAM.
                          zelhash       Flux (aliases: flux, zel)
                                          Equihash 125,4. Minimum 6 GB VRAM.
                          firopow       Firo (aliases: firo, zcoin)
                                          ProgPoW variant. Requires --with-nvrtc.
                                          Minimum 4 GB VRAM.

                          # Added in v1.1.0
                          etchash       Ethereum Classic (alias: etc)
                                          ECIP-1099, epoch every 60 000 blocks.
                                          Minimum 5 GB VRAM.
                          kawpow        Ravencoin, Neurai (aliases: rvn, ravencoin)
                                          ProgPoW. Requires --with-nvrtc.
                                          Minimum 6 GB VRAM.
                          autolykos2    Ergo
                                          k-sum BLAKE2b-256, no epoch, per-block table.
                                          Minimum 3 GB VRAM.

                          # Other Supported Algorithms
                          equihash      Zcash, KMD, HUSH (Equihash 200,9)
                          allium        Garlic
                          bmw           Midnight
                          dmd-gr        Diamond
                          fugue256      Fuguecoin
                          jackpot       Sweepcoin
                          jha           Jackpot
                          keccak        Maxcoin
                          lbry          LBRY Credits
                          luffa         Joincoin
                          lyra2         CryptoCoin
                          lyra2v2       Monacoin
                          lyra2v3       Vertcoin
                          lyra2z        Zerocoin (XZC)
                          nist5         TalkCoin
                          sha256d       Double SHA256
                          sha256t       OneCoin
                          whirlcoin     Whirlcoin
                          whirlpool     Joincoin

                          # REMOVED in v1.2.1 (ASIC-dominated / dead networks)
                          #   groestl, myr-gr, skein, skein2, quark, qubit, keccakc

                          # REMOVED in v1.1.0 (ASIC-dominated / CPU-only)
                          #   x11, x13, x15, x16r, x16s, x17 and all X-series
                          #   decred, pentablake, vanilla (Blake-ASIC)
                          #   cryptonight, monero, graft, stellite, wildkeccak
                          #   scrypt, scrypt-jane

  -d, --devices         gives a comma separated list of CUDA device IDs
                        to operate on. Device IDs start counting from 0!

  -i, --intensity=N[,N] GPU threads per call 8-25 (2^N + F, default: 0=auto)
  -f, --diff-factor     Divide difficulty by this factor (default 1.0)
  -m, --diff-multiplier Multiply difficulty by this value (default 1.0)
  -o, --url=URL         URL of mining server
  -O, --userpass=U:P    username:password pair for mining server
  -u, --user=USERNAME   username for mining server
  -p, --pass=PASSWORD   password for mining server
  -x, --proxy=[PROTOCOL://]HOST[:PORT]  connect through a proxy
  -t, --threads=N       number of miner threads (default: number of GPUs)
  -r, --retries=N       number of times to retry if a network call fails
  -R, --retry-pause=N   time to pause between retries, in seconds (default: 15)
      --shares-limit    maximum shares to mine before exiting
      --time-limit      maximum time [s] to mine before exiting
  -T, --timeout=N       network timeout, in seconds (default: 300)
  -s, --scantime=N      upper bound on time scanning current work (default: 5)
  -n, --ndevs           list cuda devices
  -N, --statsavg        number of samples used to display hashrate (default: 30)
      --no-gbt          disable getblocktemplate support
      --no-longpoll     disable X-Long-Polling support
      --no-stratum      disable X-Stratum support
  -q, --quiet           disable per-thread hashmeter output
      --no-color        disable colored output
  -D, --debug           enable debug output
  -P, --protocol-dump   verbose dump of protocol-level activities
  -b, --api-bind=port   IP:port for the miner API (default: 127.0.0.1:4068)
      --api-remote      Allow remote control
      --api-allow=...   IP/mask of the allowed api client(s), 0/0 for all
      --max-temp=N      Only mine if gpu temp is less than specified value
      --gov-temp=N      Thermal governor soft-limit in degrees C (default: 0 = disabled)
                        Halves GPU work intensity when die temperature reaches N;
                        restores on cool-down. Prevents clock throttle sawtooth.
                        Recommended: set 5°C below card's throttle point (e.g. 78).
                        Requires NVML (Linux). Silently ignored when NVML absent.
      --max-rate=N[KMG] Only mine if net hashrate is less than specified value
      --max-diff=N      Only mine if net difficulty is less than specified value
      --plimit=150W     set the gpu power limit
      --tlimit=85       Set the gpu thermal limit (windows only)
      --keep-clocks     prevent reset clocks and/or power limit on exit
  -B, --background      run the miner in the background
      --benchmark       run in offline benchmark mode
      --cputest         debug hashes from cpu algorithms
  -c, --config=FILE     load a JSON-format configuration file
  -V, --version         display version information and exit
  -h, --help            display this help text and exit


>>> Examples <<<

GoByte (NeoScrypt) pool mining:
    gbxminer -a neoscrypt -o stratum+tcp://pool.gobyte.network:3333 -u WALLET -p x

Kaspa (kHeavyHash) pool mining:
    gbxminer -a kaspa -o stratum+tcp://pool:port -u WALLET -p x

Flux (ZelHash) pool mining:
    gbxminer -a flux -o stratum+tcp://pool:port -u WALLET -p x

Ethereum Classic (ETCHash) pool mining:
    gbxminer -a etchash -o stratum+tcp://pool:port -u WALLET -p x

Firo (FiroPow) pool mining (requires --with-nvrtc build):
    gbxminer -a firo -o stratum+tcp://pool:port -u WALLET -p x

Fuguecoin pool mining:
    gbxminer -q -a fugue256 -o stratum+tcp://pool:port -u WALLET -p x

GoByte solo mining:
    gbxminer -q -s 1 -a neoscrypt -o http://127.0.0.1:12455/ -u USERNAME -p PASSWORD


>>> Configuration files <<<

With the -c parameter you can use a json config file to set your preferred settings.
An example is present in source tree (gbxminer.conf).


>>> API and Monitoring <<<

With the -b parameter you can open your gbxminer to your network, use -b 0.0.0.0:4068 if required.
Default API is enabled for localhost queries only, on port 4068.
You can test it on linux with "telnet <miner-ip> 4068" and type "help".


>>> GBXMINER RELEASE HISTORY <<<

  Apr. 25th 2026  v1.2.5
                  CRITICAL FIX: KawPow (Ravencoin) kernel was producing invalid
                  shares on all previous versions. Four bugs corrected: wrong
                  per-thread nonce (ProgPoW is warp-collaborative), missing
                  keccak_f800 seed hash, missing keccak_f800 final hash, wrong
                  result GID stored for nonce recovery.
                  Added --gov-temp=N: NVML thermal governor. Reduces GPU work
                  intensity before hardware throttle boundary is reached, keeping
                  the boost clock stable instead of sawtoothing. Default: off.
                  ETCHash: DAG allocation now persistent across epoch transitions
                  (no cudaFree/cudaMalloc stall). Light cache staging uses pinned
                  host memory for full PCIe throughput. __ldg() read-only cache
                  applied to all DAG lookups in search kernel (+5-12% sm_75/86).
                  Async CUDA stream pipeline for all per-scan operations.
                  NeoScrypt: CUDA Graph captures four-kernel pipeline on first
                  launch; subsequent scans replay with one driver call instead
                  of six, eliminating per-kernel CPU overhead.
                  KawPow: NVRTC now compiles with --gpu-architecture=compute_XY
                  matching the actual device SM, improving register allocation.
                  Module cache keyed by (period, sm_major, sm_minor).

  2026            v1.2.1
                  Added kHeavyHash (Kaspa), ZelHash (Flux), FiroPow (Firo)
                  Removed Groestl/Myriad-Groestl, Skein/Skein2, Quark/Qubit/Keccakc
                  Fixed ALGO_ZELHASH missing from enum
                  Fixed uint2 operators missing in etchash_keccak.cuh

  2026            v1.1.0
                  Added ETCHash (Ethereum Classic), KawPow (Ravencoin), Autolykos v2 (Ergo)
                  Removed X-series, Blake-ASIC, CryptoNight family, Scrypt/Scrypt-Jane

  Apr. 7th 2026   v1.0.1
                  Rebranded from ccminer to GBXminer
                  CUDA 12 compatibility (texture objects, OpenSSL EVP API)
                  Support for all nVidia GPUs (GTX 900 to RTX 5090)

>>> CCMINER RELEASE HISTORY <<<

  Jan. 30th 2019  v2.3.1
  June 23th 2018  v2.3
  June 10th 2018  v2.2.6
  Apr. 02nd 2018  v2.2.5
  Jan. 04th 2018  v2.2.4
  Dec. 04th 2017  v2.2.3
  Oct. 09th 2017  v2.2.2
  Sep. 01st 2017  v2.2.1
  Aug. 13th 2017  v2.2
  June 16th 2017  v2.1-tribus
  May. 14th 2017  v2.0
  Dec. 21th 2016  v1.8.4
  Mar. 13th 2016  v1.7.5
  Feb. 28th 2016  v1.7.4
  Jan. 26th 2016  v1.7.1
  Nov. 06th 2015  v1.7
  Aug. 28th 2015  v1.6.6
  June 23th 2015  v1.6.5
  May 15th 2015   v1.6.3
  Apr. 21th 2015  v1.6.2
  Mar. 27th 2015  v1.6.0
  Mar. 18th 2014  initial ccminer release


>>> AUTHORS <<<

Christian Buchner, Christian H. (Germany): Initial CUDA implementation
djm34, tsiv, sp and klausT: cuda algo implementation and optimisation
Tanguy Pruvot: 750Ti tuning, blake, colors, zr5, skein, general cleanup,
               API monitoring, linux Config/Makefile and vstudio libs
d0wn3d: GBXminer fork, CUDA 12 port, GoByte/NeoScrypt focus,
        kHeavyHash, ZelHash, FiroPow, ETCHash, KawPow, Autolykos v2

Source code is included to satisfy GNU GPL V3 requirements.

