#!/usr/bin/env bash
#
# INSTRUMENT B, third and final attempt -- isolated.
#
# =============================================================================
# WHY THIS PROBE EXISTS AND WHY THE LAST ONE FAILED
#
# The previous probe fetched version.txt 3 times via the `latest` alias and 7
# times via the tag-pinned URL, in one pass, and read the counter after an hour.
# It moved +6 of 10. That result discriminates NOTHING:
#
#     latest wholesale uncounted, one tag fetch deduped   ->  0 + 6 = +6
#     both paths counted, dedup eating four across them   ->  2 + 4 = +6
#
# Two variables -- which path counts, and whether anything was deduplicated --
# were confounded in a single trial on an instrument already known to be
# unreliable. The only thing established was that the counter is not dead.
#
# TWO CORRECTIONS, BOTH STRUCTURAL:
#
# 1. ONE VARIABLE. `latest` only. Whether tag-pinned URLs count is irrelevant to
#    this instrument's job, because the DEVICE uses `latest`
#    (OTA_RELEASE_BASE in src/OtaUpdater.cpp ends in /releases/latest/download).
#    Testing it added a variable and no information.
#
# 2. SPACED, NOT BURSTED. Ten minutes between fetches, to defeat any short CDN
#    dedup window on an identical 2-byte object from one IP.
#
# =============================================================================
# THE DECISION, MADE IN ADVANCE SO THE RESULT CANNOT BE ARGUED WITH
#
#     counter moves by N   -> `latest` counts. Keep Instrument B, restore the
#                             three-outcome table in docs/ota-control-plan.md.
#     counter moves by 0   -> `latest` is uncounted. B can NEVER see a device
#                             download. Drop it.
#     anything in between  -> the counter is unreliable at the scale we would be
#                             reading it. That is its own answer: drop it.
#
# AND THEN DECIDE RATHER THAN ITERATE. A counter that under-counts by an
# uncharacterised amount makes a null reading nearly worthless, and an instrument
# whose nulls mean nothing is not worth a fourth session. Instrument A is the
# gate and it is already verified.
#
# WINDOW HYGIENE: download_count is fleet-wide. The bench board on COM4 is
# powered; its OTA check interval is the 24 h default, so a stray fetch is
# unlikely but not impossible. If the final delta is N+1, suspect exactly that
# before concluding anything clever.

set -o pipefail

REPO="Valar-Systems/valar-scopes"
API="https://api.github.com/repos/${REPO}/releases/latest"
URL="https://github.com/${REPO}/releases/latest/download/version.txt"
N="${N:-4}"                 # fetches
GAP="${GAP:-600}"           # seconds between them
SETTLE="${SETTLE:-1800}"    # seconds to wait after the last fetch
OUT="$(cd "$(dirname "$0")/.." && pwd)/bench-logs/ota-latest-probe.txt"

count() {
  curl -s --max-time 30 -H "Accept: application/vnd.github+json" "$API" \
    | python -c 'import json,sys
try:
    d=json.load(sys.stdin)
    print([a["download_count"] for a in d["assets"] if a["name"]=="version.txt"][0])
except Exception:
    print("ERR")'
}

{
  echo "instrument B, isolated probe -- latest alias only"
  echo "started  $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "plan     ${N} fetches, ${GAP}s apart, then ${SETTLE}s settle"
  BASE="$(count)"
  echo "baseline version.txt = ${BASE}"
  echo
} > "$OUT"

for i in $(seq 1 "$N"); do
  code="$(curl -sSL --max-time 30 -o /dev/null -w '%{http_code}' "$URL")"
  echo "fetch ${i}/${N}  HTTP ${code}  $(date -u +%H:%M:%SZ)" >> "$OUT"
  [ "$i" -lt "$N" ] && sleep "$GAP"
done

echo "" >> "$OUT"
echo "settling ${SETTLE}s..." >> "$OUT"
sleep "$SETTLE"

FINAL="$(count)"
{
  echo "final    version.txt = ${FINAL}   (baseline ${BASE})"
  if [ "$BASE" = "ERR" ] || [ "$FINAL" = "ERR" ]; then
    echo "VERDICT: the API read failed. This is the RIG, not the counter."
    exit 2
  fi
  D=$((FINAL - BASE))
  echo "delta    ${D} of ${N} fetches"
  echo
  if [ "$D" -eq "$N" ]; then
    echo "VERDICT: KEEP B. The latest alias counts exactly. Restore the"
    echo "  three-outcome table in docs/ota-control-plan.md."
  elif [ "$D" -eq 0 ]; then
    echo "VERDICT: DROP B. The latest alias is uncounted, so download_count can"
    echo "  never observe a device fetch. Run 1 reads ONE signal (Instrument A)."
  else
    echo "VERDICT: DROP B. The counter under-counts (${D} of ${N}) at exactly the"
    echo "  scale we would be reading it, so a null reading means nothing and"
    echo "  cannot distinguish 'checked, nothing to install' from 'never checked'."
    echo "  Run 1 reads ONE signal (Instrument A)."
  fi
} >> "$OUT"

cat "$OUT"
