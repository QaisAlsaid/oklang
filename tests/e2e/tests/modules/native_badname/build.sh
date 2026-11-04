#!/bin/sh
set -e
cd "$(dirname "$0")"
gcc -shared -fPIC -I"${OK_ROOT}/include" -o badname.so badname.c
