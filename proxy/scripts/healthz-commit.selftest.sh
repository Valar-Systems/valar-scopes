#!/usr/bin/env bash
# Selftest for healthz-commit.sh (docs/healthz-commit-read.md). Synthetic /healthz bodies with fake shas: 1111111 for
# the proxy, forty 2s for the builder. Every row compares a WHOLE output, never a fragment.
#
# Then two plants, each a temporary copy of the helper with its edit asserted, whose WHOLE red list must equal the
# registered set:
#   P-OLD    the pattern every reader used before:        HC1 HC5 CD1 SM1 SM2 SM3 SM4 R12
#   P-FIRST  the half-fix, that pattern's first match:     HC5 SM4
# W1-W3 check the wiring: each reader calls the helper and holds no "commit" grep of its own. A plant edits only the
# helper, so they stay green under both plants (added at code time; docs/healthz-commit-read.md section 8).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REAL="$HERE/healthz-commit.sh"
CONFIRM="$HERE/confirm-deploy.sh"
SMOKE="$HERE/smoke-prod.sh"
R12_FILE="$(cd "$HERE/../../tools/wx" && pwd)/r12_prod.sh"
B=2222222222222222222222222222222222222222
OLD='"commit":"[^"]*"'

bm() { printf '"basemap":{"rev":1,"store":"s","commit":%s,"keyed":true}' "$1"; }
body_hc1="{\"ok\":true,\"commit\":\"1111111\",\"upstreams\":[],\"wx\":\"not_configured\",$(bm "\"$B\"")}"
body_hc2="{\"ok\":true,\"commit\":\"1111111\",\"upstreams\":[],\"wx\":\"not_configured\",$(bm null)}"
body_hc3='{"ok":true,"commit":"1111111","upstreams":[],"wx":"not_configured","basemap":"not_configured"}'
body_null='{"ok":true,"commit":null,"upstreams":[]}'
body_nokey='{"ok":true,"upstreams":[]}'
body_hc5="{\"ok\":true,\"upstreams\":[],$(bm "\"$B\"")}"
body_sm1="{\"ok\":true,\"commit\":\"dev\",\"upstreams\":[],$(bm "\"$B\"")}"
body_sm3="{\"ok\":true,\"commit\":\"1111111-dirty\",\"upstreams\":[],$(bm "\"$B\"")}"
body_sm5='{"ok":true,"upstreams":[],"basemap":"not_configured"}'

# rows <helper>: one line per row, "<id> ok" or "<id> RED got=<...> want=<...>". Run in a subshell so a plant's
# definitions never leak into the next run.
rows() (
  . "$CONFIRM"           # classify, report (it stops before any fetch when sourced); it sources the real helper
  . "$1"                 # then the helper under test, which replaces healthz_commit and stamp_verdict
  hc()  { printf '%s' "$1" | healthz_commit; }
  old() { printf '%s' "$1" | grep -oE "$OLD" | cut -d'"' -f4; }
  row() { if [ "$2" = "$3" ]; then echo "$1 ok"; else echo "$1 RED got=[$2] want=[$3]"; fi; }

  row HC1 "$(hc "$body_hc1")" "1111111"
  row HC2 "$(hc "$body_hc2")|$(old "$body_hc2")" "1111111|1111111"
  row HC3 "$(hc "$body_hc3")|$(old "$body_hc3")" "1111111|1111111"
  row HC4 "$(hc "$body_null")|$(hc "$body_nokey")|$(hc 'not json')|$(hc '')" "|||"
  row HC5 "$(hc "$body_hc5")" ""

  cd_row() { local tok rc; tok="$(classify 1 "$(hc "$2")" 1111111)"; report "$tok" x 1111111 h https 5 >/dev/null; rc=$?
             row "$1" "$tok/$rc" "CONFIRMED/0"; }
  cd_row CD1 "$body_hc1"
  cd_row CD2 "$body_hc2"

  row SM1 "$(stamp_verdict "$(hc "$body_sm1")")" "FAIL-absent"
  row SM2 "$(stamp_verdict "$(hc "$body_hc1")")/$(hc "$body_hc1")" "PASS/1111111"
  row SM3 "$(stamp_verdict "$(hc "$body_sm3")")" "FAIL-dirty"
  row SM4 "$(stamp_verdict "$(hc "$body_hc5")")" "FAIL-absent"
  row SM5 "$(stamp_verdict "$(hc "$body_sm5")")" "FAIL-absent"

  # R12: r12_prod.sh's own print line, run as written with hz set to the body (no curl, no identity).
  local n line hz
  n="$(grep -c "^printf 'healthz commit \[%s\]" "$R12_FILE")"
  line="$(grep "^printf 'healthz commit \[%s\]" "$R12_FILE")"
  hz="$body_hc1"
  row R12 "$n|$(eval "$line")" "1|healthz commit [1111111]"

  # W1-W3: each reader calls the helper exactly as wired, and no reader keeps a "commit" grep of its own.
  cnt() { grep -cF -- "$1" "$2"; }
  row W1 "$(cnt 'LIVE="$(healthz_commit < "$HZ")"' "$CONFIRM")|$(cnt '"commit":"' "$CONFIRM")" "1|0"
  row W2 "$(cnt 'COMMIT="$(printf '"'"'%s'"'"' "${LAST_BODY:-}" | healthz_commit)"' "$SMOKE")|$(cnt 'case "$(stamp_verdict "$COMMIT")" in' "$SMOKE")|$(cnt '"commit":"' "$SMOKE")" "1|1|0"
  row W3 "$(cnt '| healthz_commit)"' "$R12_FILE")|$(cnt '"commit":"' "$R12_FILE")" "1|0"
)

reds() { printf '%s\n' "$1" | awk '$2 == "RED" { printf "%s%s", s, $1; s = " " }'; }
fail=0

echo "== the helper as written =="
OUT="$(rows "$REAL")"; printf '%s\n' "$OUT" | sed 's/^/  /'
R="$(reds "$OUT")"
if [ -z "$R" ]; then echo "  clean: every row passes"; else echo "  FAIL: rows red on the real helper: $R"; fail=1; fi

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
plant() { # plant <name> <the planted function line> <registered red set>
  local copy="$T/$1.sh" got
  cp "$REAL" "$copy"; printf '\n%s\n' "$2" >> "$copy"
  if cmp -s "$REAL" "$copy" || [ "$(grep -cxF -- "$2" "$copy")" != 1 ] || [ "$(grep -cxF -- "$2" "$REAL")" != 0 ]; then
    echo "== $1: the plant did not apply -- the instrument, not a reading"; fail=1; return
  fi
  got="$(reds "$(rows "$copy")")"
  if [ "$got" = "$3" ]; then echo "== $1: red set {$got}, as registered"
  else echo "== $1: red set {$got}, registered {$3} -- OFF"; fail=1; fi
}
plant P-OLD   "healthz_commit() { grep -oE '\"commit\":\"[^\"]*\"' | cut -d'\"' -f4; }"           "HC1 HC5 CD1 SM1 SM2 SM3 SM4 R12"
plant P-FIRST "healthz_commit() { grep -oE '\"commit\":\"[^\"]*\"' | head -1 | cut -d'\"' -f4; }" "HC5 SM4"

if [ "$fail" -eq 0 ]; then echo "SELFTEST PASSED"; else echo "SELFTEST FAILED"; fi
exit "$fail"
