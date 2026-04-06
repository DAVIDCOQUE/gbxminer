# GBXminer Agent Instructions

GBXminer is a fast nVidia GPU miner supporting 60+ algorithms, built from a fork of ccminer. Supports NeoScrypt (primary), X series (X11-X17), Lyra2, Quark/Qubit, Groestl, Skein, Blake, Scrypt, Cryptonight, and many more across nVidia GPU architectures (GTX 900 to RTX 5090). Built with CUDA 12.

## Build Commands

```bash
./autogen.sh    # Regenerate autotools (after configure.ac/Makefile.am changes)
./configure.sh  # Configure with CUDA paths
make -j4        # Build
```

**Never hand-edit `Makefile.in`** - always regenerate via `autogen.sh`.

## CUDA Requirements

- CUDA 12.0+ (minimum compute target: sm_50/Maxwell)
- Edit `Makefile.am` to change GPU architectures, then run `./build.sh`
- NVCC pinned to `--std=c++14` (C++17 breaks legacy scrypt/equi code)

## Key Quirks (CUDA 12 / OpenSSL 3.0)

- **Texture objects**: Use `cuda_texture_helper.h` macros (`CREATE_TEXTURE_OBJECT_1D`, `CREATE_TEXTURE_OBJECT_2D`) for CUDA 12 compatibility
- **Texture usage**: Use `tex1Dfetch<Type>(texObj, index)` not `tex1D`; cast index to `(unsigned int)` to avoid const errors
- **Cleanup**: Always call `cudaDestroyTextureObject()` for every created texture object
- **Pragma unroll**: Use `#pragma unroll 1` instead of `#pragma nounroll` (CUDA 12 compatible)
- **Thread sync**: Use `cudaDeviceSynchronize()` not deprecated `cudaThreadSynchronize()`
- **OpenSSL**: Use EVP API (`EVP_DigestInit_ex`, `EVP_sha256`) not deprecated `SHA256_*` functions

## Project Structure

- Main entrypoint: `gbxminer.cpp`
- Algorithms: separate `.cu` files per algorithm (x11/, x16/, quark/, neoscrypt/, etc.)
- Algorithm definitions: `algos.h` (enum + algo_names array)
- Dependencies: libcurl, OpenSSL, jansson, pthreads
- Autotools: `configure.ac` + `Makefile.am`

## Architecture Notes

- Multiple algorithm support via separate `.cu` kernel files
- Each algorithm has host implementation (`.cpp`/`.c`) and CUDA kernel (`.cu`)
- `gbxminer-config.h` generated during configure (not in repo)
- JSON config file support via `-c` flag

## Code Standards

- C: C99 (`-std=c99`)
- C++: C++11 (`-std=c++11`)
- NVCC: `--std=c++14` (pinned)

## Common CLI Options

```bash
./gbxminer -a neoscrypt -o stratum+tcp://pool:port -u wallet.address -p password -d 0,1
./gbxminer --benchmark -a neoscrypt --time-limit=60
./gbxminer -c config.json
./gbxminer -b 127.0.0.1:4068 --api-remote
```

- `-a, --algo` - Algorithm name (see `algos.h` for full list)
- `-o, --url` - Pool URL
- `-d, --devices` - GPU device IDs (comma-separated)
- `-l, --launch-config` - Kernel launch config per GPU
- `-b, --api-bind` - API port (default 4068)
- `--benchmark` / `--time-limit` - Benchmark mode
- `-D, --debug` - Debug output
- `-c, --config` - JSON config file