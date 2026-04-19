# Building GBXminer

GBXminer supports building for Linux and Windows from a single codebase. This guide covers all supported build configurations.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Linux x86_64 on Linux](#linux-x86_64-on-linux)
- [Windows x86_64 on Linux (Cross-Compile)](#windows-x86_64-on-linux-cross-compile)
- [Windows x86_64 on Windows](#windows-x86_64-on-windows)
- [Build Options](#build-options)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Common Requirements

| Component | Version | Notes |
|-----------|---------|-------|
| CUDA Toolkit | 12.0+ | Required for all builds |
| OpenSSL | 1.1+ or 3.0+ | Development libraries |
| libcurl | 7.15.2+ | Development libraries |
| pthreads | - | Standard on Linux, bundled on Windows |
| jansson | 2.7+ | Bundled in compat/ or system lib |

### Platform-Specific Requirements

#### Linux Build (Native)
- GCC/G++ with C++11 support
- autotools (autoconf, automake, libtool)
- make

#### Windows Cross-Compile (on Linux)
- MinGW-w64 toolchain (x86_64-w64-mingw32)
- Windows SDK (for headers)
- Windows versions of dependencies (libcurl, OpenSSL, jansson)

#### Windows Build (Native)
- Visual Studio 2019+ with C++ workload
- CUDA Toolkit 12.0+ for Windows
- Windows SDK

---

## Linux x86_64 on Linux

### Step 1: Install Dependencies

**Debian/Ubuntu:**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    autoconf \
    automake \
    libtool \
    pkg-config \
    libssl-dev \
    libcurl4-openssl-dev \
    libcjson-dev \
    cuda-toolkit-12-0 \
    nvidia-utils-525
```

**RHEL/CentOS/Fedora:**
```bash
sudo dnf install -y \
    gcc \
    gcc-c++ \
    make \
    autoconf \
    automake \
    libtool \
    openssl-devel \
    libcurl-devel \
    jansson-devel \
    cuda-toolkit
```

**Arch Linux:**
```bash
sudo pacman -S --needed \
    base-devel \
    openssl \
    curl \
    jansson \
    cuda
```

### Step 2: Configure

```bash
# The default configure.sh works for standard CUDA installations
./configure.sh

# For non-standard CUDA paths:
CUDA_CFLAGS="-O3 -lineno -Xcompiler -Wall -D_FORCE_INLINES" \
    ./configure CXXFLAGS="-O3 -march=native -D_REENTRANT -falign-functions=16" \
    --with-cuda=/usr/local/cuda --with-nvml=libnvidia-ml.so
```

### Step 3: Build

```bash
make -j$(nproc)
```

### Step 4: Verify

```bash
./gbxminer --version
./gbxminer -n  # List available GPUs
```

The binary will be created at `./gbxminer`.

---

## Windows x86_64 on Linux (Cross-Compile)

### Step 1: Install MinGW-w64

**Debian/Ubuntu:**
```bash
sudo apt-get install -y mingw-w64
```

**RHEL/CentOS/Fedora:**
```bash
sudo dnf install -y mingw64-gcc mingw64-gcc-c++ mingw64-winpthreads-static
```

### Step 2: Prepare Windows Libraries

You need Windows-compatible versions of libcurl, OpenSSL, and jansson.

**Option A: Using Arch Linux AUR (download prebuilt):**
```bash
# Download mingwcurl and mingwopenssl from AUR
git clone https://aur.archlinux.org/mingw-w64-x86_64-curl.git
cd mingw-w64-x86_64-curl
makepkg -s --skippgpcheck
cd ..
git clone https://aur.archlinux.org/mingw-w64-x86_64-openssl.git
cd mingw-w64-x86_64-openssl
makepkg -s --skippgpcheck
```

**Option B: Using vcpkg:**
```bash
# Install vcpkg first
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh

# Install Windows libraries
./vcpkg install curl:x64-windows mingwopenssl:x64-windows mingw-jansson:x64-windows
```

### Step 3: Configure for Cross-Compile

```bash
# Configure for Windows target using MinGW
./configure \
    --host=x86_64-w64-mingw32 \
    --target=x86_64-w64-mingw32 \
    --with-cuda=/usr/local/cuda \
    CXXFLAGS="-O3 -static" \
    LDFLAGS="-static"
```

### Step 4: Build

```bash
make -j$(nproc)
```

### Step 5: Verify

```bash
file gbxminer
# Output should show: gbxminer: PE32+ executable (console) x86-64

# Test run with Wine (optional)
wine gbxminer.exe --version
```

The binary will be created at `./gbxminer` (renamed to `gbxminer.exe` automatically or manually).

---

## Windows x86_64 on Windows

### Option A: Visual Studio (Recommended)

#### Step 1: Install Requirements

1. **Visual Studio 2022** with "Desktop development with C++" workload
2. **CUDA Toolkit 12.0+** (download from NVIDIA)
3. **Windows SDK** (included with VS)

#### Step 2: Open Project

Open `gbxminer.sln` in Visual Studio:

```
File -> Open -> Project/Solution -> gbxminer.sln
```

#### Step 3: Configure Build

1. Right-click solution -> "Configuration Manager"
2. Set platform to "x64"
3. Set configuration to "Release"

#### Step 4: Build

```
Build -> Build Solution
```

Or from command line:
```cmd
devenv gbxminer.sln /Build Release /Project gbxminer
```

#### Step 5: Find Binary

The executable will be in:
```
x64\Release\gbxminer.exe
```

---

### Option B: Command Line with MSVC

#### Step 1: Open Developer Command Prompt

Search for "Developer Command Prompt for VS 2022" in Start menu.

#### Step 2: Configure

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

:: Set CUDA paths
set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.0

:: Run configure
configure
```

#### Step 3: Build

```cmd
nmake /f Makefile.win
```

---

## Build Options

### Changing GPU Architectures

Edit `Makefile.am` to modify supported GPU architectures:

```makefile
# Example: Add RTX 5090 support (sm_100)
nvcc_ARCH += -gencode=arch=compute_100,code=sm_100
```

After editing, regenerate build files:
```bash
./autogen.sh
./configure.sh
make
```

### Custom CUDA Path

```bash
./configure --with-cuda=/path/to/cuda
```

### Enable NVML Support

```bash
./configure --with-nvml=libnvidia-ml.so
```

### Static Build

```bash
./configure LDFLAGS="-static"
make
```

Note: Static builds may not work with CUDA runtime (needs dynamic linking).

---

## Troubleshooting

### CUDA Not Found

```
configure: error: CUDA Toolkit not found
```

**Solution:** Specify CUDA path explicitly:
```bash
./configure --with-cuda=/usr/local/cuda
```

### OpenSSL Errors

```
configure: error: OpenSSL library required
```

**Solution:** Install development headers:
```bash
# Debian/Ubuntu
sudo apt-get install libssl-dev
```

### Missing pthread

```
configure: error: pthread not found
```

**Solution:**
```bash
# Debian/Ubuntu
sudo apt-get install libc6-dev
```

### Cross-Compile: Wrong Architecture

```
file gbxminer: ELF 64-bit LSB executable
```

**Solution:** Ensure `--host=x86_64-w64-mingw32` is set and mingw-gcc is installed.

### Windows: Missing DLLs

The executable requires these DLLs in the same directory or system PATH:
- cudart12_0.dll (CUDA Runtime)
- msvcp140.dll (Visual C++ Runtime)
- ws2_32.dll (Windows Sockets - usually system)

**Solution:** Install CUDA Toolkit on target machine or bundle DLLs.

### Windows: nvml.dll Not Found

**Solution:** Copy `nvml.dll` from CUDA Toolkit `bin` directory to the same folder as `gbxminer.exe`.

---

## Build Artifacts

After successful build, you will have:

| Platform | Binary Location |
|----------|-----------------|
| Linux x86_64 | `./gbxminer` |
| Windows x86_64 | `./gbxminer.exe` |

---

## Credits

- Original CUDA project by Christian Buchner & Christian H.
- ccminer by Tanguy Pruvot
- GBXminer by d0wn3d
