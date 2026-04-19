AC_INIT([gbxminer], [1.0.0], [], [gbxminer], [http://github.com/d0wn3d/gbxminer])

AC_PREREQ([2.60])
AC_CONFIG_SRCDIR([gbxminer.cpp])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_HEADERS([gbxminer-config.h])

dnl Make sure anyone changing configure.ac/Makefile.am has a clue
AM_MAINTAINER_MODE

dnl Checks for programs
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AS
AC_PROG_RANLIB
AC_PROG_CXX
AC_OPENMP

dnl Checks for header files
AC_CHECK_HEADERS([sys/endian.h sys/param.h syslog.h])
# sys/sysctl.h requires sys/types.h on FreeBSD
# sys/sysctl.h requires sys/param.h on OpenBSD
AC_CHECK_HEADERS([sys/sysctl.h], [], [],
[#include <sys/types.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
])

AC_CHECK_DECLS([be32dec, le32dec, be32enc, le32enc], [], [],
[AC_INCLUDES_DEFAULT
#ifdef HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#endif
])

AC_FUNC_ALLOCA
AC_CHECK_FUNCS([getopt_long])

case $target in
  i*86-*-*)
    have_x86=true
    ;;
  x86_64-*-*)
    have_x86=true
    have_x86_64=true
    ;;
  amd64-*-*)
    have_x86=true
    have_x86_64=true
    ;;
  aarch64-*)
    have_arm64=true
    ;;
esac

PTHREAD_FLAGS="-pthread"
WS2_LIBS=""

case $target in
  *-*-mingw*)
    have_win32=true
    PTHREAD_FLAGS=""
    WS2_LIBS="-lws2_32"
    ;;
  *86*-apple-darwin*)
    have_osx=true
    PTHREAD_FLAGS=""
    ;;
esac

AC_CHECK_LIB(jansson, json_loads, request_jansson=false, request_jansson=true)
AC_CHECK_LIB([pthread], [pthread_create], PTHREAD_LIBS="-lpthread",
  AC_CHECK_LIB([pthreadGC2], [pthread_create], PTHREAD_LIBS="-lpthreadGC2",
    AC_CHECK_LIB([pthreadGC1], [pthread_create], PTHREAD_LIBS="-lpthreadGC1",
      AC_CHECK_LIB([pthreadGC], [pthread_create], PTHREAD_LIBS="-lpthreadGC"
))))

AC_CHECK_LIB([z],[gzopen], [], [])
AC_CHECK_LIB([ssl],[SSL_free], [], [AC_MSG_ERROR([OpenSSL library required])])
AC_CHECK_LIB([crypto],[EVP_DigestFinal_ex], [], [AC_MSG_ERROR([OpenSSL library required])])

AM_CONDITIONAL([WANT_JANSSON], [test x$request_jansson = xtrue])
AM_CONDITIONAL([HAVE_WINDOWS], [test x$have_win32 = xtrue])
AM_CONDITIONAL([HAVE_OSX], [test x$have_osx = xtrue])
AM_CONDITIONAL([ARCH_x86], [test x$have_x86 = xtrue])
AM_CONDITIONAL([ARCH_x86_64], [test x$have_x86_64 = xtrue])
AM_CONDITIONAL([ARCH_ARM64], [test x$have_arm64 = xtrue])

if test x$request_jansson = xtrue
then
	JANSSON_LIBS="compat/jansson/libjansson.a"
else
	JANSSON_LIBS=-ljansson
fi

LIBCURL_CHECK_CONFIG(, 7.15.2, ,
  [AC_MSG_ERROR([Missing required libcurl >= 7.15.2])])

AC_SUBST(JANSSON_LIBS)
AC_SUBST(PTHREAD_FLAGS)
AC_SUBST(PTHREAD_LIBS)
AC_SUBST(WS2_LIBS)

AC_CONFIG_FILES([
	Makefile
	compat/Makefile
	compat/jansson/Makefile
	])

dnl find out what version we are running
ARCH=`uname -m`
if [[ $ARCH == "x86_64" ]];
then
  SUFFIX="64"
else
  SUFFIX=""
fi

if test x$have_osx = xtrue
then
  SUFFIX=""
fi

dnl Setup CUDA paths
AC_ARG_WITH([cuda],
   [  --with-cuda=PATH    prefix where cuda is installed [default=/usr/local/cuda]])

AC_ARG_WITH([nvml],
   [  --with-nvml=PATH    prefix where libnvml is installed [default=/usr/lib]])

AM_CONDITIONAL([HAVE_NVML], [test -n "$with_nvml"])

NVCC="nvcc"

if test -n "$with_cuda" ; then
  CUDA_LOCAL_PATH=$with_cuda
  if test -x "$with_cuda/bin/nvcc" ; then
    NVCC="$with_cuda/bin/nvcc"
  fi
else
  CUDA_LOCAL_PATH=/usr/local/cuda
fi
AC_SUBST(CUDA_LOCAL_PATH)

CUDA_INCLUDES="-I$CUDA_LOCAL_PATH/include"
CUDA_LDFLAGS="-L$CUDA_LOCAL_PATH/lib$SUFFIX"
CUDA_LIBS="-lcudart -static-libstdc++"

if test -n "$with_nvml" ; then
  NVML_LIBPATH=$with_nvml
  CUDA_LDFLAGS="$CUDA_LDFLAGS -ldl"
fi
AC_SUBST(NVML_LIBPATH)

AC_SUBST(CUDA_CFLAGS)
AC_SUBST(CUDA_INCLUDES)
AC_SUBST(CUDA_LIBS)
AC_SUBST(CUDA_LDFLAGS)
AC_SUBST(NVCC)

AC_SUBST(OPENMP_CFLAGS)

dnl ── KawPow / ProgPoW: optional NVRTC runtime-compilation support ──────────
dnl
dnl  KawPow (Ravencoin ProgPoW) requires libnvrtc.so to JIT-compile the
dnl  period-specific inner-loop kernel at runtime.  The library ships as
dnl  part of the CUDA toolkit (/usr/local/cuda/lib64/libnvrtc.so).
dnl
dnl  Pass --with-nvrtc to enable; omit it to build without KawPow support
dnl  (the binary will still compile and all other algos remain functional).
dnl
dnl  Usage:
dnl    ./configure --with-nvrtc
dnl    ./configure --with-nvrtc=/opt/cuda

AC_ARG_WITH([nvrtc],
  [AS_HELP_STRING([--with-nvrtc[=PATH]],
    [enable KawPow/ProgPoW support via libnvrtc (default: disabled)])],
  [have_nvrtc=yes
   if test "x$withval" != "xyes" ; then
     NVRTC_PATH="$withval"
     CUDA_LDFLAGS="$CUDA_LDFLAGS -L$withval/lib64"
     CUDA_INCLUDES="$CUDA_INCLUDES -I$withval/include"
   fi],
  [have_nvrtc=no])

AM_CONDITIONAL([HAVE_NVRTC], [test "x$have_nvrtc" = "xyes"])

if test "x$have_nvrtc" = "xyes" ; then
  AC_DEFINE([HAVE_NVRTC], [1],
    [Define to 1 if libnvrtc is available for KawPow/ProgPoW JIT compilation])
  AC_MSG_NOTICE([KawPow/ProgPoW enabled (--with-nvrtc)])
else
  AC_MSG_NOTICE([KawPow/ProgPoW disabled (pass --with-nvrtc to enable)])
fi

AC_OUTPUT
