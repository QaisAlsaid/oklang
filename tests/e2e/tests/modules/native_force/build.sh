#!/bin/sh
set -e
cd "$(dirname "$0")"
gcc -shared -fPIC -I../../../../../include -o forced.so forced.c
