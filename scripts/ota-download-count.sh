#!/usr/bin/env bash
#
# INSTRUMENT B of the OTA control plan (docs/ota-control-plan.md, Phase 0).
#
# =============================================================================
# WHAT THIS IS FOR
#
# Devices fetch version.txt and their firmware image directly from GitHub
# Releases, so the update CHECK is invisible to our Worker. GitHub does expose
# one number about it: download_count per release asset. Taken immediately
# before a run window opens and immediately after it closes, the difference says
# whether anything was fetched at all.
#
# On its own that is weak -- it cannot tell a successful update from a download
# that failed to verify. Paired with Instrument A (src/fleet.ts, the firmware
# version each device reports on every feed request) it separates three
# outcomes:
#
#     download_count   reported FW   conclusion
#     increments       changes       success, end to end
#     increments       unchanged     downloaded, then failed to verify or apply
#     unchanged        unchanged     never fetched: the timer or discovery is dead
#
# =============================================================================
# THE COUNTER IS FLEET-WIDE. This is the invalidation condition that is easiest
# to breach by accident: download_count is per ASSET, not per device. A second
# unit powered on anywhere in the world during the window makes the delta
# uninterpretable. Invalidation condition 4 in the plan.
#
# =============================================================================
# VERIFY BEFORE TRUSTING A NULL READING
#
# `--control` fetches one asset by hand and re-reads the counter, which proves
# the counter moves at all. Run it as part of taking the baseline: a counter
# that has never been observed incrementing cannot distinguish "nothing fetched"
# from "this number does not work", and those are the two readings the whole
# instrument exists to tell apart.
#
# That fetch is itself a download, so it MUST be inside the recorded baseline --
# take the baseline, run the control, and use the post-control number as the
# window's starting value. The script prints them in that order for that reason.
#
# Usage:
#   scripts/ota-download-count.sh                 # read the counters
#   scripts/ota-download-count.sh --control       # read, prove the counter moves, re-read
#   scripts/ota-download-count.sh --save baseline # read and append to bench-logs/
#
# Exit: 0 read cleanly · 1 the API refused or returned no assets · 2 the rig is broken

set -o pipefail

REPO="Valar-Systems/valar-scopes"
API="https://api.github.com/repos/${REPO}/releases/latest"
# The asset the default SKU actually downloads. Named explicitly rather than
# guessed from the tag, because --control must fetch the SAME asset whose
# counter it then re-reads.
CONTROL_ASSET="version.txt"

LOGDIR="$(cd "$(dirname "$0")/.." && pwd)/bench-logs"
ERR="$(mktemp)"
trap 'rm -f "$ERR"' EXIT

# stderr to its own file, never merged and never discarded: a GitHub rate-limit
# refusal is a 403 with a JSON body, and a stdout-only read of it looks like a
# parse problem rather than a rate-limit problem. Ledger rule 15.
fetch() {
  curl -sS --max-time 30 -H "Accept: application/vnd.github+json" "$API" 2>"$ERR"
}

parse() {
  python -c '
import json,sys,time
raw=sys.stdin.read()
try:
    d=json.loads(raw)
except Exception as e:
    sys.stderr.write("REFUSED: response was not JSON (%s)\n" % e)
    sys.stderr.write(raw[:400]+"\n")
    sys.exit(2)
if isinstance(d,dict) and "message" in d and "assets" not in d:
    # A rate limit or a private repo lands here. It is a real answer and must
    # not be read as "no downloads".
    sys.stderr.write("REFUSED by the API: %s\n" % d["message"])
    sys.exit(1)
assets=d.get("assets") or []
if not assets:
    sys.stderr.write("REFUSED: release %s has no assets -- nothing to count.\n"
                     % d.get("tag_name"))
    sys.exit(1)
print("# %s  tag=%s" % (time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), d.get("tag_name")))
for a in sorted(assets, key=lambda a: a["name"]):
    print("%-34s %d" % (a["name"], a["download_count"]))
'
}

read_counts() {
  local body
  body="$(fetch)" || { echo "FAIL: curl could not reach the API." >&2; cat "$ERR" >&2; exit 2; }
  printf '%s' "$body" | parse
  local rc=$?
  if [ "$rc" -ne 0 ]; then cat "$ERR" >&2; exit "$rc"; fi
}

echo "=== baseline ==="
BASE="$(read_counts)" || exit $?
echo "$BASE"

if [ "${1:-}" = "--control" ]; then
  echo
  echo "=== control: fetching ${CONTROL_ASSET} by hand ==="
  echo "    (this IS a download and counts toward the baseline -- see the header)"
  URL="https://github.com/${REPO}/releases/latest/download/${CONTROL_ASSET}"
  if ! curl -sSL --max-time 30 -o /dev/null "$URL" 2>"$ERR"; then
    echo "FAIL: the control fetch itself failed. This is the RIG, not the counter." >&2
    cat "$ERR" >&2
    exit 2
  fi
  echo "    fetched ${URL}"
  # GitHub's counter is not instantaneous. Re-read a few times rather than
  # declaring it broken on the first look -- and say which read moved it, so a
  # slow counter is distinguishable from a dead one.
  BEFORE="$(printf '%s' "$BASE" | awk -v a="$CONTROL_ASSET" '$1==a {print $2}')"
  echo "    ${CONTROL_ASSET} before: ${BEFORE}"
  for attempt in 1 2 3 4 5 6; do
    sleep 10
    AFTER="$(read_counts | awk -v a="$CONTROL_ASSET" '$1==a {print $2}')"
    echo "    read ${attempt}: ${AFTER}"
    if [ -n "$AFTER" ] && [ -n "$BEFORE" ] && [ "$AFTER" -gt "$BEFORE" ]; then
      echo
      echo "CONTROL PASSED: the counter moved ${BEFORE} -> ${AFTER}."
      echo "  Use ${AFTER} as the window's starting value for ${CONTROL_ASSET}."
      exit 0
    fi
  done
  echo
  echo "CONTROL FAILED: ${CONTROL_ASSET} did not move after a hand fetch." >&2
  echo "  Do NOT open a run. A null reading from this counter would be" >&2
  echo "  indistinguishable from the counter not working." >&2
  exit 1
fi

if [ "${1:-}" = "--save" ]; then
  mkdir -p "$LOGDIR"
  OUT="$LOGDIR/ota-counts-${2:-snapshot}-$(date -u +%Y%m%dT%H%M%SZ).txt"
  printf '%s\n' "$BASE" > "$OUT"
  echo
  echo "saved: $OUT"
fi
