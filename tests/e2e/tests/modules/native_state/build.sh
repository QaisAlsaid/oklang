#!/bin/sh
# the fixture's declared build: a raw gcc command (M12: whatever toolchain).
set -e
cd "$(dirname "$0")"
gcc -shared -fPIC -I"${OK_ROOT}/include" -o state.so state.c
