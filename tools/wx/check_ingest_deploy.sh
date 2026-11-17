#!/usr/bin/env bash
# ID1-ID13 and the plants IDs-a..IDs-e for tools/wx/ingest_deploy.sh (docs/wx-ingest-deploy.md), frozen before the code.
# Everything runs in a mirror tree in a temporary folder: byte copies of the script, scripts/session-claim.py, the wrangler
# helper (proxy/scripts/wrangler-run.mjs) and four tomls, and its own BLIPSCOPE_CLAIMS_DIR. The script calls wrangler
# through the helper (docs/wrangler-run.md), and the helper runs WRANGLER_RUN_BIN: here a FAKE wrangler that records each
# call as "wrangler <args>" and never reaches Cloudflare. An `npx` first on PATH is a TRAP: an unmigrated `npx wrangler`
# lands there, is counted, and exits 97. No row touches a real claim, a real Worker or a repo file.
#
#   bash tools/wx/check_ingest_deploy.sh                 the 13 rows, each against its prediction
#   bash tools/wx/check_ingest_deploy.sh --sabotage      each plant, in the mirror's copy only, must turn exactly its rows red
#   bash tools/wx/check_ingest_deploy.sh --guard-selftest  the guard refuses an unset or wrong WRANGLER_RUN_BIN (G1-G4)
#
# Exit 0 all as predicted; 1 off; 2 rig (a copy that is not byte-identical, a claim that would not take, a plant whose
# anchor is not exactly one whole line, or an unplanted run that is not all as predicted).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
M="$T/m"
mkdir -p "$M/tools/wx" "$M/scripts" "$M/proxy/scripts" "$T/bin" "$T/claims"
for f in tools/wx/ingest_deploy.sh scripts/session-claim.py proxy/scripts/wrangler-run.mjs proxy/wrangler.toml \
         proxy/wx-ingest.wrangler.toml proxy/wx-ingest-opera.wrangler.toml proxy/wx-ingest-dev.wrangler.toml; do
  cp "$REPO/$f" "$M/$f" && cmp -s "$REPO/$f" "$M/$f" || { echo "RIG: $f did not copy byte-identical"; exit 2; }
done
mkdir -p "$M/docs"; echo base > "$M/docs/x.md"
SCRIPT="$M/tools/wx/ingest_deploy.sh"
cp "$SCRIPT" "$T/script.orig"

PY=""
for c in python3 python; do command -v "$c" >/dev/null 2>&1 && "$c" -c 'import tomllib' >/dev/null 2>&1 && { PY="$c"; break; }; done
[ -n "$PY" ] || { echo "RIG: no Python with tomllib"; exit 2; }
export BLIPSCOPE_CLAIMS_DIR="$(cygpath -m "$T/claims" 2>/dev/null || echo "$T/claims")"
wpath() { cygpath -m "$1" 2>/dev/null || echo "$1"; }   # node on Windows needs a Windows path
export FAKE_CALLS="$(wpath "$T/calls")"
export NPX_TRAP="$T/npx-trap"

# THE FAKE wrangler, run by the helper as WRANGLER_RUN_BIN: it records "wrangler <args>", whole, one line per call.
cat > "$T/bin/fake-wrangler.mjs" <<'EOF'
import { appendFileSync } from "node:fs";
const a = process.argv.slice(2);
appendFileSync(process.env.FAKE_CALLS, ["wrangler", ...a].join(" ") + "\n");
const s = a.join(" ");
if (s === "__rig_probe__") { process.stdout.write("fake probe\n"); process.exit(0); }
if (s.startsWith("deploy ")) { process.stdout.write("fake deploy\n"); process.exit(Number(process.env.FAKE_DEPLOY_RC || 0)); }
if (s.startsWith("deployments status ")) { process.stdout.write('{"versions":[{"version_id":"fake0000-0000-0000-0000-000000000000","percentage":100}]}\n'); process.exit(0); }
process.stderr.write("fake wrangler: unexpected call\n"); process.exit(99);
EOF
FAKE_BIN="$(wpath "$T/bin/fake-wrangler.mjs")"
# THE TRAP: an unmigrated `npx wrangler` lands here, is counted, and reaches nothing real.
cat > "$T/bin/npx" <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$NPX_TRAP"
exit 97
EOF
chmod +x "$T/bin/npx"
: > "$T/npx-trap"
# A row holds a real-looking claim in its own folder, so the script passes its check and calls wrangler. And if the
# helper ever ran the real wrangler, it would meet a token that is not one, and fail to authenticate rather than deploy
# (a per-process variable, never a user-level one).
export CLOUDFLARE_API_TOKEN=not-a-token-check-ingest-deploy

# THE MIRROR'S GIT (docs/deploy-sha-pin.md 2b): C0 is the base and HEAD; C1 and C2 each add a file under proxy/, and CD
# touches only docs/x.md, on side refs in a bare origin. A row sets origin's main to its tip; the script's own
# `git fetch` picks it up. RIG (ruled 2026-11-17): a plant edits the mirror's TRACKED ingest_deploy.sh, which would read
# as dirt and trip the dirty-tree refusal before the pin; assume-unchanged hides ONLY that file, and IS5 proves the
# refusal still fires on other dirt.
export GIT_AUTHOR_NAME=harness GIT_AUTHOR_EMAIL=harness@invalid GIT_COMMITTER_NAME=harness GIT_COMMITTER_EMAIL=harness@invalid
g() { git -c core.autocrlf=false -c core.eol=lf -c init.defaultBranch=main "$@"; }
g init -q --bare "$T/origin.git" || { echo "RIG: git init --bare"; exit 2; }
( cd "$M" && g init -q && g add -A && g commit -q -m C0 && g remote add origin "$T/origin.git" && g push -q origin main ) \
  || { echo "RIG: the mirror's base commit"; exit 2; }
C0="$(g -C "$M" rev-parse HEAD)"
g -C "$M" update-index --assume-unchanged tools/wx/ingest_deploy.sh || { echo "RIG: assume-unchanged"; exit 2; }
g clone -q "$T/origin.git" "$T/side" || { echo "RIG: git clone"; exit 2; }
( cd "$T/side" && echo c1 > proxy/c1.txt && g add proxy/c1.txt && g commit -q -m C1 && g push -q origin HEAD:refs/heads/s-c1 \
  && echo c2 > proxy/c2.txt && g add proxy/c2.txt && g commit -q -m C2 && g push -q origin HEAD:refs/heads/s-c2 \
  && g checkout -q "$C0" && echo docs >> docs/x.md && g commit -q -am CD && g push -q origin HEAD:refs/heads/s-cd ) \
  || { echo "RIG: the side commits"; exit 2; }
C1="$(g -C "$T/origin.git" rev-parse s-c1)"; C2="$(g -C "$T/origin.git" rev-parse s-c2)"; CD="$(g -C "$T/origin.git" rev-parse s-cd)"
tipsha() { case "$1" in C0) echo "$C0";; C1) echo "$C1";; C2) echo "$C2";; CD) echo "$CD";; esac; }
export EXPECT_SHA="$C0"   # ID1-ID13 run pinned to HEAD; their predictions are unchanged

# THE GUARD, before any row: WRANGLER_RUN_BIN must be the fake, a probe through the MIRROR's helper must land in the
# fake (one whole recorded line), and `npx` must resolve to the trap. Exit 2 (rig) otherwise.
guard() {
  [ "${WRANGLER_RUN_BIN:-}" = "$FAKE_BIN" ] || { echo "RIG: WRANGLER_RUN_BIN is not the fake"; return 2; }
  : > "$T/calls"
  (cd "$M/proxy" && node scripts/wrangler-run.mjs __rig_probe__) > /dev/null 2>&1 || { echo "RIG: the helper's probe failed"; return 2; }
  [ "$(cat "$T/calls")" = "wrangler __rig_probe__" ] || { echo "RIG: the probe did not land in the fake"; return 2; }
  [ "$(PATH="$T/bin:$PATH" command -v npx)" = "$T/bin/npx" ] || { echo "RIG: npx does not resolve to the trap"; return 2; }
  return 0
}

if [ "${1:-}" = "--guard-selftest" ]; then
  r1="$(env -u WRANGLER_RUN_BIN bash -c "$(declare -f guard); T='$T'; M='$M'; FAKE_BIN='$FAKE_BIN'; guard" 2>&1)"; c1=$?
  printf 'export const x = 1;\n' > "$T/bin/not-the-fake.mjs"
  r2="$(WRANGLER_RUN_BIN="$(wpath "$T/bin/not-the-fake.mjs")" bash -c "$(declare -f guard); T='$T'; M='$M'; FAKE_BIN='$FAKE_BIN'; guard" 2>&1)"; c2=$?
  r3="$(WRANGLER_RUN_BIN="$FAKE_BIN" bash -c "$(declare -f guard); T='$T'; M='$M'; FAKE_BIN='$FAKE_BIN'; guard" 2>&1)"; c3=$?
  echo "G1 WRANGLER_RUN_BIN unset: exit $c1 ($r1)"
  echo "G2 WRANGLER_RUN_BIN not the fake: exit $c2 ($r2)"
  echo "G3 WRANGLER_RUN_BIN the fake: exit $c3"
  # G4: the NAME check passes (the variable equals the guard's fake path) but the file there records nothing, so only
  # the probe can refuse it. G1 and G2 stop at the name; G4 shows the probe is load-bearing.
  X="$(wpath "$T/bin/same-name-not-the-fake.mjs")"; cp "$T/bin/not-the-fake.mjs" "$T/bin/same-name-not-the-fake.mjs"
  r4="$(WRANGLER_RUN_BIN="$X" bash -c "$(declare -f guard); T='$T'; M='$M'; FAKE_BIN='$X'; guard" 2>&1)"; c4=$?
  echo "G4 the name right, the file not the fake: exit $c4 ($r4)"
  [ $c1 -eq 2 ] && [ $c2 -eq 2 ] && [ $c3 -eq 0 ] && [ $c4 -eq 2 ] && { echo "GUARD: AS PREDICTED (G1 refused, G2 refused, G3 passed, G4 refused by the probe)"; exit 0; }
  echo "GUARD: OFF"; exit 1
fi
export WRANGLER_RUN_BIN="$FAKE_BIN"
guard || exit 2

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

# THE PIN ROWS (docs/deploy-sha-pin.md 2b), each against wx-ingest-opera.wrangler.toml opuk under a held claim, through
# row() above. name | origin's tip (C0dirty: C0 plus one line appended to the tracked proxy/wrangler.toml) | EXPECT_SHA
# (unset|abc123|C1|HEAD) | ALLOW_BEHIND | ALLOW_BEHIND_SHAS (-|C1|C1,C0|C1short) | token | "<exit> <deploy 0|1> <token 0|1>"
IROWS=(
  "IS1|C0|unset|-|-|EXPECT_SHA is not set|1 0 1"
  "IS2|C0|abc123|-|-|EXPECT_SHA must be a full 40-hex sha|1 0 1"
  "IS3|C0|C1|-|-|HEAD is not EXPECT_SHA|1 0 1"
  "IS4|C0|HEAD|-|-|pinned: HEAD is EXPECT_SHA|0 1 1"
  "IS5|C0dirty|HEAD|-|-|uncommitted changes|1 0 1"
  "IB1|C1|HEAD|1|-|ALLOW_BEHIND=1 is retired|1 0 1"
  "IB2|C1|HEAD|-|C1|named and behind: <C1:12>|0 1 1"
  "IB3|C2|HEAD|-|C1|behind and not named: <C2:12>|1 0 1"
  "IB4|C1|HEAD|-|C1,C0|named but not behind: <C0:12>|1 0 1"
  "IB5|C1|HEAD|-|C1short|ALLOW_BEHIND_SHAS must list full 40-hex shas|1 0 1"
  "IB6|CD|HEAD|-|-|pinned: HEAD is EXPECT_SHA|0 1 1"
)
srow() {
  local tipn=$1 expect=$2 ab=$3 abs=$4 token=$5 dirt=0
  [ "$tipn" = C0dirty ] && { dirt=1; tipn=C0; }
  g -C "$T/origin.git" update-ref refs/heads/main "$(tipsha "$tipn")" || { echo "RIG: update-ref"; return 2; }
  [ "$dirt" = 1 ] && echo '# dirt' >> "$M/proxy/wrangler.toml"
  local ev="" absv=""
  case "$expect" in unset) ;; HEAD) ev="$C0";; C1) ev="$C1";; *) ev="$expect";; esac
  case "$abs" in -) ;; C1) absv="$C1";; C1,C0) absv="$C1,$C0";; C1short) absv="${C1:0:12}";; esac
  local got
  got="$( if [ "$expect" = unset ]; then unset EXPECT_SHA; else export EXPECT_SHA="$ev"; fi
          if [ "$ab" = 1 ]; then export ALLOW_BEHIND=1; else unset ALLOW_BEHIND; fi
          if [ -n "$absv" ]; then export ALLOW_BEHIND_SHAS="$absv"; else unset ALLOW_BEHIND_SHAS; fi
          row alpha wx-ingest-opuk:production 0 alpha 0 "$OPERA" opuk )"
  if [ "$dirt" = 1 ]; then g -C "$M" checkout -q -- proxy/wrangler.toml || { echo "RIG: restore"; return 2; }; fi
  [ -z "$(g -C "$M" status --porcelain -- proxy/ tools/wx/)" ] || { echo "RIG: the mirror is not clean after a row"; return 2; }
  case "$got" in RIG:*) echo "$got"; return 2;; esac
  local rc="${got%% *}" calls="${got#* }" up=0 tk=0
  case "$calls" in *"wrangler deploy -c"*) up=1;; esac
  grep -qF -- "$(echo "$token" | sed "s/<C1:12>/${C1:0:12}/; s/<C2:12>/${C2:0:12}/; s/<C0:12>/${C0:0:12}/")" "$T/out" && tk=1
  echo "$rc $up $tk"
}

RED=""
run_all() {
  RED=""
  local r name holder covers expired runas drc args want got
  g -C "$T/origin.git" update-ref refs/heads/main "$C0" || { echo "RIG: update-ref"; exit 2; }
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
  for r in "${IROWS[@]}"; do
    IFS='|' read -r name tipn exp ab abs token want <<< "$r"
    got="$(srow "$tipn" "$exp" "$ab" "$abs" "$token")" || { echo "$got"; exit 2; }
    if [ "$got" = "$want" ]; then
      [ "$1" = loud ] && echo "$name: [$got] (exit deploy token): as predicted"
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
  : > "$T/npx-trap"
  run_all loud
  echo "npx trap hits: $(wc -l < "$T/npx-trap" | tr -d ' '); fake token in force: $([ "$CLOUDFLARE_API_TOKEN" = not-a-token-check-ingest-deploy ] && echo yes || echo no)"
  if [ -z "$RED" ] && [ ! -s "$T/npx-trap" ]; then echo "ALL AS PREDICTED (24 rows)"; exit 0; fi
  echo "OFF: ${RED:-none}; npx trap hits must be 0"; exit 1
fi

run_all quiet
[ -z "$RED" ] || { echo "RIG: the unplanted run is not all as predicted (off: $RED)"; exit 2; }
echo "CONTROL (unplanted): 24 of 24 as predicted"
# name | the whole line | its replacement | the registered red set
PLANTS=(
  'IDs-a|if ! "$CLAIM_PY" "$CLAIM_TOOL" check deploy --covers "$CLAIM_TARGET"; then|if false; then|ID2 ID3 ID4 ID5 ID6'
  'IDs-b|CLAIM_TARGET="${NAME#blipscope-}:production"|CLAIM_TARGET="wx-ingest-${ENV}:production"|ID9 ID11 ID13'
  'IDs-c|if [ -n "$REQUIRE_ENV" ] && [ -z "$ENV" ]; then|if false; then|ID7'
  'IDs-d|  wx-ingest.wrangler.toml|wx-ingest-opera.wrangler.toml|wx-ingest-dev.wrangler.toml) ;;|  *) ;;|ID8'
  'IDs-e|exit $rc|exit 0|ID12'
  'IDs-f|node scripts/wrangler-run.mjs deploy -c "$TOML" ${EARGS[@]+"${EARGS[@]}"} > "$T/dep.out" 2> "$T/dep.err" < /dev/null   # docs/wrangler-run.md|npx wrangler deploy -c "$TOML" ${EARGS[@]+"${EARGS[@]}"} > "$T/dep.out" 2> "$T/dep.err" < /dev/null|ID1 ID9 ID11 ID12 ID13 IS4 IB2 IB6'
  'S-IS1|  fatal "EXPECT_SHA is not set: the deploy plan names the commit it ships (docs/deploy-sha-pin.md)."; exit 1|  :|IS1'
  'S-IS2|  fatal "EXPECT_SHA must be a full 40-hex sha (got: ${EXPECT_SHA:-})"; exit 1|  :|IS2'
  'S-IS3|  fatal "HEAD is not EXPECT_SHA: HEAD $HEAD_SHA, the plan names $EXPECT_SHA."; exit 1|  :|IS3'
  'S-IS5|if [ -n "$DIRTY" ]; then|if false; then|IS5'
  'S-IB1|  fatal "ALLOW_BEHIND=1 is retired: name the commits behind with ALLOW_BEHIND_SHAS=<full sha>,..."; exit 1|  :|IB1'
  'S-IB3|    fatal "behind and not named: ${c:0:12} (a merge after the plan; name it in ALLOW_BEHIND_SHAS, or re-plan)"; exit 1|    :|IB3'
  'S-IB4|    fatal "named but not behind: ${c:0:12} (the plan is stale)"; exit 1|    :|IB4'
  'S-IB5|      fatal "ALLOW_BEHIND_SHAS must list full 40-hex shas (got: $c)"; exit 1|      :|IB5'
)
off=0
for p in "${PLANTS[@]}"; do
  name="${p%%|*}"; reg="${p##*|}"; rest="${p#*|}"; rest="${rest%|*}"
  repl="${rest##*|}"; anchor="${rest%|*}"
  cp "$T/script.orig" "$SCRIPT"
  plant "$name" "$anchor" "$repl"
  : > "$T/npx-trap"
  run_all quiet
  [ "$name" = IDs-f ] && echo "IDs-f: npx trap hits $(wc -l < "$T/npx-trap" | tr -d ' ') (the unmigrated call reached the trap, nothing real)"
  cp "$T/script.orig" "$SCRIPT"
  cmp -s "$SCRIPT" "$T/script.orig" || { echo "RIG: the copy was not restored after $name"; exit 2; }
  if [ "$RED" = "$reg" ]; then echo "$name: red {$RED}: as registered"
  else echo "$name: NOT AS REGISTERED -- red {$RED}, registered {$reg}"; off=$((off + 1)); fi
done
[ $off -eq 0 ] && { echo "ALL AS REGISTERED (14 plants)"; exit 0; }
echo "$off NOT AS REGISTERED"; exit 1
