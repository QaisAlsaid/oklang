#!/bin/sh
set -e
cd "$(dirname "$0")"
gcc -shared -fPIC -I../../../../../include -o badabi.so badabi.c
