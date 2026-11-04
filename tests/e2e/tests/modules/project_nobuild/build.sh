#!/bin/sh
# the clean fixture: no artifact, so --no-build's refusal is the one tested
set -e
cd "$(dirname "$0")"
rm -f adder.so
rm -rf .okbuild
