#!/usr/bin/env bash
#
# mayhem/test.sh — runs the project's own Catch suite plus a known-answer probe (both built by
# mayhem/build.sh). exit 0 = pass. Both runners print a summary that this script parses; a runner
# that exits 0 without printing its summary (no-op build, exit(0) patch) counts as a failure.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

CATCH=mayhem/build-tests/Tests/Tests
KAT=mayhem/build-tests/kat_probe
for r in "$CATCH" "$KAT"; do
  [ -x "$r" ] || { echo "missing $r: mayhem/build.sh did not build it" >&2; emit_ctrf "recastnavigation" 0 1 0; exit 1; }
done

passed=0; failed=0; skipped=0

# Upstream's own Catch suite.
catch_out=$("$CATCH" 2>&1) ; catch_rc=$?
printf '%s\n' "$catch_out" | tail -8
if printf '%s\n' "$catch_out" | grep -q '^All tests passed ('; then
  n=$(printf '%s\n' "$catch_out" | sed -n 's/^All tests passed (.* in \([0-9]*\) test case.*/\1/p')
  passed=$((passed + n))
elif line=$(printf '%s\n' "$catch_out" | grep '^test cases:'); then
  p=$(printf '%s\n' "$line" | sed -n 's/.*| *\([0-9]*\) passed.*/\1/p')
  f=$(printf '%s\n' "$line" | sed -n 's/.*| *\([0-9]*\) failed.*/\1/p')
  passed=$((passed + ${p:-0})); failed=$((failed + ${f:-0}))
  [ "${f:-0}" -gt 0 ] || failed=$((failed + 1))
else
  echo "no Catch2 summary" >&2; failed=$((failed + 1))
fi
[ "$catch_rc" -eq 0 ] || [ "$failed" -gt 0 ] || failed=$((failed + 1))

# Known-answer probe: the OBJ loader, chunky mesh and raycast with fixed inputs.
kat_out=$("$KAT" 2>&1) ; kat_rc=$?
printf '%s\n' "$kat_out" | grep -v ' ok$'
kp=$(printf '%s\n' "$kat_out" | grep -c '^KAT [^ ]* ok$' || true)
kf=$(printf '%s\n' "$kat_out" | grep -c '^KAT [^ ]* FAIL$' || true)
if ! printf '%s\n' "$kat_out" | grep -q '^KAT done failed=0$' || [ "$kp" -lt 21 ] || [ "$kat_rc" -ne 0 ]; then
  [ "$kf" -gt 0 ] || kf=1
fi
passed=$((passed + kp)); failed=$((failed + kf))

emit_ctrf "catch+kat" "$passed" "$failed" "$skipped"
