# allow to use Host cuda functions in C/C++
DEF_INCLUDES = @CUDA_INCLUDES@

if WANT_JANSSON
JANSSON_INCLUDES= -I$(top_srcdir)/compat/jansson
else
JANSSON_INCLUDES=
endif

EXTRA_DIST		= autogen.sh README.md README.txt LICENSE.txt \
			  ccminer.sln ccminer.vcxproj ccminer.vcxproj.filters \
			  compat/gettimeofday.c compat/getopt/getopt_long.c

SUBDIRS = compat

bin_PROGRAMS = gbxminer

gbxminer_SOURCES	= elist.h miner.h compat.h \
			  compat/inttypes.h compat/stdbool.h compat/unistd.h \
			  compat/sys/time.h compat/getopt/getopt.h \
			  gbxminer.cpp pools.cpp util.cpp bench.cpp bignum.cpp \
			  api.cpp hashlog.cpp nvml.cpp stats.cpp sysinfos.cpp cuda.cpp \
			  nvsettings.cpp \
			  stubs.cpp \
			  cuda_checkhash.cu \
			  neoscrypt/neoscrypt.cpp neoscrypt/neoscrypt-cpu.c neoscrypt/cuda_neoscrypt.cu \
			  sph/sph_sha2.c

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

# we're now targeting all major compute architectures within one binary.
.cu.o:
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=128 -o $@ -c $<

skein.o: skein.cu
	$(NVCC) $(nvcc_FLAGS) --maxrregcount=64 -o $@ -c $<

