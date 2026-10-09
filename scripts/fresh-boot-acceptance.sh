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
#   fresh-boot-acceptance.sh --assert-only <log>     assert an existing capture
#
# The split is what lets an agent hold the capture and the assertions while a
# human does the physical steps, which is how this is actually run.
MODE="guided"
case "${1:-}" in
  --capture-only) MODE="capture"; shift ;;
  --assert-only)  MODE="assert";  shift ;;
  --probe-claim)  MODE="probe";   shift ;;
  --selftest)     MODE="selftest"; shift ;;
esac

PORT=""
if [ "$MODE" = "selftest" ]; then
  :
elif [ "$MODE" = "assert" ] || [ "$MODE" = "probe" ]; then
  LOG="${1:-}"
  if [ -z "$LOG" ] || [ ! -f "$LOG" ]; then
    echo "usage: $0 --assert-only <log file> | --probe-claim <log file>" >&2
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
  base() { cat <<'L'
10:00:00 [capture] attached to COM18
10:00:01 [build] env=blipscope-s3-128 fw=v17 features=cloud
10:00:02 [reset] tier=factory cleared=wifi,wifi-fast,config,logbook preserved=cloud-key-fac
10:00:05 [build] env=blipscope-s3-128 fw=v17 features=cloud
10:00:05 [boot] reset reason=SW
10:00:10 [WiFi] CONNECTED  IP=192.0.2.10  RSSI=-50 dBm
10:00:12 [logbook] loaded 0 types (0 claimed), 0 airlines (0), 0 countries (0), 0 contacts
10:02:00 [claim] E75L claimed (1/10 types)
10:02:01 [logbook] persisted (1/10 types claimed, 3 airlines, 1 countries, 40 contacts; 900 B blobs, 2300 NVS entries free)
10:02:30 [GET] Handling request to config web server...
10:04:00 [capture] port dropped (IOException) -- waiting for it to come back
10:04:05 [capture] port back; waiting 25 s past the boot window before reopening
10:04:30 [capture] attached to COM18
10:04:30 [build] env=blipscope-s3-128 fw=v17 features=cloud
10:04:30 [boot] reset reason=POWERON
10:04:50 [logbook] loaded 12 types (1 claimed), 5 airlines (1), 2 countries (1), 60 contacts
10:06:00 [logbook] disabled -- flushing before logging stops
L
  }
  probe_ok='probe 10:02:05 claim=E75L claim_line=8 ip=192.0.2.10 http=200 verdict=CLAIMED first=yes'
  mk() { # name, sed-expression-or-empty, probe-line-or-NONE
    base | { if [ -n "$2" ]; then sed "$2"; else cat; fi; } > "$T/$1.log"
    [ "$3" = "NONE" ] || printf '%s\n' "$3" > "$T/$1.log.probe"
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
  mk good "" "$probe_ok"
  mk stale "" "${probe_ok/verdict=CLAIMED/verdict=UNCLAIMED}"
  mk noprobe "" NONE
  mk notfirst "" "${probe_ok/first=yes/first=no}"
  mk neveropened '/\[GET\] Handling request/d' "$probe_ok"
  mk doubleboot '/10:04:30 \[boot\] reset reason=POWERON/a 10:04:30 rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)\n10:04:31 [boot] reset reason=USB' "$probe_ok"
  mk noboot '/10:04:30 \[boot\] reset reason=POWERON/d' "$probe_ok"
  case_ good 0 "" ""
  case_ stale 1 "step 4: the FIRST Collection view after the claim included it" "had it UNCLAIMED"
  case_ noprobe 1 "step 4: the FIRST Collection view after the claim included it" "No probe was recorded"
  case_ notfirst 1 "step 4: the FIRST Collection view after the claim included it" "not the first fetch"
  case_ neveropened 1 "step 4: Collection was opened after the claim" "Collection was never opened; the step was not performed."
  case_ doubleboot 1 "step 5: exactly ONE boot after the power cut" "2 boot(s) after the power cut"
  case_ noboot 1 "step 5: exactly ONE boot after the power cut" "BLIND"
  # An empty verdict is a broken probe, and must not read as the v16 failure (2026-10-08, P1).
  mk noverdict "" "${probe_ok/verdict=CLAIMED/verdict=}"
  case_ noverdict 1 "step 4: the FIRST Collection view after the claim included it" "BLIND: the probe produced no verdict"
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

# DTR/RTS false so attaching does not itself reset the board: step 1 must be YOUR
# factory reset, not one this script caused. A reset we triggered would still
# produce a passing log and prove nothing about the path a customer takes.
# THE CAPTURE MUST SURVIVE A REBOOT, which is the entire point of this procedure.
# The first version opened the port once and caught only [TimeoutException]. On an
# S3 the USB CDC device DISAPPEARS when the board resets, so the read throws an
# IOException instead, the process exits, and the log simply stops. Observed on
# the first real run: the capture died at the exact second step 1's factory reset
# was triggered. Steps 2-6 could never have been recorded, and step 6 IS a power
# cut -- so the check could not reach its own most important assertion.
#
# So: reopen forever, and WRITE A MARKER when the port drops. A silent gap is
# indistinguishable from a quiet board; a marked one is evidence of the reboot.
PS_CAPTURE="
\$sw = New-Object System.IO.StreamWriter('$(cygpath -w "$LOG" 2>/dev/null || echo "$LOG")', \$true)
\$sw.AutoFlush = \$true
while (\$true) {
  \$p = \$null
  try {
    \$p = New-Object System.IO.Ports.SerialPort $PORT,115200,None,8,one
    \$p.ReadTimeout = 4000
    \$p.DtrEnable = \$false
    \$p.RtsEnable = \$false
    \$p.Open()
    \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' [capture] attached to $PORT')
    while (\$true) {
      try { \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' ' + \$p.ReadLine()) } catch [TimeoutException] { }
    }
  } catch {
    \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' [capture] port dropped (' + \$_.Exception.GetType().Name + ') -- waiting for it to come back')
    if (\$p) { try { \$p.Close() } catch { } ; try { \$p.Dispose() } catch { } }
    # NOT DTR/RTS: every open above already holds both false. A REOPEN that lands inside the
    # boot window resets the chip (rst:0x15) whatever the handshake (CLAUDE.md, the capture-rig
    # entry). This reconnected every 700 ms and did exactly that after step 5's power cut on
    # 2026-10-08. So: wait for the port to come back, then 25 s more (the bench capture that
    # rode out W3b's unplug used 25 s with no reset), then reopen. Boot lines are not lost:
    # the board buffers them until a host opens the port.
    #
    # AND WAIT FOR IT TO LEAVE FIRST. The first version of this wait still reset the board on
    # 2026-10-08 (P2): GetPortNames() listed the port at the instant it dropped, so the
    # come-back wait ended at once and the 25 s ran from the power CUT, not the power-on. The
    # board's own log bounds it: the first boot printed a fast join but neither its OK nor its
    # 6 s miss, so it was at most ~10 s old when the reopen reset it. So: wait for the port to
    # be gone (bounded; one that never leaves the list in 15 s is treated as back), then for it
    # to return, and time the 25 s from the RETURN. Both gaps are logged, so the wait is
    # measured rather than assumed.
    \$t0 = Get-Date
    while (([System.IO.Ports.SerialPort]::GetPortNames() -contains '$PORT') -and (((Get-Date) - \$t0).TotalSeconds -lt 15)) { Start-Sleep -Milliseconds 200 }
    if ([System.IO.Ports.SerialPort]::GetPortNames() -contains '$PORT') {
      \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' [capture] port never left the list in 15 s; treating it as back')
    } else {
      \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' [capture] port gone ' + [int]((Get-Date) - \$t0).TotalSeconds + ' s after the drop')
    }
    \$t1 = Get-Date
    while (-not ([System.IO.Ports.SerialPort]::GetPortNames() -contains '$PORT')) { Start-Sleep -Milliseconds 200 }
    \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' [capture] port back after ' + [int]((Get-Date) - \$t1).TotalSeconds + ' s away; waiting 25 s past the boot window before reopening')
    Start-Sleep -Seconds 25
  }
}
"
# LAUNCH VIA A FILE, NOT -Command. The first version inlined $PS_CAPTURE into a
# single-quoted -ArgumentList element -- and $PS_CAPTURE itself contains single
# quotes ('HH:mm:ss', the log path), each of which terminates that element. The
# child process died instantly on a parse error, Start-Process reported success
# because it had launched something, and the only symptom was a log file that
# never appeared. Nothing printed an error anywhere.
CAPTURE_PS1="$(mktemp -t fbacapture-XXXXXX.ps1)"
start_capture() {
  printf '%s\n' "$PS_CAPTURE" > "$CAPTURE_PS1"
  powershell.exe -NoProfile -Command "Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','$(cygpath -w "$CAPTURE_PS1")' -WindowStyle Hidden" >/dev/null 2>&1
  # PROVE IT ATTACHED. A capture that silently failed to start produces an empty
  # log, and an empty log makes every assertion below fail for the wrong reason --
  # which reads as "the device is broken" rather than "the capture is broken".
  local i=0
  while [ $i -lt 15 ]; do
    [ -s "$LOG" ] && return 0
    sleep 1
    i=$((i+1))
  done
  echo "CAPTURE DID NOT START: $LOG is empty after 15s." >&2
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
probe_claim() {
  local hit cl type ip out code body verdict first py c
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
  # An interpreter PROVEN by running it, not found by name. On Windows `python3` can be the
  # Microsoft Store stub, which prints "Python was not found" and exits. The first real probe took
  # it, parsed nothing and wrote an empty verdict -- which step 4 read as the v16 failure it was
  # predicting, so a broken probe and a broken device looked the same.
  py=""
  for c in python3 python py; do
    "$c" -c 'import json' >/dev/null 2>&1 && { py="$c"; break; }
  done
  verdict=NOPARSE
  [ -n "$py" ] && verdict="$(printf '%s' "$body" | "$py" -c "import json,sys
t=sys.argv[1]
try:
    d=json.load(sys.stdin)
except Exception:
    print('NOPARSE'); sys.exit()
e=[x for x in d.get('types',[]) if x.get('code')==t]
print('CLAIMED' if e and e[0].get('claimed') else ('UNCLAIMED' if e else 'ABSENT'))" "$type" 2>/dev/null)"
  # Only three answers are evidence. Anything else is the instrument, and says so.
  case "$verdict" in CLAIMED|UNCLAIMED|ABSENT) ;; *) verdict=NOPARSE ;; esac
  [ "$code" = "200" ] || verdict=NOFETCH
  printf 'probe %s claim=%s claim_line=%s ip=%s http=%s verdict=%s first=%s\n' \
    "$(date +%H:%M:%S)" "$type" "$cl" "$ip" "$code" "$verdict" "$first" > "$(PROBE_FILE)"
  echo "  probe: first /logbook.json after the claim of $type -> $verdict (http $code, first=$first)"
}

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
step 5 "PULL THE POWER. Not a reset -- an actual power cut, which is what a
     customer's plug does. Then power back on and let it boot. The logbook
     must still be ON for this step: its reload is the survival evidence."
step 6 "NOW toggle the spotting logbook OFF and save. Then look at Collection
     again -- what you already claimed must STILL BE THERE. Disabling means
     stop collecting, never discard."

if [ "$MODE" = "guided" ]; then
  printf '\n  capture continuing for 20s to catch the boot...\n'
  sleep 20
fi
# GUARDED ON MODE. With PORT unset (--assert-only) this pattern degrades to
# '*SerialPort *', which matches the capture process for ANY port -- so an
# assert-only run would kill a capture it does not own, including a live one on
# another board.
[ "$MODE" = "guided" ] && powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -like '*SerialPort $PORT*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1

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

grep -aq "\[logbook\] disabled -- flushing before logging stops" "$LOG"; check "step 6: disabling FLUSHED first" $? \
  "The disable edge did not flush. Anything claimed since the last write was stranded in RAM."

# THE ONE THAT MATTERS MOST. After a real power cut the book must come back
# non-empty: that is the difference between a collection and a session.
# Take the load from AFTER the last reboot, not merely the last one in the file.
# Those differ exactly when the post-cut boot printed no load at all -- the case
# above -- and silently reading a pre-claim line is how this reports a loss that
# did not happen.
LAST_BOOT_LINE="$(grep -an "\[build\] env=" "$LOG" | tail -1 | cut -d: -f1)"
if [ -n "$LAST_BOOT_LINE" ]; then
  LAST_LOAD="$(tail -n +"$LAST_BOOT_LINE" "$LOG" | grep -a "\[logbook\] loaded" | tail -1)"
else
  LAST_LOAD=""
fi
if [ -z "$LAST_LOAD" ]; then
  LAST_LOAD="(no [logbook] loaded after the last boot -- was the logbook left OFF across the power cut?)"
fi
LOADED_TYPES="$(printf '%s' "$LAST_LOAD" | sed -n 's/.*loaded \([0-9]*\) types.*/\1/p')"
LOADED_CLAIMED="$(printf '%s' "$LAST_LOAD" | sed -n 's/.*loaded [0-9]* types (\([0-9]*\) claimed.*/\1/p')"
if [ "${LOADED_TYPES:-0}" -gt 0 ] && [ "${LOADED_CLAIMED:-0}" -gt 0 ]; then r=0; else r=1; fi
check "step 5: the collection SURVIVED the power cut ($LAST_LOAD)" $r \
  "The post-power-cut boot loaded 0 types or 0 claimed. The customer lost their collection."

# EXACTLY ONE BOOT AFTER THE POWER CUT, and it is POWERON. The capture's own reconnect reset
# the board a second time on 2026-10-08 (rst:0x15); a second boot is the instrument
# intervening, and zero boots means the capture missed the boot entirely (blind, not a pass).
DROP_LINE="$( [ -n "$CLAIM_LINE" ] && grep -an "\[capture\] port dropped" "$LOG" | awk -F: -v c="$CLAIM_LINE" '$1 > c' | head -1 | cut -d: -f1 )"
r=1; B_WHY="No port drop after the claim: the power cut (step 5) is not in the log."
if [ -n "$DROP_LINE" ]; then
  END_LINE="$(grep -an "\[logbook\] disabled -- flushing" "$LOG" | awk -F: -v d="$DROP_LINE" '$1 > d' | head -1 | cut -d: -f1)"
  WIN="$(sed -n "${DROP_LINE},${END_LINE:-\$}p" "$LOG")"
  NB="$(printf '%s\n' "$WIN" | grep -ac "\[boot\] reset reason=")"
  RR="$(printf '%s\n' "$WIN" | grep -a "\[boot\] reset reason=" | head -1 | sed -n 's/.*reset reason=\([A-Z_0-9]*\).*/\1/p')"
  NX="$(printf '%s\n' "$WIN" | grep -ac "rst:0x15")"
  if [ "$NB" -eq 1 ] && [ "$RR" = "POWERON" ] && [ "$NX" -eq 0 ]; then r=0
  elif [ "$NB" -eq 0 ]; then B_WHY="BLIND: no boot line was captured after the power cut, so this cannot tell one boot from two."
  else B_WHY="$NB boot(s) after the power cut (first: ${RR:-?}; rst:0x15 lines: $NX). A second boot is a reset by the capture, not the power cut."
  fi
fi
check "step 5: exactly ONE boot after the power cut, and it is POWERON" $r "$B_WHY"

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
printf '  log: %s\n\n' "$LOG"
[ "$fail" -eq 0 ] || exit 1
