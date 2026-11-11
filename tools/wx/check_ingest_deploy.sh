#!/usr/bin/env bash
# ID1-ID13 and the plants IDs-a..IDs-e for tools/wx/ingest_deploy.sh (docs/wx-ingest-deploy.md), frozen before the code.
# Everything runs in a mirror tree in a temporary folder: byte copies of the script, scripts/session-claim.py and four
# tomls, a fake npx first on PATH (it records each call and never reaches Cloudflare), and its own BLIPSCOPE_CLAIMS_DIR.
# No row touches a real claim, a real Worker or a repo file.
#
#   bash tools/wx/check_ingest_deploy.sh              the 13 rows, each against its prediction
#   bash tools/wx/check_ingest_deploy.sh --sabotage   each plant, in the mirror's copy only, must turn exactly its rows red
#
# Exit 0 all as predicted; 1 off; 2 rig (a copy that is not byte-identical, a claim that would not take, a plant whose
# anchor is not exactly one whole line, or an unplanted run that is not all as predicted).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
M="$T/m"
mkdir -p "$M/tools/wx" "$M/scripts" "$M/proxy" "$T/bin" "$T/claims"
for f in tools/wx/ingest_deploy.sh scripts/session-claim.py proxy/wrangler.toml proxy/wx-ingest.wrangler.toml \
         proxy/wx-ingest-opera.wrangler.toml proxy/wx-ingest-dev.wrangler.toml; do
  cp "$REPO/$f" "$M/$f" && cmp -s "$REPO/$f" "$M/$f" || { echo "RIG: $f did not copy byte-identical"; exit 2; }
done
SCRIPT="$M/tools/wx/ingest_deploy.sh"
cp "$SCRIPT" "$T/script.orig"

PY=""
for c in python3 python; do command -v "$c" >/dev/null 2>&1 && "$c" -c 'import tomllib' >/dev/null 2>&1 && { PY="$c"; break; }; done
[ -n "$PY" ] || { echo "RIG: no Python with tomllib"; exit 2; }
export BLIPSCOPE_CLAIMS_DIR="$(cygpath -m "$T/claims" 2>/dev/null || echo "$T/claims")"
export FAKE_CALLS="$T/calls"

cat > "$T/bin/npx" <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$FAKE_CALLS"
case "$*" in
  "wrangler deploy "*) echo "fake deploy"; exit "${FAKE_DEPLOY_RC:-0}" ;;
  "wrangler deployments status "*) echo '{"versions":[{"version_id":"fake0000-0000-0000-0000-000000000000","percentage":100}]}' ;;
  *) echo "fake npx: unexpected call" >&2; exit 99 ;;
esac
EOF
chmod +x "$T/bin/npx"
# A row holds a real-looking claim in its own folder, so the script passes its check and runs npx. Prove npx is the fake
# before any row runs. And if it ever were not, the real wrangler would meet a token that is not one, and fail to
# authenticate rather than deploy (a per-process variable, never a user-level one).
[ "$(PATH="$T/bin:$PATH" command -v npx)" = "$T/bin/npx" ] || { echo "RIG: npx does not resolve to the fake"; exit 2; }
export CLOUDFLARE_API_TOKEN=not-a-token-check-ingest-deploy

OPERA=wx-ingest-opera.wrangler.toml
D1="wrangler deploy -c $OPERA -e opuk|wrangler deployments status -c $OPERA -e opuk --json"
D9="wrangler deploy -c wx-ingest.wrangler.toml|wrangler deployments status -c wx-ingest.wrangler.toml --json"
D11="wrangler deploy -c $OPERA -e opuk-inert|wrangler deployments status -c $OPERA -e opuk-inert --json"
D13="wrangler deploy -c wx-ingest-dev.wrangler.toml -e off|wrangler deployments status -c wx-ingest-dev.wrangler.toml -e off --json"

# One row: the claim (holder, covers, expired?), who runs it, the fake deploy's exit code, the script's arguments.
# Prints "<exit> <calls joined by |>".
row() {
  local holder=$1 covers=$2 expired=$3 runas=$4 drc=$5; shift 5
  rm -f "$T/claims/deploy.json"; : > "$T/calls"
  if [ "$holder" != - ]; then
    if [ "$expired" = 1 ]; then
      printf '{"session": "%s", "covers": ["%s"], "since": "%s", "until": "%s", "host": "h"}\n' "$holder" "$covers" \
        "$(date -u -d '-10 minutes' +%Y-%m-%dT%H:%M:%SZ)" "$(date -u -d '-1 minute' +%Y-%m-%dT%H:%M:%SZ)" > "$T/claims/deploy.json"
    else
      BLIPSCOPE_SESSION="$holder" "$PY" "$M/scripts/session-claim.py" take deploy --covers "$covers" --until +10m >/dev/null 2>&1 \
        || { echo "RIG: the claim for $covers would not take"; exit 2; }
    fi
  fi
  if [ "$runas" = - ]; then
    env -u BLIPSCOPE_SESSION PATH="$T/bin:$PATH" FAKE_DEPLOY_RC="$drc" bash "$SCRIPT" "$@" > "$T/out" 2>&1
  else
    BLIPSCOPE_SESSION="$runas" PATH="$T/bin:$PATH" FAKE_DEPLOY_RC="$drc" bash "$SCRIPT" "$@" > "$T/out" 2>&1
  fi
  local rc=$?
  echo "$rc $(paste -sd'|' "$T/calls")"
}

# name | the claim's holder | covers | expired | runs as | fake deploy rc | arguments | predicted "<exit> <calls>"
ROWS=(
  "ID1|alpha|wx-ingest-opuk:production|0|alpha|0|$OPERA opuk|0 $D1"
  "ID2|-|-|0|alpha|0|$OPERA opuk|1 "
  "ID3|beta|wx-ingest-opuk:production|0|alpha|0|$OPERA opuk|1 "
  "ID4|alpha|wx-ingest-opis:production|0|alpha|0|$OPERA opuk|1 "
  "ID5|alpha|wx-ingest-opuk:production|1|alpha|0|$OPERA opuk|1 "
  "ID6|alpha|wx-ingest-opuk:production|0|-|0|$OPERA opuk|1 "
  "ID7|alpha|wx-ingest-opera:production|0|alpha|0|$OPERA|2 "
  "ID8|alpha|proxy:production|0|alpha|0|wrangler.toml|2 "
  "ID9|alpha|wx-ingest:production|0|alpha|0|wx-ingest.wrangler.toml|0 $D9"
  "ID10|alpha|wx-ingest-opuk:production|0|alpha|0|$OPERA opux|2 "
  "ID11|alpha|wx-ingest-opuk:production|0|alpha|0|$OPERA opuk-inert|0 $D11"
  "ID12|alpha|wx-ingest-opuk:production|0|alpha|7|$OPERA opuk|7 $D1"
  "ID13|alpha|wx-ingest-dev:production|0|alpha|0|wx-ingest-dev.wrangler.toml off|0 $D13"
)

# Runs every row; prints one line each when $1 is "loud"; sets RED to the rows that differ from their prediction.
RED=""
run_all() {
  RED=""
  local r name holder covers expired runas drc args want got
  for r in "${ROWS[@]}"; do
    # The last field, the prediction, keeps its own "|"s: read gives the last name the rest of the line.
    IFS='|' read -r name holder covers expired runas drc args want <<< "$r"
    # shellcheck disable=SC2086
    got="$(row "$holder" "$covers" "$expired" "$runas" "$drc" $args)" || { echo "$got"; exit 2; }
    case "$got" in RIG:*) echo "$got"; exit 2 ;; esac
    if [ "$got" = "$want" ]; then
      [ "$1" = loud ] && echo "$name: exit ${got%% *}, calls [${got#* }]: as predicted"
    else
      RED="$RED $name"
      [ "$1" = loud ] && { echo "$name: OFF -- got [$got], predicted [$want]"; sed 's/^/    | /' "$T/out" | head -8; }
    fi
  done
  RED="${RED# }"
}

# A plant replaces exactly one whole line of the mirror's copy, and must change it.
plant() {
  "$PY" -I -c '
import sys
p, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p, encoding="utf-8", newline="").read()
lines = s.split("\n")
n = sum(1 for l in lines if l == a)
if n != 1:
    print("RIG: the anchor is %d whole lines, not 1: %s" % (n, a)); sys.exit(2)
s2 = "\n".join(b if l == a else l for l in lines)
if s2 == s:
    print("RIG: the plant did not change the file"); sys.exit(2)
open(p, "w", encoding="utf-8", newline="").write(s2)
' "$(cygpath -m "$SCRIPT" 2>/dev/null || echo "$SCRIPT")" "$2" "$3" || exit 2
  cmp -s "$SCRIPT" "$T/script.orig" && { echo "RIG: $1 left the copy unchanged"; exit 2; }
  return 0
}

if [ "${1:-}" != "--sabotage" ]; then
  run_all loud
  if [ -z "$RED" ]; then echo "ALL AS PREDICTED (13 rows)"; exit 0; fi
  echo "OFF: $RED"; exit 1
fi

run_all quiet
[ -z "$RED" ] || { echo "RIG: the unplanted run is not all as predicted (off: $RED)"; exit 2; }
echo "CONTROL (unplanted): 13 of 13 as predicted"
# name | the whole line | its replacement | the registered red set
PLANTS=(
  'IDs-a|if ! "$CLAIM_PY" "$CLAIM_TOOL" check deploy --covers "$CLAIM_TARGET"; then|if false; then|ID2 ID3 ID4 ID5 ID6'
  'IDs-b|CLAIM_TARGET="${NAME#blipscope-}:production"|CLAIM_TARGET="wx-ingest-${ENV}:production"|ID9 ID11 ID13'
  'IDs-c|if [ -n "$REQUIRE_ENV" ] && [ -z "$ENV" ]; then|if false; then|ID7'
  'IDs-d|  wx-ingest.wrangler.toml|wx-ingest-opera.wrangler.toml|wx-ingest-dev.wrangler.toml) ;;|  *) ;;|ID8'
  'IDs-e|exit $rc|exit 0|ID12'
)
off=0
for p in "${PLANTS[@]}"; do
  name="${p%%|*}"; reg="${p##*|}"; rest="${p#*|}"; rest="${rest%|*}"
  repl="${rest##*|}"; anchor="${rest%|*}"
  cp "$T/script.orig" "$SCRIPT"
  plant "$name" "$anchor" "$repl"
  run_all quiet
  cp "$T/script.orig" "$SCRIPT"
  cmp -s "$SCRIPT" "$T/script.orig" || { echo "RIG: the copy was not restored after $name"; exit 2; }
  if [ "$RED" = "$reg" ]; then echo "$name: red {$RED}: as registered"
  else echo "$name: NOT AS REGISTERED -- red {$RED}, registered {$reg}"; off=$((off + 1)); fi
done
[ $off -eq 0 ] && { echo "ALL AS REGISTERED (5 plants)"; exit 0; }
echo "$off NOT AS REGISTERED"; exit 1
