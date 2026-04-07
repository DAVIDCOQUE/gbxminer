#!/usr/bin/env bash

set -euo pipefail

make distclean 2>/dev/null || true

rm -rf .deps
rm -f Makefile.in configure config.h.in config.log config.status stamp-h1
rm -f configure~ depcomp
rm -rf autom4te.cache
rm -f aclocal.m4

./autogen.sh

./configure.sh

make -j$(nproc)
