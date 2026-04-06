#!/bin/sh
# autogen.sh — regenerate the autotools build system from source.
#
# Run this script once after cloning, and again after any change to
# configure.ac or Makefile.am.  It must be run before ./configure.
#
# CUDA 12 constraints
# -------------------
# CUDA 12.0 removed all pre-sm_50 (Fermi/Kepler) compute targets.
# The minimum supported GPU architecture is Maxwell (sm_50).
# The generated Makefile uses SCRYPT_NVCC_FLAGS with
# -gencode=arch=compute_50,code="sm_50,compute_50" for the scrypt
# sub-kernels, superseding the original sm_35 flags.
#
# IMPORTANT: never distribute or commit a hand-edited Makefile.in.
# Always regenerate it via this script so that Makefile.am and
# Makefile.in remain in sync.  Out-of-sync generated files are the
# single most common cause of "works on my machine" build failures.

set -euo pipefail

command -v autoreconf >/dev/null 2>&1 || {
    printf 'error: autoreconf not found -- install autoconf\n' >&2
    exit 1
}

autoreconf --force --install --verbose
