#!/usr/bin/env bash
# fresh-boot-acceptance.sh -- THE FRESH-BOOT ACCEPTANCE.
#
# The acceptance for the entire first-ten-minutes surface: everything a customer
# meets between plugging a device in and having a collection that survives a
# power cut. Runs before every release from v8 on (see RELEASING.md).
#
# WHY IT EXISTS. On 2026-08-21 a device that was actively logging showed an empty
# Collection page saying "Turn on the spotting logbook above" -- with the logbook
# on and claims landing. /logbook.json is served from NVS, and NVS was not written
# for the first ten minutes of uptime, so a factory-fresh unit was invisible to
# its own page for exactly as long as a new owner would be looking at it.
#
# Every check below is one of the ways that surface can be wrong. They are
# separate checks rather than one pass/fail because they fail for different
# reasons and each names its own repair.
#
# THIS IS A GUIDED PROCEDURE, NOT AN AUTOMATED TEST, and it says so rather than
# pretending: the physical steps (tap the glass, pull the power) cannot be driven
# from here. What IS automated is the evidence -- one continuous serial capture,
# asserted at the end against lines the firmware already prints.
#
#   ./scripts/fresh-boot-acceptance.sh COM119
#
set -u

# THREE MODES, because the guided flow cannot be driven by anything without a
# human at a terminal -- and the first attempt to have an agent run it walked
# straight into that: `read` on a closed stdin returns instantly, so all six
# steps "completed" in about a millisecond and the assertions would then have run
# against a log of an idle board. That direction fails loudly rather than passing
# falsely, so nothing wrong would have been believed -- but a guided procedure
# that silently skips its own guidance is one refactor away from the opposite.
# So it REFUSES rather than races, and splits into parts that drive separately:
#
#   fresh-boot-acceptance.sh COM119                  guided, needs a TTY
#   fresh-boot-acceptance.sh --capture-only COM119   start capture, print log path
#   fresh-boot-acceptance.sh --probe-claim <log>     step 4: right after the claim
#   fresh-boot-acceptance.sh --grade-cut <log>       step 5: start BEFORE pulling the power
#   fresh-boot-acceptance.sh --grade-off <log>       step 6: after the logbook-off save
#   fresh-boot-acceptance.sh --grade-boots <log>     boots at the Worker, cut to end of step 6
#   fresh-boot-acceptance.sh --collect COM119 <log>  reopen serial, AFTER everything is graded
#   fresh-boot-acceptance.sh --sabotage-extra-reset COM119 <log>   the boot check must go red
#   fresh-boot-acceptance.sh --assert-only <log>     assert an existing capture
#
# The split is what lets an agent hold the capture and the assertions while a
# human does the physical steps, which is how this is actually run.
MODE="guided"
case "${1:-}" in
  --capture-only) MODE="capture"; shift ;;
  --assert-only)  MODE="assert";  shift ;;
  --probe-claim)  MODE="probe";   shift ;;
  --grade-cut)    MODE="gcut";    shift ;;
  --grade-off)    MODE="goff";    shift ;;
  --grade-boots)  MODE="gboots";  shift ;;
  --collect)      MODE="collect"; shift ;;
  --sabotage-extra-reset) MODE="sabotage"; shift ;;
  --selftest)     MODE="selftest"; shift ;;
esac

PORT=""
if [ "$MODE" = "selftest" ]; then
  :
elif [ "$MODE" = "collect" ] || [ "$MODE" = "sabotage" ]; then
  PORT="${1:-}"; LOG="${2:-}"
  if [ -z "$PORT" ] || [ -z "$LOG" ] || [ ! -f "$LOG" ]; then
    echo "usage: $0 --collect <COM port> <log file> | --sabotage-extra-reset <COM port> <log file>" >&2
    exit 2
  fi
elif [ "$MODE" = "assert" ] || [ "$MODE" = "probe" ] || [ "$MODE" = "gcut" ] || [ "$MODE" = "goff" ] || [ "$MODE" = "gboots" ]; then
  LOG="${1:-}"
  if [ -z "$LOG" ] || [ ! -f "$LOG" ]; then
    echo "usage: $0 --assert-only | --probe-claim | --grade-cut | --grade-off | --grade-boots <log file>" >&2
    exit 2
  fi
else
  PORT="${1:-}"
  if [ -z "$PORT" ]; then
    echo "usage: $0 [--capture-only] <COM port> | --assert-only <log> | --probe-claim <log> | --selftest" >&2
    exit 2
  fi
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# --selftest: the assertions against synthetic captures, each built to fail ONE way, plus a
# control that passes. A check that cannot be shown failing proves nothing (CLAUDE.md).
# The IP is from 192.0.2.0/24 (documentation range): no probe ever leaves the machine.
if [ "$MODE" = "selftest" ]; then
  T="$(mktemp -d)"; st=0
  # The capture ends at the power cut by design (ruling, 2026-10-08): steps 5-6 and the boot
  # count come from the graders' result files, not from serial lines after the cut.
  base() { cat <<'L'
10:00:00 [capture] attached to COM18 (pyserial, one open)
10:00:01 [build] env=blipscope-s3-128 fw=v17 features=cloud
10:00:02 [reset] tier=factory cleared=wifi,wifi-fast,config,logbook preserved=cloud-key-fac
10:00:05 [build] env=blipscope-s3-128 fw=v17 features=cloud
10:00:05 [boot] reset reason=SW
10:00:10 [WiFi] CONNECTED  IP=192.0.2.10  RSSI=-50 dBm
10:00:12 [logbook] loaded 0 types (0 claimed), 0 airlines (0), 0 countries (0), 0 contacts
10:02:00 [claim] E75L claimed (1/10 types)
10:02:01 [logbook] persisted (1/10 types claimed, 3 airlines, 1 countries, 40 contacts; 900 B blobs, 2300 NVS entries free)
10:02:30 [GET] Handling request to config web server...
10:04:00 [capture] port dropped (SerialException) -- NOT reopening: steps 5-6 are graded over the network
L
  }
  probe_ok='probe 10:02:05 claim=E75L claim_line=8 ip=192.0.2.10 http=200 verdict=CLAIMED first=yes'
  s5_ok='step5 cut=1000 back=1030 at=1050 claim=E75L ip=192.0.2.10 http=200 verdict=CLAIMED tries=2'
  s6_ok='step6 at=1200 claim=E75L logbook=OFF http=200/200 verdict=CLAIMED'
  bt_ok='boots from=1000 to=1200 n=1 reasons=POWERON anchor=1 dev=0000 verdict=ONE_POWERON'
  mk() { # name, sed-or-empty, probe, [step5], [step6], [boots]   (empty = the passing line; NONE = no file)
    base | { if [ -n "$2" ]; then sed "$2"; else cat; fi; } > "$T/$1.log"
    [ "$3" = NONE ] || printf '%s\n' "$3" > "$T/$1.log.probe"
    [ "${4:-$s5_ok}" = NONE ] || printf '%s\n' "${4:-$s5_ok}" > "$T/$1.log.step5"
    [ "${5:-$s6_ok}" = NONE ] || printf '%s\n' "${5:-$s6_ok}" > "$T/$1.log.step6"
    [ "${6:-$bt_ok}" = NONE ] || printf '%s\n' "${6:-$bt_ok}" > "$T/$1.log.boots"
  }
  case_() { # name, want-exit, fail-name-or-empty, must-say-or-empty
    local out ex
    out="$(bash "$0" --assert-only "$T/$1.log" 2>&1)"; ex=$?
    local ok=1
    [ "$ex" -eq "$2" ] || ok=0
    [ -z "$3" ] || printf '%s\n' "$out" | grep -q "FAIL  $3" || ok=0
    [ -z "$4" ] || printf '%s\n' "$out" | grep -qF "$4" || ok=0
    if [ "$ok" -eq 1 ]; then printf '  ok    %s\n' "$1"; else printf '  FAIL  %s (exit %s)\n%s\n' "$1" "$ex" "$out"; st=1; fi
  }
  S4="step 4: the FIRST Collection view after the claim included it"
  S5="step 5: the claim is on the owner's Collection page after the power cut"
  S6A="step 6: the logbook was turned off (config page)"
  S6B="step 6: the claim is still on the Collection page with the logbook off"
  SB="boots: exactly ONE boot at the Worker from the cut to the end of step 6, and it is POWERON"
  mk good "" "$probe_ok"
  mk stale "" "${probe_ok/verdict=CLAIMED/verdict=UNCLAIMED}"
  mk noprobe "" NONE
  mk notfirst "" "${probe_ok/first=yes/first=no}"
  mk neveropened '/\[GET\] Handling request/d' "$probe_ok"
  # An empty verdict is a broken probe, and must not read as the v16 failure (2026-10-08, P1).
  mk noverdict "" "${probe_ok/verdict=CLAIMED/verdict=}"
  mk lost5 "" "$probe_ok" "${s5_ok/verdict=CLAIMED/verdict=ABSENT}"
  mk nocut "" "$probe_ok" "step5 cut=- back=- at=1050 claim=E75L ip=192.0.2.10 http=- verdict=NOCUT tries=0"
  mk no5 "" "$probe_ok" NONE
  mk stillon "" "$probe_ok" "" "${s6_ok/logbook=OFF/logbook=ON}"
  mk discarded "" "$probe_ok" "" "${s6_ok/verdict=CLAIMED/verdict=ABSENT}"
  mk twoboots "" "$probe_ok" "" "" "boots from=1000 to=1200 n=2 reasons=POWERON,USB anchor=1 dev=0000 verdict=WRONG"
  mk noboots "" "$probe_ok" "" "" "boots from=1000 to=1200 n=0 reasons=- anchor=1 dev=0000 verdict=NONE"
  mk blindboots "" "$probe_ok" "" "" "boots verdict=UNTRUSTWORTHY why=query-failed"
  case_ good 0 "" ""
  case_ stale 1 "$S4" "had it UNCLAIMED"
  case_ noprobe 1 "$S4" "No probe was recorded"
  case_ notfirst 1 "$S4" "not the first fetch"
  case_ neveropened 1 "step 4: Collection was opened after the claim" "Collection was never opened; the step was not performed."
  case_ noverdict 1 "$S4" "BLIND: the probe produced no verdict"
  case_ lost5 1 "$S5" "lost their collection"
  case_ nocut 1 "$S5" "No power cut was seen"
  case_ no5 1 "$S5" "Step 5 was not graded"
  case_ stillon 1 "$S6A" "step 6 was not performed"
  case_ discarded 1 "$S6B" "Disabling discarded the collection"
  case_ twoboots 1 "$SB" "2 boot(s) at the Worker"
  case_ noboots 1 "$SB" "BLIND"
  case_ blindboots 1 "$SB" "UNTRUSTWORTHY"
  # The PROBE itself, through its FBA_PROBE_BODY seam: the parse and the interpreter, with the
  # body kept as the artifact. Each wants ONE exact verdict.
  mkdir -p "$T/stub"
  for c in python3 python py; do printf '#!/bin/sh\necho "Python was not found"\nexit 49\n' > "$T/stub/$c"; chmod +x "$T/stub/$c"; done
  pcase() { # name, body, want-verdict, stub-pythons(yes|no)
    local got; base > "$T/p-$1.log"; printf '%s' "$2" > "$T/p-$1.body"
    if [ "$4" = yes ]; then PATH="$T/stub:$PATH" FBA_PROBE_BODY="$T/p-$1.body" bash "$0" --probe-claim "$T/p-$1.log" >/dev/null 2>&1
    else FBA_PROBE_BODY="$T/p-$1.body" bash "$0" --probe-claim "$T/p-$1.log" >/dev/null 2>&1; fi
    got="$(sed -n 's/.* verdict=\([^ ]*\).*/\1/p' "$T/p-$1.log.probe" 2>/dev/null)"
    if [ "$got" = "$3" ] && cmp -s "$T/p-$1.body" "$T/p-$1.log.probe.json"; then printf '  ok    probe %s -> %s\n' "$1" "$got"
    else printf '  FAIL  probe %s: verdict=%s (want %s), body kept: %s\n' "$1" "${got:-<empty>}" "$3" "$(cmp -s "$T/p-$1.body" "$T/p-$1.log.probe.json" && echo yes || echo no)"; st=1; fi
  }
  pcase claimed   '{"types":[{"code":"E75L","claimed":true}]}'  CLAIMED   no
  pcase unclaimed '{"types":[{"code":"E75L","claimed":false}]}' UNCLAIMED no
  pcase absent    '{"types":[{"code":"B738","claimed":true}]}'  ABSENT    no
  pcase garbage   '<html>not json</html>'                       NOPARSE   no
  pcase stubpy    '{"types":[{"code":"E75L","claimed":true}]}'  NOPARSE   yes
  # The STEP-5, STEP-6 and BOOT graders, run whole through their seams (pings, pages, Worker
  # rows). Each wants ONE exact field value in its result file.
  printf '%s' '{"types":[{"code":"E75L","claimed":true}]}' > "$T/claimed.json"
  printf '%s' '{"types":[{"code":"E75L","claimed":false}]}' > "$T/unclaimed.json"
  printf '<label>Spotting logbook <input name="logbook" type="checkbox" checked></label>\n' > "$T/cfg-on.html"
  printf '<label>Spotting logbook <input name="logbook" type="checkbox" ></label>\n' > "$T/cfg-off.html"
  gcase() { # name, mode, result-suffix, want field=value, then env assignments
    local name="$1" mode="$2" f="$3" want="$4" got; shift 4
    base > "$T/g-$name.log"
    if [ "$mode" = --grade-boots ]; then printf '%s\n' "$s5_ok" > "$T/g-$name.log.step5"; printf '%s\n' "$s6_ok" > "$T/g-$name.log.step6"; fi
    env FBA_POLL_S=0 FBA_CUT_WAIT_S=3 FBA_BACK_WAIT_S=3 FBA_LB_WAIT_S=1 FBA_DEVICE_ID=0000000000000000 "$@" \
      bash "$0" "$mode" "$T/g-$name.log" >/dev/null 2>&1
    got="$(grep -o " ${want%%=*}=[^ ]*" "$T/g-$name.log.$f" 2>/dev/null | head -1 | sed 's/^ //')"
    if [ "$got" = "$want" ]; then printf '  ok    %s -> %s\n' "$name" "$got"
    else printf '  FAIL  %s: %s (want %s)\n' "$name" "${got:-<no result>}" "$want"; st=1; fi
  }
  printf '1\n1\n0\n0\n0\n1\n' > "$T/seq-good";  printf '1\n0\n1\n' > "$T/seq-blip"
  printf '1\n0\n0\n0\n1\n' > "$T/seq-lost";     printf '1\n0\n' > "$T/seq-noback"
  gcase cut-good   --grade-cut step5 verdict=CLAIMED   FBA_ALIVE_SEQ="$T/seq-good"   FBA_LOGBOOK_BODY="$T/claimed.json"
  gcase cut-blip   --grade-cut step5 verdict=NOCUT     FBA_ALIVE_SEQ="$T/seq-blip"   FBA_LOGBOOK_BODY="$T/claimed.json"
  gcase cut-lost   --grade-cut step5 verdict=UNCLAIMED FBA_ALIVE_SEQ="$T/seq-lost"   FBA_LOGBOOK_BODY="$T/unclaimed.json"
  gcase cut-noback --grade-cut step5 verdict=NOBACK    FBA_ALIVE_SEQ="$T/seq-noback" FBA_LOGBOOK_BODY="$T/claimed.json"
  gcase off-off    --grade-off step6 logbook=OFF       FBA_CONFIG_BODY="$T/cfg-off.html" FBA_LOGBOOK_BODY="$T/claimed.json"
  gcase off-on     --grade-off step6 logbook=ON        FBA_CONFIG_BODY="$T/cfg-on.html"  FBA_LOGBOOK_BODY="$T/claimed.json"
  gcase off-gone   --grade-off step6 verdict=UNCLAIMED FBA_CONFIG_BODY="$T/cfg-off.html" FBA_LOGBOOK_BODY="$T/unclaimed.json"
  # Worker rows: the cut is 1000 and step 6 ends at 1200 (s5_ok / s6_ok); 500 is step 1's boot.
  printf '500 SW\n1040 POWERON\n' > "$T/rows-one";            printf '500 SW\n1040 POWERON\n1100 USB\n' > "$T/rows-two"
  printf '1040 POWERON\n' > "$T/rows-noanchor";               printf '500 SW\n1040 USB\n' > "$T/rows-usb"
  printf '500 SW\n1040 POWERON\n1300 USB\n' > "$T/rows-after"; printf '500 SW\n' > "$T/rows-none"
  gcase boots-one      --grade-boots boots verdict=ONE_POWERON   FBA_BOOT_ROWS="$T/rows-one"
  gcase boots-two      --grade-boots boots verdict=WRONG         FBA_BOOT_ROWS="$T/rows-two"
  gcase boots-noanchor --grade-boots boots verdict=UNTRUSTWORTHY FBA_BOOT_ROWS="$T/rows-noanchor"
  gcase boots-usb      --grade-boots boots verdict=WRONG         FBA_BOOT_ROWS="$T/rows-usb"
  # A reset AFTER step 6 -- the --collect reopen -- is recorded, never counted.
  gcase boots-after    --grade-boots boots verdict=ONE_POWERON   FBA_BOOT_ROWS="$T/rows-after"
  gcase boots-none     --grade-boots boots verdict=NONE          FBA_BOOT_ROWS="$T/rows-none"
  rm -rf "$T"
  [ "$st" -eq 0 ] && echo "SELFTEST PASSED" || echo "SELFTEST FAILED"
  exit "$st"
fi
if [ "$MODE" = "guided" ] || [ "$MODE" = "capture" ]; then
  STAMP="$(date -u +%Y-%m-%dT%H%M%SZ)"
  LOG="$ROOT/bench-logs/fresh-boot-acceptance-$STAMP.log"
  mkdir -p "$ROOT/bench-logs"
fi

if [ "$MODE" = "guided" ] || [ "$MODE" = "capture" ]; then
cat <<BANNER

  FRESH-BOOT ACCEPTANCE          port $PORT
  log: $LOG

  Capture starts now and runs until you finish the last step. Do the steps in
  order -- several checks depend on the ORDER, not just the outcome.

BANNER
fi

# THE CAPTURE (ruling, 2026-10-08): pyserial, dtr/rts set False BEFORE the open, and ONE open.
# When the port drops -- the step-5 power cut -- it writes a marker and EXITS instead of
# reopening. Twice on 2026-10-08 the old capture's reopen after the cut reset the board it was
# measuring (rst:0x15), and the wait meant to prevent that was timed from GetPortNames(), which
# kept listing COM18 for more than 15 s while the board was unplugged. A port list is never a
# board-present signal (CLAUDE.md, the capture rig). Steps 5-6 and the boot count are graded
# without serial (--grade-cut, --grade-off, --grade-boots); serial is reopened only at the very
# end, by --collect, after all of them, where a reset is recorded and cannot change a result.
#
# DTR/RTS false so attaching does not itself reset the board: step 1 must be YOUR factory reset,
# not one this script caused. A reset we triggered would still produce a passing log and prove
# nothing about the path a customer takes.
CAPTURE_PY="$ROOT/scripts/fresh-boot-capture.py"

# A path a Windows interpreter can open. Windows Python cannot resolve an MSYS path (/tmp/...,
# /c/...) and reports the file absent rather than failing (CLAUDE.md).
winpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi; }

# An interpreter PROVEN by running it, not found by name -- with pyserial when asked. On Windows
# `python3` can be the Microsoft Store stub, which prints "Python was not found" and exits. The
# first real step-4 probe took it, parsed nothing and wrote an empty verdict, which step 4 read as
# the v16 failure it was predicting (2026-10-08).
find_py() {
  local c mods='import json'
  [ "${1:-}" = serial ] && mods='import json, serial'
  for c in python3 python py; do
    "$c" -c "$mods" >/dev/null 2>&1 && { printf '%s' "$c"; return 0; }
  done
  return 1
}

start_capture() {
  local py exe i=0
  py="$(find_py serial)" || { echo "CAPTURE CANNOT START: no working python with pyserial (pip install pyserial)." >&2; exit 3; }
  if command -v powershell.exe >/dev/null 2>&1; then
    # Detached through Start-Process so it outlives this shell. The FULL interpreter path, from the
    # interpreter itself: PowerShell resolves a bare `python` by its own PATH, which can land on the
    # Store stub even where bash's does not.
    exe="$("$py" -c 'import sys; print(sys.executable)')"
    powershell.exe -NoProfile -Command "Start-Process -FilePath '$exe' -ArgumentList '\"$(winpath "$CAPTURE_PY")\"','capture','$PORT','\"$(winpath "$LOG")\"' -WindowStyle Hidden" >/dev/null 2>&1
  else
    nohup "$py" "$CAPTURE_PY" capture "$PORT" "$LOG" >/dev/null 2>&1 &
  fi
  # PROVE IT ATTACHED. A capture that silently failed to start produces an empty log, and an empty
  # log makes every assertion below fail for the wrong reason -- which reads as "the device is
  # broken" rather than "the capture is broken".
  while [ $i -lt 15 ]; do
    grep -aq "\[capture\] attached" "$LOG" 2>/dev/null && return 0
    grep -aq "\[capture\] could not open" "$LOG" 2>/dev/null && break
    sleep 1
    i=$((i+1))
  done
  echo "CAPTURE DID NOT START: no [capture] attached line in $LOG." >&2
  echo "  Nothing below would mean anything. Check the board is powered and on $PORT." >&2
  exit 3
}

# THE FIRST COLLECTION VIEW AFTER A CLAIM, read the way the page reads it.
#
# The page is served from NVS (Logbook::JsonStream). A /logbook.json request only ASKS for a
# save, which lands a frame or two after its own response, and fetch-triggered saves are
# rate-limited to one per 30 s. So "a save followed the page request" is true on every board
# and proves nothing about what the page SHOWED. On v16, Daniel's first view after the claim
# did not show it (2026-10-08). This fetches /logbook.json ONCE, right after the claim, before
# anyone opens the page, and records whether the claimed type is marked claimed. That is the
# customer's first view, measured.
PROBE_FILE() { printf '%s.probe' "$LOG"; }

# What the owner's Collection page says about TYPE, from a saved /logbook.json body. Read from
# stdin, so a Windows interpreter never has to resolve an MSYS path. Only three answers are
# evidence; anything else is the instrument, and says so.
lb_verdict() { # body-file type
  local py v
  py="$(find_py)" || { echo NOPARSE; return 0; }
  v="$("$py" -c "import json,sys
t=sys.argv[1]
try:
    d=json.load(sys.stdin)
except Exception:
    print('NOPARSE'); sys.exit()
e=[x for x in d.get('types',[]) if x.get('code')==t]
print('CLAIMED' if e and e[0].get('claimed') else ('UNCLAIMED' if e else 'ABSENT'))" "$2" < "$1" 2>/dev/null)"
  case "$v" in CLAIMED|UNCLAIMED|ABSENT) echo "$v" ;; *) echo NOPARSE ;; esac
}

probe_claim() {
  local hit cl type ip out code body verdict first
  hit="$(grep -an "\[claim\] [A-Z0-9]* claimed" "$LOG" | tail -1)"
  cl="${hit%%:*}"
  type="$(printf '%s' "$hit" | sed -n 's/.*\[claim\] \([A-Z0-9]*\) claimed.*/\1/p')"
  # FBA_DEVICE_IP overrides the log, for a capture attached to a board that was already running
  # (its boot-time CONNECTED line is not in this log).
  ip="${FBA_DEVICE_IP:-$(grep -a "\[WiFi\] CONNECTED" "$LOG" | tail -1 | sed -n 's/.*IP=\([0-9.]*\).*/\1/p')}"
  if [ -z "$type" ] || [ -z "$ip" ]; then
    printf 'probe claim=%s claim_line=%s ip=%s http=0 verdict=NOPROBE first=no\n' "${type:--}" "${cl:-0}" "${ip:--}" > "$(PROBE_FILE)"
    echo "  probe: no claim or no device IP in the log yet" >&2; return 1
  fi
  # FIRST means first: a config-page load after the claim may already have fetched it.
  first=yes
  tail -n +"$((cl + 1))" "$LOG" | grep -aq "\[GET\] Handling request to config web server" && first=no
  # FBA_PROBE_BODY names a file to use as the response instead of fetching: the --selftest seam
  # that exercises the parse below. Never set on a real run.
  if [ -n "${FBA_PROBE_BODY:-}" ]; then
    code=200; body="$(cat "$FBA_PROBE_BODY")"
  else
    out="$(curl -s -m 15 -w '\n%{http_code}' "http://$ip/logbook.json")"
    code="${out##*$'\n'}"; body="${out%$'\n'*}"
  fi
  # Keep what the page SAID, so the verdict can be re-derived from the artifact. The first real
  # probe (2026-10-08) kept only its verdict, and when that came back empty the run was lost.
  printf '%s' "$body" > "$LOG.probe.json"
  verdict="$(lb_verdict "$LOG.probe.json" "$type")"
  [ "$code" = "200" ] || verdict=NOFETCH
  printf 'probe %s claim=%s claim_line=%s ip=%s http=%s verdict=%s first=%s\n' \
    "$(date +%H:%M:%S)" "$type" "$cl" "$ip" "$code" "$verdict" "$first" > "$(PROBE_FILE)"
  echo "  probe: first /logbook.json after the claim of $type -> $verdict (http $code, first=$first)"
}

# ---- STEPS 5 AND 6, GRADED WITHOUT SERIAL (ruling, 2026-10-08) -----------------------------
# Owner-visible facts only: the board answering on the network, its Collection page
# (/logbook.json) and its config page. Serial is not reopened until all of these are graded.
dev_ip() { printf '%s' "${FBA_DEVICE_IP:-$(grep -a "\[WiFi\] CONNECTED" "$LOG" | tail -1 | sed -n 's/.*IP=\([0-9.]*\).*/\1/p')}"; }
claim_type() { grep -a "\[claim\] [A-Z0-9]* claimed" "$LOG" | head -1 | sed -n 's/.*\[claim\] \([A-Z0-9]*\) claimed.*/\1/p'; }
POLL_S="${FBA_POLL_S:-1}"
CUT_WAIT_S="${FBA_CUT_WAIT_S:-1800}"; BACK_WAIT_S="${FBA_BACK_WAIT_S:-300}"; LB_WAIT_S="${FBA_LB_WAIT_S:-120}"

# Does the board answer? ICMP, not HTTP: a ping touches no handler, requests no logbook save and
# prints nothing, and a board with no power cannot answer one -- unlike a port list. Judged by a
# TTL in the reply, not by ping's exit status: Windows ping exits 0 on "Destination host
# unreachable". FBA_ALIVE_SEQ (selftest seam): a file of 1/0 lines, one used per call, the last
# one repeating.
alive() {
  if [ -n "${FBA_ALIVE_SEQ:-}" ]; then
    local v; v="$(head -1 "$FBA_ALIVE_SEQ")"
    [ "$(wc -l < "$FBA_ALIVE_SEQ")" -gt 1 ] && sed -i '1d' "$FBA_ALIVE_SEQ"
    [ "$v" = 1 ]; return
  fi
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ping -n 1 -w 1000 "$1" 2>/dev/null | grep -qi "ttl=" ;;
    *) ping -c 1 -W 1 "$1" 2>/dev/null | grep -qi "ttl=" ;;
  esac
}

# GET a page into a file and print the http code. FBA_LOGBOOK_BODY / FBA_CONFIG_BODY (selftest
# seams) stand in for /logbook.json and / .
fetch_to() { # ip path out
  local seam=""
  case "$2" in /logbook.json) seam="${FBA_LOGBOOK_BODY:-}" ;; /) seam="${FBA_CONFIG_BODY:-}" ;; esac
  if [ -n "$seam" ]; then cp "$seam" "$3"; echo 200; return 0; fi
  curl -s -m 15 -o "$3" -w '%{http_code}' "http://$1$2"
}

# STEP 5. Start it BEFORE the power is pulled: it has to see the board go quiet to know there was
# a cut at all. Three missed pings in a row make a cut (one dropped ping on Wi-Fi is not a power
# cut); one reply makes the board back. Then the claimed type must be on the owner's page. That
# wait is retried, because the web server answers before the logbook has loaded -- and a lost
# collection cannot satisfy it however long it waits, so the retry cannot pass the failing world.
grade_cut() {
  local ip type miss=0 first_miss="" t_cut="" t_back="" code="" verdict="" tries=0 dl
  ip="$(dev_ip)"; type="$(claim_type)"
  if [ -z "$ip" ] || [ -z "$type" ]; then
    printf 'step5 claim=%s ip=%s verdict=NOCLAIM\n' "${type:--}" "${ip:--}" > "$LOG.step5"
    echo "  step 5: no claim or no device IP in the log" >&2; return 1
  fi
  echo "  step 5: watching $ip for the power cut..."
  dl=$(( $(date +%s) + CUT_WAIT_S ))
  while [ "$(date +%s)" -lt "$dl" ]; do
    if alive "$ip"; then miss=0; first_miss=""
    else
      miss=$((miss + 1)); [ -z "$first_miss" ] && first_miss="$(date +%s)"
      [ "$miss" -ge 3 ] && { t_cut="$first_miss"; break; }
    fi
    sleep "$POLL_S"
  done
  if [ -z "$t_cut" ]; then
    verdict=NOCUT
  else
    echo "  step 5: the board went quiet; waiting for it to come back..."
    dl=$(( $(date +%s) + BACK_WAIT_S ))
    while [ "$(date +%s)" -lt "$dl" ]; do
      alive "$ip" && { t_back="$(date +%s)"; break; }
      sleep "$POLL_S"
    done
    if [ -z "$t_back" ]; then
      verdict=NOBACK
    else
      dl=$(( $(date +%s) + LB_WAIT_S ))
      while :; do
        tries=$((tries + 1))
        code="$(fetch_to "$ip" /logbook.json "$LOG.step5.json")"
        if [ "$code" = 200 ]; then verdict="$(lb_verdict "$LOG.step5.json" "$type")"; else verdict=NOFETCH; fi
        [ "$verdict" = CLAIMED ] && break
        [ "$(date +%s)" -ge "$dl" ] && break
        sleep "$POLL_S"
      done
    fi
  fi
  printf 'step5 cut=%s back=%s at=%s claim=%s ip=%s http=%s verdict=%s tries=%s\n' \
    "${t_cut:--}" "${t_back:--}" "$(date +%s)" "$type" "$ip" "${code:--}" "$verdict" "$tries" > "$LOG.step5"
  echo "  step 5: $type on the owner's Collection page after the cut -> $verdict (tries $tries)"
}

# STEP 6, after the owner turned the logbook OFF and saved. Two facts from two owner-visible
# pages: the config page shows the logbook OFF -- without that, "still present" would be true of
# a book nobody switched off, and the step would pass unperformed -- and Collection still has the
# claim. The config page carries the location, so it is read and deleted, never kept.
grade_off() {
  local ip type c1 c2 state verdict tmp
  ip="$(dev_ip)"; type="$(claim_type)"; tmp="$(mktemp)"
  c1="$(fetch_to "$ip" / "$tmp")"
  if [ "$c1" != 200 ]; then state=NOFETCH
  elif grep -Eq '<input name="logbook" type="checkbox" *checked' "$tmp"; then state=ON
  elif grep -Eq '<input name="logbook" type="checkbox" *>' "$tmp"; then state=OFF
  else state=UNKNOWN; fi
  rm -f "$tmp"
  c2="$(fetch_to "$ip" /logbook.json "$LOG.step6.json")"
  if [ "$c2" = 200 ]; then verdict="$(lb_verdict "$LOG.step6.json" "$type")"; else verdict=NOFETCH; fi
  printf 'step6 at=%s claim=%s logbook=%s http=%s/%s verdict=%s\n' "$(date +%s)" "${type:--}" "$state" "$c1" "$c2" "$verdict" > "$LOG.step6"
  echo "  step 6: the logbook is $state on the config page; $type on the Collection page -> $verdict"
}

# The device id the Worker files boot rows under: FBA_DEVICE_ID, else the board's own config page
# (window.BP_DEVID). Never printed; the result records a 4-character prefix.
dev_id() {
  [ -n "${FBA_DEVICE_ID:-}" ] && { printf '%s' "$FBA_DEVICE_ID"; return 0; }
  local tmp id
  tmp="$(mktemp)"
  fetch_to "$(dev_ip)" / "$tmp" >/dev/null
  id="$(grep -o "window.BP_DEVID='[0-9a-f]\{16\}'" "$tmp" | head -1 | sed "s/.*='\(.*\)'/\1/")"
  rm -f "$tmp"
  [ -n "$id" ] && printf '%s' "$id"
}

# "<epoch> <reason>" boot rows for a device in [from, to]: the Worker's X-Blip-Boot rows, read
# with the dashboard's own query (scripts/fba-boot-rows.mjs). FBA_BOOT_ROWS (selftest seam): a
# file of such lines. stderr goes to its own file, so a failed query says why.
boot_rows() { # dev from to errfile
  if [ -n "${FBA_BOOT_ROWS:-}" ]; then awk -v a="$2" -v b="$3" 'NF==2 && $1>=a && $1<=b' "$FBA_BOOT_ROWS"; return 0; fi
  node --experimental-strip-types "$(winpath "$ROOT/scripts/fba-boot-rows.mjs")" "$1" "$2" "$3" 2>"$4"
}

# EXACTLY ONE BOOT between the cut and the end of step 6, and it is POWERON -- counted at the
# Worker, which sees every boot that checks in and cannot cause one. Serial cannot be the
# counter: on 2026-10-08 its reopen WAS the extra boot. Rows land some seconds after the check-in,
# so this waits, then reads until two reads 30 s apart agree (an anchor against a moving target
# measures the movement). ANCHOR: the same query must also find a boot of this device in the hour
# before the cut -- step 1's post-reset boot -- or a query that sees nothing reads as "no boots".
grade_boots() { # [until-epoch] [result-suffix]
  local t0 t1 dev r1 r2 k=0 win n reasons anchor verdict out="$LOG.boots${2:-}" settle
  t0="$(sed -n 's/.* cut=\([0-9]*\) .*/\1/p' "$LOG.step5" 2>/dev/null)"
  t1="${1:-$(sed -n 's/.* at=\([0-9]*\) .*/\1/p' "$LOG.step6" 2>/dev/null)}"
  if [ -z "$t0" ] || [ -z "$t1" ]; then
    printf 'boots verdict=UNGRADED why=step-5-or-6-not-graded\n' > "$out"
    echo "  boots: step 5 or step 6 has not been graded" >&2; return 1
  fi
  dev="$(dev_id)"
  if [ -z "$dev" ]; then printf 'boots verdict=UNTRUSTWORTHY why=no-device-id\n' > "$out"; echo "  boots: no device id" >&2; return 1; fi
  if [ -z "${FBA_BOOT_ROWS:-}" ]; then
    settle=$(( t1 + 60 - $(date +%s) ))
    [ "$settle" -gt 0 ] && { echo "  boots: waiting ${settle}s for the Worker's rows to land..."; sleep "$settle"; }
  fi
  r1="$(boot_rows "$dev" $((t0 - 3600)) "$t1" "$out.err")" || { printf 'boots verdict=UNTRUSTWORTHY why=query-failed\n' > "$out"; echo "  boots: the query failed (see $out.err)" >&2; return 1; }
  while :; do
    [ -z "${FBA_BOOT_ROWS:-}" ] && sleep 30
    r2="$(boot_rows "$dev" $((t0 - 3600)) "$t1" "$out.err")" || { printf 'boots verdict=UNTRUSTWORTHY why=query-failed\n' > "$out"; return 1; }
    [ "$r1" = "$r2" ] && break
    k=$((k + 1)); r1="$r2"
    [ "$k" -ge 4 ] && { printf 'boots verdict=UNSTABLE why=rows-kept-changing\n' > "$out"; echo "  boots: the rows kept changing" >&2; return 1; }
  done
  anchor="$(printf '%s\n' "$r2" | awk -v a="$t0" 'NF==2 && $1 < a' | wc -l | tr -d ' ')"
  win="$(printf '%s\n' "$r2" | awk -v a="$t0" 'NF==2 && $1 >= a')"
  n="$(printf '%s\n' "$win" | grep -c .)"
  reasons="$(printf '%s\n' "$win" | awk 'NF==2 { printf "%s%s", s, $2; s="," }')"
  if [ "$anchor" -eq 0 ]; then verdict=UNTRUSTWORTHY
  elif [ "$n" -eq 1 ] && [ "$reasons" = POWERON ]; then verdict=ONE_POWERON
  elif [ "$n" -eq 0 ]; then verdict=NONE
  else verdict=WRONG; fi
  printf 'boots from=%s to=%s n=%s reasons=%s anchor=%s dev=%s verdict=%s\n' "$t0" "$t1" "$n" "${reasons:--}" "$anchor" "${dev:0:4}" "$verdict" > "$out"
  echo "  boots at the Worker, cut to end of step 6: n=$n (${reasons:-none}); anchor rows before the cut: $anchor -> $verdict"
}

if [ "$MODE" = "gcut" ]; then grade_cut; exit $?; fi
if [ "$MODE" = "goff" ]; then grade_off; exit $?; fi
if [ "$MODE" = "gboots" ]; then grade_boots; exit $?; fi

# Serial again, only AFTER every check above has been graded. A reset by this reopen is recorded
# in the log and cannot change a result.
if [ "$MODE" = "collect" ]; then
  py="$(find_py serial)" || { echo "no working python with pyserial" >&2; exit 3; }
  "$py" "$(winpath "$CAPTURE_PY")" collect "$PORT" "$(winpath "$LOG")" 30
  grep -a "\[collect\] the reopen reset the board" "$LOG" | tail -1
  exit 0
fi

# THE BOOT CHECK, SHOWN CATCHING A REAL EXTRA BOOT (ruling, 2026-10-08). Run after --grade-boots
# has graded the run: this resets the board once more (RTS, esptool's USB hard reset), waits for
# it to answer again and check in, and re-grades the SAME check over a window extended to include
# it. It must go red. Writes <log>.boots.sabotage and never touches <log>.boots.
if [ "$MODE" = "sabotage" ]; then
  py="$(find_py serial)" || { echo "no working python with pyserial" >&2; exit 3; }
  { [ -f "$LOG.step5" ] && [ -f "$LOG.step6" ]; } || { echo "grade steps 5 and 6 first" >&2; exit 2; }
  ip="$(dev_ip)"
  "$py" "$(winpath "$CAPTURE_PY")" reset "$PORT" "$(winpath "$LOG")" || { echo "the reset did not run" >&2; exit 3; }
  echo "  sabotage: reset sent; waiting for the board to answer again..."
  dl=$(( $(date +%s) + 20 )); while [ "$(date +%s)" -lt "$dl" ] && alive "$ip"; do sleep 1; done
  dl=$(( $(date +%s) + 180 )); until alive "$ip" || [ "$(date +%s)" -ge "$dl" ]; do sleep 1; done
  # Its X-Blip-Boot rides the first check-in after the network returns; give that 45 s.
  grade_boots $(( $(date +%s) + 45 )) .sabotage
  case "$(sed -n 's/.* verdict=\([A-Z_]*\).*/\1/p' "$LOG.boots.sabotage")" in
    WRONG) echo "  sabotage CAUGHT: the boot check went red with the extra reset"; exit 0 ;;
    ONE_POWERON) echo "  SABOTAGE NOT CAUGHT: the boot check stayed green through an extra reset"; exit 1 ;;
    *) echo "  sabotage INCONCLUSIVE: $(cat "$LOG.boots.sabotage")"; exit 3 ;;
  esac
fi

step() {
  [ "$MODE" = "guided" ] || return 0
  printf '\n  ---- STEP %s ----\n  %s\n\n  press ENTER when done: ' "$1" "$2"
  read -r _
}

if [ "$MODE" = "probe" ]; then
  probe_claim
  exit $?
fi

if [ "$MODE" = "capture" ]; then
  start_capture
  printf '
  capture running. log: %s
' "$LOG"
  printf '  right after the claim (step 3), BEFORE anyone opens Collection: %s --probe-claim %s\n' "$0" "$LOG"
  printf '  BEFORE the power is pulled (step 5), and leave it running:  %s --grade-cut %s\n' "$0" "$LOG"
  printf '  after the logbook-off save (step 6):                         %s --grade-off %s\n' "$0" "$LOG"
  printf '  then:                                                        %s --grade-boots %s\n' "$0" "$LOG"
  printf '  only after all of those, to collect the log tail:            %s --collect %s %s\n' "$0" "$PORT" "$LOG"
  printf '  finish with: %s --assert-only %s

' "$0" "$LOG"
  exit 0
fi

# A guided run with no terminal would auto-answer every prompt. Refuse instead.
# BOTH conditions, and the second one is the bug this comment exists for: gating
# only on the TTY sent --assert-only straight into the guided path and refused a
# run that needs no terminal at all.
if [ "$MODE" = "guided" ] && [ ! -t 0 ]; then
  echo "REFUSING: guided mode needs a TTY on stdin (every prompt would auto-answer)." >&2
  echo "Drive the parts separately: --capture-only then --assert-only." >&2
  exit 2
fi
[ "$MODE" = "guided" ] && start_capture

step 1 "FACTORY RESET the device. Swipe left three times to Connect, tap [ Reset ], choose Factory Reset, confirm.
     This is the state every new owner starts from, and the state your
     friend's board was handed over in."
step 2 "Join Wi-Fi through the portal, then on the config page SET YOUR
     LOCATION *and TICK THE SPOTTING LOGBOOK*, then save.

     THE LOGBOOK TICK IS NOT OPTIONAL AND IS NOT A CONVENIENCE. On a
     factory-fresh device the 'logbook' NVS key is unset, and
     AircraftManager.cpp reads that as FALSE -- so Logbook::Begin() is
     never called, no [logbook] loaded line is ever printed, and
     RecordClaim() returns at its first gate. Steps 3-6 would all be
     unreachable and assertion 3 could not pass on any genuinely
     factory-fresh board.

     This was missed because flashing does NOT clear NVS: a bench board
     carries its previous logbook=true across a reflash and looks like it
     ships enabled. Only a real factory reset -- or a real new unit --
     shows the shipped default.

     This save is also the one that freezes defaults as explicit values,
     which is why the cfg-rev migration exists."
step 3 "Wait for aircraft, then TAP one to open its card and claim it.
     Note the type code it claims."
[ "$MODE" = "guided" ] && probe_claim
step 4 "Open the CONFIG PAGE -> Collection, WITHIN ABOUT A MINUTE of the claim.
     The claim must be visible. This is the check the whole script exists for:
     before the fix it stayed empty for ten minutes."
# STEP ORDER IS LOad-BEARING, and this pair was originally the other way round.
#
# Turning the logbook off BEFORE the power cut makes the most important
# assertion in this file unobservable: with the logbook disabled,
# AircraftManager never calls Logbook::Begin(), so the post-cut boot prints no
# [logbook] loaded line at all. The survival check then reads the LAST such line
# in the log -- which is the "loaded 0 types (0 claimed)" from the factory reset,
# before anything was ever claimed -- and reports THE CUSTOMER LOST THEIR
# COLLECTION while the collection sits intact in NVS.
#
# A false alarm about data loss is not a harmless failure. It is the exact shape
# this repo keeps getting caught by: the failing observation and the passing one
# are identical, so the check cannot tell you which world you are in. Caught on
# the first real run, 2026-08-23, having never been executed before.
#
# So: power cut FIRST, while the logbook is still on and its load is observable.
# Toggle off AFTER, which is also the more faithful order -- an owner turns
# collecting off having already collected something.
# Step 5 is graded over the network (ruling, 2026-10-08), so the watcher starts BEFORE the
# prompt: it has to see the board go quiet. The serial capture exits at the cut by design.
GCUT_PID=""
if [ "$MODE" = "guided" ]; then grade_cut & GCUT_PID=$!; fi
step 5 "PULL THE POWER. Not a reset -- an actual power cut, which is what a
     customer's plug does. Then power back on and let it boot. The logbook
     must still be ON for this step. The script is watching the board go
     quiet and come back; press ENTER once it has booted."
if [ -n "$GCUT_PID" ]; then printf '  waiting for step 5 to be graded...\n'; wait "$GCUT_PID"; fi
step 6 "NOW toggle the spotting logbook OFF and save. Then look at Collection
     again -- what you already claimed must STILL BE THERE. Disabling means
     stop collecting, never discard."

if [ "$MODE" = "guided" ]; then
  grade_off
  grade_boots
  # GUARDED ON MODE and on THIS port's capture only. The capture normally exited at the cut; this
  # only stops one that never saw a drop.
  if command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -like '*fresh-boot-capture.py*capture*$PORT*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1
  else
    pkill -f "fresh-boot-capture.py capture $PORT" 2>/dev/null
  fi
  # Serial again, only now that every check is graded. A reset here is recorded, not graded.
  py="$(find_py serial)" && "$py" "$(winpath "$CAPTURE_PY")" collect "$PORT" "$(winpath "$LOG")" 30
fi

printf '\n  ================ ASSERTIONS ================\n\n'
pass=0; fail=0
check() { # name, condition-result, hint
  if [ "$2" -eq 0 ]; then printf '  PASS  %s\n' "$1"; pass=$((pass+1))
  else printf '  FAIL  %s\n        %s\n' "$1" "$3"; fail=$((fail+1)); fi
}

grep -aq "\[build\] env=" "$LOG"; check "the build banner was captured" $? \
  "No [build] line. The capture did not attach, or the board never rebooted."

grep -aq "\[reset\] tier=factory" "$LOG"; check "step 1: a FACTORY reset happened" $? \
  "Only a Wi-Fi reset, or none. The first-run path was not exercised."

grep -aq "\[logbook\] loaded 0 types" "$LOG"; check "step 1: the reset emptied the logbook" $? \
  "The post-reset boot did not load an empty book -- the reset did not clear it."

grep -aq "\[claim\] .* claimed" "$LOG"; check "step 3: a claim landed" $? \
  "No [claim] line. Nothing was claimed, so steps 4-6 prove nothing."

# THE FIX ITSELF. A persist must appear AFTER the first claim, and SOON -- before
# the 2026-08-21 fix the first write could not happen until ten minutes of uptime
# had passed, so a factory-fresh unit was invisible to its own Collection page for
# exactly as long as a new owner would be staring at it.
#
# THIS CHECK WAS WRONG UNTIL 2026-09-17 AND FAILED A HEALTHY BOARD. It took the
# first persist in the WHOLE FILE and required it to come after the first claim:
#
#     PERSIST_LINE="$(grep -an "persisted" | head -1 ...)"   # first ANYWHERE
#     [ "$PERSIST_LINE" -gt "$CLAIM_LINE" ]
#
# Any persist before the first claim -- a periodic write while contacts
# accumulate, or a logbook toggled off and on during setup -- fails that forever,
# however well the device behaves. On 326E64 the first persist was at line 297 and
# the first claim at line 318, so the gate went red while the log plainly showed a
# persist 18 s after the claim carrying the claimed type.
#
# The check's NAME was right and its CODE asked a narrower question. That is this
# repo's most repeated defect, and here it sat inside the gate guarding the
# defect the whole procedure exists for -- which is the worst place for it, since
# a step that is red on a good board trains everyone to read past it.
#
# So: the first persist AFTER the claim, and within a time bound. The bound is
# what keeps the original ten-minute failure failing; without it, a persist at
# +10 min still satisfies "after".
LB_WINDOW_S=60

# "HH:MM:SS" -> seconds since midnight.
ts_secs() {
  local t="${1%% *}" h m s
  h="${t%%:*}"; t="${t#*:}"; m="${t%%:*}"; s="${t##*:}"
  case "$h$m$s" in *[!0-9]*) echo ""; return 1 ;; esac
  echo $((10#$h * 3600 + 10#$m * 60 + 10#$s))
}

# STEP 4, REWRITTEN 2026-10-08. The persist-timing check above it was anchored on the claim
# and could not say what the page SHOWED: the page is served from NVS and a fetch's own save
# lands after its response. Two checks replace it: what the FIRST view after the claim held
# (the probe), and whether anyone opened Collection at all (a separate, clearly named failure).
CLAIM_HIT="$(grep -an "\[claim\] .* claimed" "$LOG" | head -1)"
CLAIM_LINE="${CLAIM_HIT%%:*}"
PROBE="$( [ -f "$LOG.probe" ] && cat "$LOG.probe" )"
pv() { printf '%s' "$PROBE" | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
r=1; P_WHY=""
if [ -z "$PROBE" ]; then
  P_WHY="No probe was recorded. Run --probe-claim <log> right after the claim, before anyone opens Collection (guided mode does it after step 3)."
elif [ "$(pv first)" != "yes" ]; then
  P_WHY="The config page was loaded between the claim and the probe, so the probe was not the first fetch. Redo steps 3-4."
elif [ "$(pv verdict)" = "CLAIMED" ]; then
  r=0
elif [ "$(pv verdict)" = "UNCLAIMED" ] || [ "$(pv verdict)" = "ABSENT" ]; then
  P_WHY="The first /logbook.json after the claim of $(pv claim) had it $(pv verdict). The page is served from NVS, and the save a fetch triggers lands after its own response (at most once per 30 s), so a new owner's first view is missing the claim until a refresh. Known issue in v15/v16; v17 saves on the claim."
else
  # Named outcomes only reach the explanation above. An unknown or empty verdict is a broken
  # probe, and must never read as the device failure the run was predicting.
  P_WHY="BLIND: the probe produced no verdict (verdict='$(pv verdict)', http $(pv http)), so there is no evidence either way. The instrument failed, not the device: fix the probe and redo steps 3-4."
fi
check "step 4: the FIRST Collection view after the claim included it" $r "$P_WHY"
r=1
if [ -n "$CLAIM_LINE" ] && tail -n +"$((CLAIM_LINE + 1))" "$LOG" | grep -aq "\[GET\] Handling request to config web server"; then r=0; fi
check "step 4: Collection was opened after the claim" $r "Collection was never opened; the step was not performed."
# Informational: when the claim actually reached NVS. Not a check (see above).
if [ -n "$CLAIM_LINE" ]; then
  PH="$(grep -an "\[logbook\] persisted" "$LOG" | awk -F: -v c="$CLAIM_LINE" '$1 > c' | head -1)"
  [ -n "$PH" ] && printf '  info  first save after the claim: %s\n' "$(printf '%s' "$PH" | cut -d: -f2- | cut -c1-9)"
fi

# STEPS 5 AND 6 AND THE BOOT COUNT, graded without serial (ruling, 2026-10-08). Until that day
# these read serial lines from after the power cut: the post-cut [logbook] loaded line, the
# disable-edge flush line, and a boot count. Twice that day the capture's reopen after the cut
# reset the board it was counting boots on. So: the owner's Collection and config pages over the
# network for steps 5 and 6, and the Worker's boot rows for the count. The serial-only "disabling
# FLUSHED first" check has no network equivalent and is gone; what an owner can see -- the claim
# still on the page after the logbook is off -- is what step 6 now proves.
rv() { [ -f "$1" ] && sed -n "s/.* $2=\([^ ]*\).*/\1/p" "$1"; }
S5V="$(rv "$LOG.step5" verdict)"; S6V="$(rv "$LOG.step6" verdict)"; S6L="$(rv "$LOG.step6" logbook)"
S5C="$(rv "$LOG.step5" claim)"

r=1
case "$S5V" in
  CLAIMED)   r=0; W="" ;;
  "")        W="Step 5 was not graded. Run --grade-cut BEFORE pulling the power (guided mode does it)." ;;
  NOCUT)     W="No power cut was seen: the board never stopped answering ping while --grade-cut watched. Redo step 5." ;;
  NOBACK)    W="The board did not come back on the network after the cut." ;;
  NOCLAIM)   W="No claim in the log, so there was nothing to look for." ;;
  UNCLAIMED|ABSENT) W="After the power cut the owner's Collection page had $S5C $S5V. The customer lost their collection." ;;
  *)         W="BLIND: step 5's page read produced no verdict ($S5V), so there is no evidence either way." ;;
esac
check "step 5: the claim is on the owner's Collection page after the power cut" $r "$W"

r=1
case "$S6L" in
  OFF) r=0; W="" ;;
  ON)  W="The logbook is still ON on the config page; step 6 was not performed." ;;
  "")  W="Step 6 was not graded. Run --grade-off after the logbook-off save." ;;
  *)   W="BLIND: the config page could not be read ($S6L)." ;;
esac
check "step 6: the logbook was turned off (config page)" $r "$W"

r=1
if [ "$S6L" = OFF ] && [ "$S6V" = CLAIMED ]; then r=0; W=""
elif [ "$S6L" != OFF ]; then W="Not graded: the logbook was not off, so 'still there' would describe a book nobody switched off."
elif [ "$S6V" = UNCLAIMED ] || [ "$S6V" = ABSENT ]; then W="With the logbook off the Collection page had it $S6V. Disabling discarded the collection."
else W="BLIND: step 6's page read produced no verdict ($S6V)."; fi
check "step 6: the claim is still on the Collection page with the logbook off" $r "$W"

BV="$(rv "$LOG.boots" verdict)"
r=1
case "$BV" in
  ONE_POWERON)   r=0; W="" ;;
  WRONG)         W="$(rv "$LOG.boots" n) boot(s) at the Worker between the cut and the end of step 6 ($(rv "$LOG.boots" reasons)). Exactly one, POWERON, is the power cut; anything else is an extra reset." ;;
  NONE)          W="BLIND: the network saw the board come back, but the Worker has no boot row for it." ;;
  UNTRUSTWORTHY) W="UNTRUSTWORTHY: the query could not see this device's boots at all ($(rv "$LOG.boots" why)). Nothing was measured." ;;
  "")            W="The boots were not graded. Run --grade-boots after --grade-off." ;;
  *)             W="Not graded: $(cat "$LOG.boots" 2>/dev/null)" ;;
esac
check "boots: exactly ONE boot at the Worker from the cut to the end of step 6, and it is POWERON" $r "$W"
CR="$(grep -a "\[collect\] the reopen reset the board" "$LOG" | tail -1)"
[ -n "$CR" ] && printf '  info  %s (recorded; it changes no result)\n' "${CR#* }"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
printf '  log: %s\n\n' "$LOG"
[ "$fail" -eq 0 ] || exit 1
