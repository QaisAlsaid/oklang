#!/bin/sh
# the clean fixture: no artifact, so the refusal path is the one under test
set -e
cd "$(dirname "$0")"
rm -f adder.so
rm -rf .okbuild
