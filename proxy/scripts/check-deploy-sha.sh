#!/usr/bin/env bash
# ES1-ES4 and AB1-AB6 for proxy/scripts/deploy.sh's commit pin (docs/deploy-sha-pin.md), frozen before the code.
#
# Everything runs in a mirror tree in a temporary folder: a git repository with a bare `origin`, holding byte copies of
# deploy.sh and its machinery (deploy-inputs.mjs, wrangler-run.mjs, scripts/session-claim.py, the wrangler config and
# the package files). wrangler is a FAKE, run by the helper as WRANGLER_RUN_BIN: it records each call, writes a metafile
# naming src/index.ts for the dry-run, lists DEVICE_KEY_SECRET for `secret list`, and prints "fake deploy" for the
# upload. confirm-deploy.sh is a stub that records its arguments and reaches no network. Its own claims folder. No row
# touches a real claim, a real Worker, the network or a repo file.
#
#   bash proxy/scripts/check-deploy-sha.sh               the rows, each against its prediction
#   bash proxy/scripts/check-deploy-sha.sh --sabotage    each plant, in the mirror's copy only, must turn exactly its rows red
#
# Every row is graded on three things: the exit code, whether the upload call was recorded, and a message token only
# the pin prints. Exit 0 all as predicted; 1 off; 2 rig (a copy that is not byte-identical, a git step that fails, a
# claim that would not take, a plant whose anchor is not exactly one line, or an unplanted run that is not as predicted).
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
M="$T/m"
wpath() { cygpath -m "$1" 2>/dev/null || echo "$1"; }   # node on Windows needs a Windows path
export GIT_AUTHOR_NAME=harness GIT_AUTHOR_EMAIL=harness@invalid GIT_COMMITTER_NAME=harness GIT_COMMITTER_EMAIL=harness@invalid
g() { git -c core.autocrlf=false -c core.eol=lf -c init.defaultBranch=main "$@"; }

mkdir -p "$M/proxy/scripts" "$M/proxy/src" "$M/scripts" "$M/docs" "$T/bin" "$T/claims"
for f in proxy/scripts/deploy.sh proxy/scripts/deploy-inputs.mjs proxy/scripts/wrangler-run.mjs proxy/scripts/healthz-commit.sh \
         scripts/session-claim.py proxy/wrangler.toml proxy/package.json proxy/package-lock.json proxy/tsconfig.json proxy/src/index.ts; do
  cp "$REPO/$f" "$M/$f" && cmp -s "$REPO/$f" "$M/$f" || { echo "RIG: $f did not copy byte-identical"; exit 2; }
done
# The stub for the post-upload confirmation: it records, and reaches no network.
cat > "$M/proxy/scripts/confirm-deploy.sh" <<'EOF'
#!/usr/bin/env bash
echo "confirm $*" >> "$FAKE_CALLS_SH"
exit 0
EOF
chmod +x "$M/proxy/scripts/confirm-deploy.sh"
printf 'base\n' > "$M/docs/x.md"
SCRIPT="$M/proxy/scripts/deploy.sh"
cp "$SCRIPT" "$T/script.orig"

PY=""
for c in python3 python; do command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys' >/dev/null 2>&1 && { PY="$c"; break; }; done
[ -n "$PY" ] || { echo "RIG: no working Python"; exit 2; }
export BLIPSCOPE_CLAIMS_DIR="$(wpath "$T/claims")"
export FAKE_CALLS="$(wpath "$T/calls")"
export FAKE_CALLS_SH="$T/calls"
export CLOUDFLARE_API_TOKEN=not-a-token-check-deploy-sha   # if the real wrangler were ever reached, it could not authenticate

# THE FAKE wrangler, run by the helper as WRANGLER_RUN_BIN.
cat > "$T/bin/fake-wrangler.mjs" <<'EOF'
import { appendFileSync, writeFileSync } from "node:fs";
const a = process.argv.slice(2);
const s = a.join(" ");
if (a[0] === "deploy" && a.includes("--dry-run")) {
  appendFileSync(process.env.FAKE_CALLS, "dry-run\n");
  writeFileSync(a[a.indexOf("--metafile") + 1], JSON.stringify({ inputs: { "src/index.ts": { bytes: 1 } } }));
  process.exit(0);
}
appendFileSync(process.env.FAKE_CALLS, a.join(" ") + "\n");
if (s.startsWith("secret list")) { process.stdout.write('[{"name":"DEVICE_KEY_SECRET","type":"secret_text"}]\n'); process.exit(0); }
if (s.startsWith("deploy ")) { process.stdout.write("fake deploy\n"); process.exit(0); }
process.stderr.write("fake wrangler: unexpected call\n"); process.exit(99);
EOF
export WRANGLER_RUN_BIN="$(wpath "$T/bin/fake-wrangler.mjs")"

# THE MIRROR'S GIT: C0 is the base and HEAD; C1 and C2 (each touching proxy/src/index.ts) and CD (only docs/x.md) sit on
# side refs in the bare origin. A row sets origin's main to its tip; deploy.sh's own `git fetch` picks it up.
g init -q --bare "$T/origin.git" || { echo "RIG: git init --bare"; exit 2; }
( cd "$M" && g init -q && g add -A && g commit -q -m C0 && g remote add origin "$T/origin.git" && g push -q origin main ) \
  || { echo "RIG: the mirror's base commit"; exit 2; }
C0="$(g -C "$M" rev-parse HEAD)"
# RIG (attempt 2, ruled 2026-11-17): a plant edits the mirror's TRACKED deploy.sh, which would read as dirt and trip the
# dirty-tree guard before the pin. assume-unchanged hides ONLY that file; row DC1 proves the guard still fires on other dirt.
g -C "$M" update-index --assume-unchanged proxy/scripts/deploy.sh || { echo "RIG: assume-unchanged"; exit 2; }
g clone -q "$T/origin.git" "$T/side" || { echo "RIG: git clone"; exit 2; }
( cd "$T/side" && printf '// c1\n' >> proxy/src/index.ts && g commit -q -am C1 && g push -q origin HEAD:refs/heads/s-c1 \
  && printf '// c2\n' >> proxy/src/index.ts && g commit -q -am C2 && g push -q origin HEAD:refs/heads/s-c2 \
  && g checkout -q "$C0" && printf 'docs\n' >> docs/x.md && g commit -q -am CD && g push -q origin HEAD:refs/heads/s-cd ) \
  || { echo "RIG: the side commits"; exit 2; }
C1="$(g -C "$T/origin.git" rev-parse s-c1)"; C2="$(g -C "$T/origin.git" rev-parse s-c2)"; CD="$(g -C "$T/origin.git" rev-parse s-cd)"
tip() { case "$1" in C0) echo "$C0";; C1) echo "$C1";; C2) echo "$C2";; CD) echo "$CD";; esac; }

BLIPSCOPE_SESSION=alpha "$PY" "$M/scripts/session-claim.py" take deploy --covers proxy:staging --until +60m >/dev/null 2>&1 \
  || { echo "RIG: the deploy claim would not take"; exit 2; }

# One row: origin's main tip, EXPECT_SHA (unset|abc123|C1|HEAD), ALLOW_BEHIND (-|1), ALLOW_BEHIND_SHAS (-|C1|C1,C0|C1short).
# Prints "<exit> <upload 0|1> <token present 0|1>".
row() {
  local tipname=$1 expect=$2 ab=$3 abs=$4 token=$5
  local dirt=0; [ "$tipname" = C0dirty ] && { dirt=1; tipname=C0; }
  g -C "$T/origin.git" update-ref refs/heads/main "$(tip "$tipname")" || { echo "RIG: update-ref"; exit 2; }
  [ "$dirt" = 1 ] && echo '// dirt' >> "$M/proxy/src/index.ts"
  : > "$T/calls"
  local -a envs=(BLIPSCOPE_SESSION=alpha)
  case "$expect" in unset) ;; HEAD) envs+=(EXPECT_SHA="$C0");; C1) envs+=(EXPECT_SHA="$C1");; *) envs+=(EXPECT_SHA="$expect");; esac
  [ "$ab" = 1 ] && envs+=(ALLOW_BEHIND=1)
  case "$abs" in -) ;; C1) envs+=(ALLOW_BEHIND_SHAS="$C1");; C1,C0) envs+=(ALLOW_BEHIND_SHAS="$C1,$C0");; C1short) envs+=(ALLOW_BEHIND_SHAS="${C1:0:12}");; esac
  ( cd "$M" && env -u EXPECT_SHA -u ALLOW_BEHIND -u ALLOW_BEHIND_SHAS "${envs[@]}" bash "$SCRIPT" staging ) > "$T/out" 2>&1
  local rc=$?
  if [ "$dirt" = 1 ]; then g -C "$M" checkout -q -- proxy/src/index.ts || { echo "RIG: restore"; exit 2; }; fi
  [ -z "$(g -C "$M" status --porcelain -- proxy/)" ] || { echo "RIG: the mirror is not clean after a row"; exit 2; }
  # The upload field is a COUNT of the recorded upload calls (ruling (A)): a pass row predicts exactly 1, a refusal 0.
  local up; up="$(grep -c '^deploy --env staging --define' "$T/calls")"
  local tk=0; grep -qF -- "$(echo "$token" | sed "s/<C1:12>/${C1:0:12}/; s/<C2:12>/${C2:0:12}/; s/<C0:12>/${C0:0:12}/")" "$T/out" && tk=1
  echo "$rc $up $tk"
}

# name | tip | EXPECT_SHA | ALLOW_BEHIND | ALLOW_BEHIND_SHAS | token | predicted "<exit> <upload calls> <token>"
ROWS=(
  "ES1|C0|unset|-|-|EXPECT_SHA is not set|1 0 1"
  "ES2|C0|abc123|-|-|EXPECT_SHA must be a full 40-hex sha|1 0 1"
  "ES3|C0|C1|-|-|HEAD is not EXPECT_SHA|1 0 1"
  "ES4|C0|HEAD|-|-|pinned: HEAD is EXPECT_SHA|0 1 1"
  "AB1|C1|HEAD|1|-|ALLOW_BEHIND=1 is retired|1 0 1"
  "AB2|C1|HEAD|-|C1|named and behind: <C1:12>|0 1 1"
  "AB3|C2|HEAD|-|C1|behind and not named: <C2:12>|1 0 1"
  "AB4|C1|HEAD|-|C1,C0|named but not behind: <C0:12>|1 0 1"
  "AB5|C1|HEAD|-|C1short|ALLOW_BEHIND_SHAS must list full 40-hex shas|1 0 1"
  "AB6|CD|HEAD|-|-|pinned: HEAD is EXPECT_SHA|0 1 1"
  "DC1|C0dirty|HEAD|-|-|proxy/ has uncommitted changes|1 0 1"
)

RED=""
run_all() {
  RED=""
  for r in "${ROWS[@]}"; do
    IFS='|' read -r name tipn exp ab abs token pred <<< "$r"
    got="$(row "$tipn" "$exp" "$ab" "$abs" "$token")"
    [ "$got" = "$pred" ] || RED="$RED $name"
    [ "${1:-}" = loud ] && printf '%-4s %s  (predicted %s; exit upload token)\n' "$name" "$([ "$got" = "$pred" ] && echo "AS PREDICTED" || echo "OFF: got $got")" "$pred"
  done
  RED="${RED# }"
}

if [ "${1:-}" != "--sabotage" ]; then
  run_all loud
  [ -z "$RED" ] && { echo "ALL AS PREDICTED (${#ROWS[@]} rows)"; exit 0; }
  echo "OFF: $RED"; exit 1
fi

# --sabotage: each plant edits the mirror's copy of deploy.sh at an anchor that must be exactly one line; then every row
# runs, and the red set must equal the plant's registered set. The copy is restored by bytes after each plant.
run_all
[ -z "$RED" ] || { echo "RIG: the unplanted copy is not all as predicted (red: $RED)"; exit 2; }
echo "CONTROL (unmodified copy): every row as predicted"
PLANTS=(
  "S-ES1|ES1|  fatal \"EXPECT_SHA is not set: the deploy plan names the commit it ships (docs/deploy-sha-pin.md).\"; exit 1"
  "S-ES2|ES2|  fatal \"EXPECT_SHA must be a full 40-hex sha (got: \${EXPECT_SHA:-})\"; exit 1"
  "S-ES3|ES3|  fatal \"HEAD is not EXPECT_SHA: HEAD \$HEAD_SHA, the plan names \$EXPECT_SHA.\"; exit 1"
  "S-AB1|AB1|  fatal \"ALLOW_BEHIND=1 is retired: name the commits behind with ALLOW_BEHIND_SHAS=<full sha>,...\"; exit 1"
  "S-AB3|AB3|    fatal \"behind and not named: \${c:0:12} (a merge after the plan; name it in ALLOW_BEHIND_SHAS, or re-plan)\"; exit 1"
  "S-AB4|AB4|    fatal \"named but not behind: \${c:0:12} (the plan is stale)\"; exit 1"
  "S-AB5|AB5|      fatal \"ALLOW_BEHIND_SHAS must list full 40-hex shas (got: \$c)\"; exit 1"
)
off=0
for p in "${PLANTS[@]}"; do
  IFS='|' read -r pname want anchor <<< "$p"
  n="$(grep -cxF -- "$anchor" "$T/script.orig")"
  [ "$n" = 1 ] || { echo "$pname: RIG (its anchor is found $n times)"; off=1; continue; }
  "$PY" - "$(wpath "$T/script.orig")" "$(wpath "$SCRIPT")" "$anchor" <<'PYEOF'
import sys
src, dst, anchor = sys.argv[1], sys.argv[2], sys.argv[3]
t = open(src, encoding="utf-8", newline="").read()
lines = t.split("\n")
assert sum(1 for l in lines if l == anchor) == 1
new = "\n".join(("  : # planted: " + anchor.strip()[:40]) if l == anchor else l for l in lines)
assert new != t
open(dst, "w", encoding="utf-8", newline="").write(new)
PYEOF
  run_all
  cp "$T/script.orig" "$SCRIPT"; cmp -s "$T/script.orig" "$SCRIPT" || { echo "RIG: the copy was not restored"; exit 2; }
  if [ "$RED" = "$want" ]; then echo "$pname: AS REGISTERED (red: $RED)"; else echo "$pname: OFF (red: ${RED:-none}; registered: $want)"; off=1; fi
done
[ "$off" = 0 ] && { echo "PLANTS: every plant reddens exactly its registered rows"; exit 0; }
echo "PLANTS: OFF"; exit 1
