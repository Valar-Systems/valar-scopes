#!/usr/bin/env bash
# routes-refresh-run.sh <staging|production> -- W5's whole logic (docs/route-weekly-code.md, section 2f). The workflow
# is a thin shell around this, so this is what is graded end to end (routes-run.selftest.sh, RR1-RR5, through the fake
# KV). Every failure ends in ONE class from routes-notify.ts's closed set, which opens or updates the route-refresh
# issue and sends ntfy; with no class the run is green.
#
# Test hooks, all refused by FK1 wherever a real credential is in reach (routes-fk1.ts --check):
#   ROUTES_KV_CLI      the fake KV                  ROUTES_SOURCE_DIR  local tarballs, <sha>.tar.gz
#   ROUTES_RUN_GH      canned {sha, date} answer    ROUTES_RUN_PLANT   a command run after each write (RR3)
# With any hook set, the environment and token checks (graded by SC2 and TK1) are skipped and say so, and the notice
# is a dry run (printed, never sent).
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.."
ENV="${1:-}"
case "$ENV" in staging|production) ;; *) echo "usage: routes-refresh-run.sh <staging|production>"; exit 2 ;; esac
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
TSX=(npx --no-install tsx)
KV="${ROUTES_KV_CLI:-scripts/wrangler-run.mjs}"
TEST=""; [ -n "${ROUTES_KV_CLI:-}${ROUTES_SOURCE_DIR:-}${ROUTES_RUN_GH:-}${ROUTES_RUN_PLANT:-}" ] && TEST=1
COUNTS=()

notify() { # notify <class>: the issue and ntfy (a dry run under the test hooks)
  if [ -n "$TEST" ]; then "${TSX[@]}" scripts/routes-notify.ts --class "$1" --env "$ENV" "${COUNTS[@]}" --dry-run
  else "${TSX[@]}" scripts/routes-notify.ts --class "$1" --env "$ENV" "${COUNTS[@]}"; fi
}
finish() { # finish <class>: the run failed or refused
  echo "routes-refresh: FAILED, class $1"
  [ -n "${GITHUB_OUTPUT:-}" ] && echo "class=$1" >> "$GITHUB_OUTPUT"
  notify "$1"
  exit 1
}
masked() { sed -E 's/e\.g\. .*/e.g. [masked]/' "$1"; } # verify FAIL lines carry sample callsigns: never in the log

# ---- 0. the test hooks, refused where a real credential is in reach -------------------------------------------------
if [ -n "$TEST" ]; then "${TSX[@]}" scripts/routes-fk1.ts --check "$ENV" || exit 2; fi

# ---- 1. the environment (W11) and 3. the token (TK1) ------------------------------------------------------------------
READBACK=off
if [ -n "$TEST" ]; then echo "environment check: SKIPPED (test hooks; graded by SC2)"
else
  "${TSX[@]}" scripts/routes-env-check.ts || finish secrets-not-set
  [ -n "${ROUTES_TEST_DEVICE:-}" ] && [ -n "${ROUTES_TEST_KEY:-}" ] && [ -n "${ROUTES_TEST_KEY_STAGING:-}" ] && READBACK=on
fi

# ---- 2. the freeze (W4), at this run's actual start -----------------------------------------------------------------
bash scripts/routes-freeze.sh; rc=$?
if [ "$rc" = 4 ]; then echo "routes-refresh: frozen, nothing done"; exit 0; fi
[ "$rc" = 0 ] || finish frozen-malformed

if [ -n "$TEST" ]; then echo "token check: SKIPPED (test hooks; graded by TK1)"
else
  "${TSX[@]}" scripts/routes-token-check.ts; rc=$?
  if [ "$rc" = 3 ]; then notify token-expiring || true; elif [ "$rc" != 0 ]; then finish secrets-not-set; fi
fi

# ---- 4. the source: main's sha and commit date, then its tarball by sha ---------------------------------------------
if [ -n "${ROUTES_RUN_GH:-}" ]; then SRC=$(node -e 'const j=require(process.argv[1]); console.log(j.sha+" "+j.date)' "$(cd "$(dirname "$ROUTES_RUN_GH")" && pwd)/$(basename "$ROUTES_RUN_GH")")
else SRC=$(gh api repos/vradarserver/standing-data/commits/main --jq '.sha + " " + .commit.committer.date' 2>/dev/null); fi
NEW="${SRC%% *}"; DATE="${SRC#* }"
echo "$NEW" | grep -qE '^[0-9a-f]{40}$' || finish source-unreachable
echo "$DATE" | grep -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$' || finish source-unreachable
fetch() { # fetch <sha>: the snapshot as $T/<sha>.tar.gz
  if [ -n "${ROUTES_SOURCE_DIR:-}" ]; then cp "$ROUTES_SOURCE_DIR/$1.tar.gz" "$T/$1.tar.gz" 2>/dev/null
  else curl -sfL --max-time 300 "https://codeload.github.com/vradarserver/standing-data/tar.gz/$1" -o "$T/$1.tar.gz"; fi
}
fetch "$NEW" || finish source-unreachable
echo "source: ${NEW:0:12}, committed $DATE"

# ---- 5. meta: a key-level seal, or this is a first run (attended, never CI's) ----------------------------------------
node "$KV" kv key get meta:routes --binding=ENRICH_KV "--env=$ENV" --remote > "$T/meta.json" 2> "$T/meta.err" || finish run-failed
read -r SEALED PREVSRC < <(node -e '
let m={}; try { m=JSON.parse(require("fs").readFileSync(process.argv[1],"utf8")); } catch {}
const ok=(s)=>typeof s==="string"&&/^[0-9a-f]{40}$/.test(s);
console.log((ok(m.sourceCommit)?m.sourceCommit:"-")+" "+(ok(m.prevSourceCommit)?m.prevSourceCommit:"-"));' "$T/meta.json")
[ "$SEALED" = "-" ] && finish meta-not-key-level

# ---- 6. the write: a changed source, or a replay of the last change -------------------------------------------------
if [ "$READBACK" = on ]; then # the identity as a token file outside any git tree, mode 600, never printed
  ( umask 077; printf 'test-device=%s\ntest-key=%s\ntest-key-staging=%s\n' "$ROUTES_TEST_DEVICE" "$ROUTES_TEST_KEY" "$ROUTES_TEST_KEY_STAGING" > "$T/tokens" )
  export BLIPSCOPE_TOKEN_FILE="$T/tokens"
fi
if [ "$NEW" = "$SEALED" ]; then
  [ "$PREVSRC" = "-" ] && finish meta-not-key-level
  fetch "$PREVSRC" || finish source-unreachable
  echo "source unchanged since the last seal (${PREVSRC:0:12} -> ${NEW:0:12})"
  W=(--key-level --replay --env "$ENV" --prev "$T/$PREVSRC.tar.gz" --file "$T/$NEW.tar.gz")
  REPAIRABLE=""; REPLAY=1
else
  fetch "$SEALED" || finish source-unreachable
  W=(--key-level --no-seal --env "$ENV" --prev "$T/$SEALED.tar.gz" --file "$T/$NEW.tar.gz" --readback "$READBACK")
  REPAIRABLE=1
fi
W+=(--out-written "$T/w.json" --out-deleted "$T/d.json")
write() {
  "${TSX[@]}" scripts/ingest-routes.ts "${W[@]}" > "$T/w.out" 2>&1; local rc=$?
  cat "$T/w.out"
  # D1 (docs/route-weekly-code.md 2n): a replay refused for a CURRENT journal (exit 6, JN1) becomes the key-level write
  # for the same source: --prev is the sealed snapshot (this source), so the writes and deletes are the journal's keys.
  if [ -n "${REPLAY:-}" ] && [ "$rc" = 6 ] && grep -q "(JN1)" "$T/w.out"; then
    echo "a journal of unsealed writes: the key-level write for the same source, not a replay"
    W=(--key-level --no-seal --env "$ENV" --prev "$T/$NEW.tar.gz" --file "$T/$NEW.tar.gz" --readback "$READBACK"
       --out-written "$T/w.json" --out-deleted "$T/d.json")
    REPAIRABLE=1; REPLAY=""
    write; return $?
  fi
  [ -n "${REPLAY:-}" ] && [ "$rc" = 0 ] && echo "replaying the last change (${PREVSRC:0:12} -> ${NEW:0:12})"
  local n; n=$(grep -oE 'key-level writes: [0-9]+' "$T/w.out" | grep -oE '[0-9]+$' | head -1); [ -n "$n" ] && COUNTS=(--count "written=$n")
  if [ "$rc" != 0 ]; then grep -q "REFUSING EVERYTHING (G1)" "$T/w.out" && finish refused-g1; finish run-failed; fi
  if grep -q "not sealed, and must not be" "$T/w.out"; then
    local g; g=$(grep -oE '^ +FAIL G[2-6]' "$T/w.out" | head -1 | grep -oE '[2-6]$'); finish "refused-g${g:-6}"
  fi
  [ -n "${ROUTES_RUN_PLANT:-}" ] && bash -c "$ROUTES_RUN_PLANT"
  return 0
}
verify() {
  "${TSX[@]}" scripts/verify-routes.ts --env "$ENV" --file "$T/$NEW.tar.gz" --check-values "$T/w.json" \
    --expect-absent "$T/d.json" --receipt "$T/r.json" > "$T/v.out" 2>&1; local rc=$?
  masked "$T/v.out"
  return $rc
}
write

# ---- 7-8. the verify, and at most one repair (the writes are idempotent; a replay writes nothing to repair) ----------
if ! verify; then
  [ -n "$REPAIRABLE" ] || finish verify-fail
  echo "verify FAILED: one repair (the same write again), then the verify again"
  write
  verify || finish verify-fail
fi

# ---- 9. the seal, last, on the receipt --------------------------------------------------------------------------------
"${TSX[@]}" scripts/ingest-routes.ts --key-level --seal-only --env "$ENV" --file "$T/$NEW.tar.gz" --receipt "$T/r.json" \
  --source-date "$DATE" || finish seal-refused
echo "routes-refresh: sealed $ENV at source ${NEW:0:12}"
