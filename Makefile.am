# allow to use Host cuda functions in C/C++
DEF_INCLUDES = @CUDA_INCLUDES@

# Enable parallel builds by default
MAKEFLAGS = -j$(shell nproc)

if WANT_JANSSON
JANSSON_INCLUDES= -I$(top_srcdir)/compat/jansson
else
JANSSON_INCLUDES=
endif

EXTRA_DIST		= autogen.sh README.md README.txt LICENSE.txt \
			  gbxminer.sln gbxminer.vcxproj gbxminer.vcxproj.filters \
			  compat/gettimeofday.c compat/getopt/getopt_long.c

SUBDIRS = compat

bin_PROGRAMS = gbxminer

gbxminer_SOURCES	= elist.h miner.h compat.h \
			  compat/inttypes.h compat/stdbool.h compat/unistd.h \
			  compat/sys/time.h compat/getopt/getopt.h \
			  crc32.c hefty1.c \
			  gbxminer.cpp pools.cpp util.cpp bench.cpp bignum.cpp \
			  api.cpp hashlog.cpp nvml.cpp stats.cpp sysinfos.cpp cuda.cpp \
			  nvsettings.cpp stubs.cpp \
			  heavy/heavy.cu \
			  heavy/cuda_blake512.cu heavy/cuda_blake512.h \
			  heavy/cuda_combine.cu heavy/cuda_combine.h \
			  heavy/cuda_groestl512.cu heavy/cuda_groestl512.h \
			  heavy/cuda_hefty1.cu heavy/cuda_hefty1.h \
			  heavy/cuda_keccak512.cu heavy/cuda_keccak512.h \
			  heavy/cuda_sha256.cu heavy/cuda_sha256.h \
			  fuguecoin.cpp Algo256/cuda_fugue256.cu sph/fugue.c uint256.h \
			  groestlcoin.cpp cuda_groestlcoin.cu cuda_groestlcoin.h \
			  myriadgroestl.cpp cuda_myriadgroestl.cu \
			  lyra2/lyra2.c lyra2/sponge.c \
			  lyra2/lyra2RE.cu lyra2/cuda_lyra2.cu \
		          lyra2/lyra2REv2.cu lyra2/cuda_lyra2v2.cu \
			  lyra2/lyra2REv3.cu lyra2/cuda_lyra2v3.cu \
			  lyra2/lyra2z.c lyra2/lyra2Z.cu lyra2/cuda_lyra2Z.cu \
			  lyra2/allium.cu \
			  Algo256/cuda_bmw256.cu Algo256/cuda_cubehash256.cu \
			  Algo256/cuda_blake256.cu Algo256/cuda_groestl256.cu \
			  Algo256/cuda_keccak256_sm3.cu Algo256/cuda_keccak256.cu Algo256/cuda_skein256.cu \
			  Algo256/blake256.cu Algo256/keccak256.cu \
			  Algo256/blake2s.cu sph/blake2s.c sph/blake2b.c \
			  Algo256/bmw.cu Algo256/cuda_bmw.cu \
			  blake2b.cu \
			  equi/equi-stratum.cpp \
			  equi/zelhash.cpp \
			  JHA/jha.cu JHA/jackpotcoin.cu JHA/cuda_jha_keccak512.cu \
			  JHA/cuda_jha_compactionTest.cu cuda_checkhash.cu \
			  quark/cuda_jh512.cu quark/cuda_quark_blake512.cu quark/cuda_quark_groestl512.cu quark/cuda_skein512.cu \
			  quark/cuda_bmw512.cu quark/cuda_quark_keccak512.cu \
			  quark/nist5.cu \
			  quark/quarkcoin.cu quark/cuda_quark_compactionTest.cu \
			  neoscrypt/neoscrypt.cpp neoscrypt/neoscrypt-cpu.c neoscrypt/cuda_neoscrypt.cu \
			  sha256/sha256d.cu sha256/cuda_sha256d.cu sha256/sha256t.cu sha256/cuda_sha256t.cu \
			  skein.cu cuda_skeincoin.cu skein2.cpp \
			  sph/bmw.c sph/blake.c sph/groestl.c sph/jh.c sph/keccak.c sph/skein.c \
			  sph/cubehash.c sph/echo.c sph/luffa.c sph/sha2.c sph/shavite.c sph/simd.c \
			  sph/hamsi.c sph/hamsi_helper.c sph/streebog.c \
			  sph/shabal.c sph/whirlpool.c sph/sha2big.c sph/haval.c \
			  sph/ripemd.c sph/sph_sha2.c \
			  lbry/lbry.cu lbry/cuda_sha256_lbry.cu lbry/cuda_sha512_lbry.cu lbry/cuda_lbry_merged.cu

# kHeavyHash (Kaspa, no DAG)
gbxminer_SOURCES += \
			  kheavyhash/kheavyhash.cpp \
			  kheavyhash/kheavyhash_cuda.cu

# Autolykos v2 (Ergo PoW, EIP-0037)
gbxminer_SOURCES += \
			  autolykos2/autolykos2.cpp \
			  autolykos2/autolykos2_cuda.cu \
			  autolykos2/prehash.cu \
			  autolykos2/mining.cu \
			  autolykos2/reduction.cu \
			  autolykos2/compaction.cu

# ETCHash (ECIP-1099 Ethereum Classic PoW)
gbxminer_SOURCES += \
			  etchash/etchash.cpp \
			  etchash/etchash_cuda_miner_kernel.cu

# kawpow.cpp always compiled — provides free_kawpow() stub
# when NVRTC is absent.  ProgPow.cpp and kawpow_cuda_miner_kernel.cpp
# require NVRTC for JIT compilation.
gbxminer_SOURCES += kawpow/kawpow.cpp

# firopow.cpp always compiled — provides free_firopow() stub.
# Shares kawpow/ProgPow.cpp (already listed above under HAVE_NVRTC).
gbxminer_SOURCES += firopow/firopow.cpp

if HAVE_NVRTC
gbxminer_SOURCES += \
			  kawpow/ProgPow.cpp \
			  kawpow/kawpow_cuda_miner_kernel.cpp \
			  firopow/firopow_cuda_miner_kernel.cpp
endif

if !ARCH_ARM64
  # equihash
  gbxminer_SOURCES += equi/equi.cpp equi/blake2/blake2bx.cpp equi/equihash.cpp equi/cuda_equi.cu

endif

if HAVE_NVML
nvml_defs = -DUSE_WRAPNVML
nvml_libs = -ldl
endif

if HAVE_WINDOWS
gbxminer_SOURCES += compat/winansi.c
endif

gbxminer_LDFLAGS  = $(PTHREAD_FLAGS) @CUDA_LDFLAGS@
gbxminer_LDADD    = @LIBCURL@ @JANSSON_LIBS@ @PTHREAD_LIBS@ @WS2_LIBS@ @CUDA_LIBS@ @OPENMP_CFLAGS@ @LIBS@ $(nvml_libs)
gbxminer_CPPFLAGS = @LIBCURL_CPPFLAGS@ @OPENMP_CFLAGS@ $(CPPFLAGS) $(PTHREAD_FLAGS) -fno-strict-aliasing $(JANSSON_INCLUDES) $(DEF_INCLUDES) $(nvml_defs)

if ARCH_ARM64
gbxminer_CPPFLAGS += -DARM64
gbxminer_LDFLAGS += -L@CUDA_LOCAL_PATH@/targets/aarch64-linux/lib
endif

if HAVE_OSX
gbxminer_CPPFLAGS += -I/usr/local/llvm/lib/clang/4.0.0/include
gbxminer_LDFLAGS += -L/usr/local/llvm/lib
gbxminer_LDADD += -lomp
endif

#gbxminer_CPPFLAGS += -DUSE_LIBSODIUM
#gbxminer_LDFLAGS += -Lequi/lib
#gbxminer_LDADD += -lsodium
gbxminer_LDADD += -lcuda

if HAVE_NVRTC
gbxminer_LDADD += -lnvrtc
endif

nvcc_ARCH :=
# CUDA 12+ support - target all architectures from Maxwell to Hopper
# Maxwell (GTX 900 series)
nvcc_ARCH += -gencode=arch=compute_50,code=sm_50
nvcc_ARCH += -gencode=arch=compute_52,code=sm_52
nvcc_ARCH += -gencode=arch=compute_53,code=sm_53
# Pascal (GTX 1000 series)
nvcc_ARCH += -gencode=arch=compute_60,code=sm_60
nvcc_ARCH += -gencode=arch=compute_61,code=sm_61
nvcc_ARCH += -gencode=arch=compute_62,code=sm_62
# Volta (V100, Titan V)
nvcc_ARCH += -gencode=arch=compute_70,code=sm_70
nvcc_ARCH += -gencode=arch=compute_72,code=sm_72
# Turing (RTX 2000 series)
nvcc_ARCH += -gencode=arch=compute_75,code=sm_75
# Ampere (RTX 3000 series, A100, A30)
nvcc_ARCH += -gencode=arch=compute_80,code=sm_80
nvcc_ARCH += -gencode=arch=compute_86,code=sm_86
nvcc_ARCH += -gencode=arch=compute_87,code=sm_87
# Ada Lovelace (RTX 4000 series)
nvcc_ARCH += -gencode=arch=compute_89,code=sm_89
# Hopper (H100, RTX 5000 series)
nvcc_ARCH += -gencode=arch=compute_90,code=sm_90

nvcc_FLAGS = $(nvcc_ARCH) @CUDA_INCLUDES@ -I. @CUDA_CFLAGS@
nvcc_FLAGS += $(JANSSON_INCLUDES) --ptxas-options="-v"
# CUDA 12 defaults to --std=c++17.  Several legacy constructs in scrypt/ and
# equi/ (e.g. variable-length arrays in device code, non-constexpr initialisers)
# are ill-formed under C++17 strict mode.  Pin to C++14, which is the newest
# standard that compiles this tree cleanly without source changes.
nvcc_FLAGS += --std=c++14

# we're now targeting all major compute architectures within one binary.
.cu.o:
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -o $@ -c $<

Algo256/blake256.o: Algo256/blake256.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=64 -o $@ -c $<

Algo256/cuda_bmw.o: Algo256/cuda_bmw.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=76 -o $@ -c $<


heavy/cuda_hefty1.o: heavy/cuda_hefty1.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=80 -o $@ -c $<

qubit/qubit_luffa512.o: qubit/qubit_luffa512.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=80 -o $@ -c $<

quark/cuda_quark_blake512.o: quark/cuda_quark_blake512.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=80 -o $@ -c $<

quark/cuda_jh512.o: quark/cuda_jh512.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=80 -o $@ -c $<

quark/cuda_quark_keccak512.o: quark/cuda_quark_keccak512.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=88 -o $@ -c $<

quark/cuda_quark_compactionTest.o: quark/cuda_quark_compactionTest.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=80 -o $@ -c $<

JHA/cuda_jha_compactionTest.o: JHA/cuda_jha_compactionTest.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=80 -o $@ -c $<

skein.o: skein.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=64 -o $@ -c $<

autolykos2/reduction.o: autolykos2/reduction.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -I autolykos2 -o $@ -c $<

autolykos2/compaction.o: autolykos2/compaction.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -I autolykos2 -o $@ -c $<

kheavyhash/kheavyhash_cuda.o: kheavyhash/kheavyhash_cuda.cu
	$(NVCC) $(nvcc_FLAGS) -I kheavyhash -o $@ -c $<

autolykos2/autolykos2_cuda.o: autolykos2/autolykos2_cuda.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -I autolykos2 -o $@ -c $<

autolykos2/prehash.o: autolykos2/prehash.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -I autolykos2 -o $@ -c $<

autolykos2/mining.o: autolykos2/mining.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -I autolykos2 -o $@ -c $<

etchash/etchash_cuda_miner_kernel.o: etchash/etchash_cuda_miner_kernel.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -o $@ -c $<

