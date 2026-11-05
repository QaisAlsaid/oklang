#!/usr/bin/env bash
# regenerates the binary corpus seeds - the ones whose bytes are not valid
# utf-8 - from pure-ascii octal escapes.
#
# why this exists (the 4ad2cbd lesson): patches travel through media that
# may replace invalid utf-8 sequences with U+FFFD, and one landing did.
# the repo therefore never again TRUSTS the medium for these bytes: the
# escapes below are the source of truth, the committed copy is the cache,
# and tests/fuzz/corpus/checksums (fnv1a64, verified by okfuzz at corpus
# load, both the fuzz and the crosscheck lanes) trips loudly if the two
# ever disagree. run this after applying any patch that touches the
# corpus, or whenever okfuzz names a seed - it is idempotent.
#
# adding a binary seed: one printf line here (octal escapes, ascii
# source only), then regenerate the pins: tests/fuzz/crosscheck.sh
# (it runs this script first, then refreshes manifest + checksums).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"

# the invalid-utf-8 seed: valid text around a rejected ff fe pair and a
# truncated c3 lead - the lexer's not-acceptable-utf-8 door, as a seed.
printf 'print("caf\303\251 \377\376");\nlet x = "\303";\n' > "$HERE/invalid_utf8.bin"

echo "gen: $HERE/invalid_utf8.bin"
echo "gen: seeds rewritten from ascii escapes (idempotent; checksums pin them)."
