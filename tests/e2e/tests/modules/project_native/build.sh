#!/bin/sh
# the fixture's declared build runs through the CLI's own build command:
# ok build discovers the project here, runs the target, stamps it. OK_BIN
# is the runner's gift (the fixture-build contract).
set -e
cd "$(dirname "$0")"
: "${OK_BIN:?the e2e runner sets OK_BIN; export it for a hand run}"
"$OK_BIN" build --yes
