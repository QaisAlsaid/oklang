#!/usr/bin/env bash
# tests/cli/run.sh - the CLI lane (P1). the e2e runner drives .ok files
# through the interpreter; this one drives the BINARY's own surface: the
# version spellings, the scaffold, the refusals. the e2e culture applies:
# every CLI feature lands with its checks here, in the same commit.
#
# usage: OK_BIN=<path/to/ok> bash tests/cli/run.sh   (or $1 = the binary)
set -u
OK_BIN="${1:-${OK_BIN:-/home/z/my-project/oklang/build-gcc/ok}}"
# absolutize before any cd happens below
case "$OK_BIN" in
/*) ;;
*) OK_BIN="$PWD/${OK_BIN#./}" ;;
esac
pass=0; fail=0

ok() { pass=$((pass + 1)); }
no() { echo "FAIL: $1"; echo "  got: $2" | head -2; fail=$((fail + 1)); }

# the stamp's shape: one line, version (with the wave's lane while
# mid-wave), the abi pair. anchored both ends - nothing else may print.
STAMP='^ok [0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)? \(abi [0-9]+\.[0-9]+\)$'

out=$("$OK_BIN" --version 2>&1)
echo "$out" | grep -qE "$STAMP" && ok || no "--version prints the stamp" "$out"
out=$("$OK_BIN" -v 2>&1)
echo "$out" | grep -qE "$STAMP" && ok || no "-v prints the stamp" "$out"
out=$("$OK_BIN" version 2>&1)
echo "$out" | grep -qE "$STAMP" && ok || no "the version verb prints the stamp" "$out"

# -v wins over other args (it answers and leaves)
out=$("$OK_BIN" run -v whatever.ok 2>&1)
echo "$out" | grep -qE '^ok [0-9]+\.[0-9]+\.[0-9]+' && ok || no "-v answers and leaves" "$out"

# ok create: the scaffold
tmp=$(mktemp -d) || exit 70
cd "$tmp" || exit 70
"$OK_BIN" create hello >/dev/null 2>&1
if [ -f hello/project.ok ] && [ -f hello/src/main.ok ] && [ -f hello/tests/.gitkeep ] && [ -f hello/.gitignore ] &&
  grep -q '.okbuild' hello/.gitignore; then
  ok
else
  no "ok create scaffolds the four pieces" "missing files under $tmp/hello"
fi

out=$("$OK_BIN" hello/src/main.ok 2>&1)
echo "$out" | grep -q 'hello from hello' && ok || no "the scaffolded project runs" "$out"

"$OK_BIN" create hello >/dev/null 2>&1
if [ $? -ne 0 ]; then ok; else no "ok create refuses an existing dir" "exit 0"; fi

"$OK_BIN" create a/b >/dev/null 2>&1
if [ $? -eq 64 ]; then ok; else no "ok create refuses path separators (usage)" "exit $?"; fi

# the manifest is a real project: the where lane reads it through the same
# discovery a run uses
"$OK_BIN" where std/io hello/src/main.ok >/dev/null 2>&1
if [ $? -eq 0 ]; then ok; else no "the scaffold's discovery finds std" "where failed"; fi

# unknown flags still refuse with usage
"$OK_BIN" --bogus </dev/null >/dev/null 2>&1
if [ $? -eq 64 ]; then ok; else no "unknown flag exits 64" "exit $?"; fi

cd / || exit 70
rm -rf "$tmp"
echo "cli: PASS $pass  FAIL $fail"
[ $fail -eq 0 ]
