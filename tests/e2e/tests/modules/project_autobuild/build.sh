#!/bin/sh
# the clean fixture: every run starts like a fresh clone (no artifact, no
# stamps), so the auto-build lane fires during the test itself
set -e
cd "$(dirname "$0")"
rm -f adder.so
rm -rf .okbuild
