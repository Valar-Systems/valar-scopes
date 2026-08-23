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
esac

PORT=""
if [ "$MODE" = "assert" ]; then
  LOG="${1:-}"
  if [ -z "$LOG" ] || [ ! -f "$LOG" ]; then
    echo "usage: $0 --assert-only <log file>" >&2
    exit 2
  fi
else
  PORT="${1:-}"
  if [ -z "$PORT" ]; then
    echo "usage: $0 [--capture-only] <COM port> | --assert-only <log>" >&2
    exit 2
  fi
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$MODE" != "assert" ]; then
  STAMP="$(date -u +%Y-%m-%dT%H%M%SZ)"
  LOG="$ROOT/bench-logs/fresh-boot-acceptance-$STAMP.log"
  mkdir -p "$ROOT/bench-logs"
fi

if [ "$MODE" != "assert" ]; then
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
    \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' [capture] port dropped (' + \$_.Exception.GetType().Name + ') -- reconnecting')
    if (\$p) { try { \$p.Close() } catch { } ; try { \$p.Dispose() } catch { } }
    Start-Sleep -Milliseconds 700
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

step() {
  [ "$MODE" = "guided" ] || return 0
  printf '\n  ---- STEP %s ----\n  %s\n\n  press ENTER when done: ' "$1" "$2"
  read -r _
}

if [ "$MODE" = "capture" ]; then
  start_capture
  printf '
  capture running. log: %s
' "$LOG"
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

step 1 "FACTORY RESET the device (Stats screen menu, or the config page).
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

# THE FIX ITSELF. A persist must appear AFTER the first claim -- before this
# change the first write could not happen until ten minutes of uptime had passed.
CLAIM_LINE="$(grep -an "\[claim\] .* claimed" "$LOG" | head -1 | cut -d: -f1)"
PERSIST_LINE="$(grep -an "\[logbook\] persisted" "$LOG" | head -1 | cut -d: -f1)"
if [ -n "$CLAIM_LINE" ] && [ -n "$PERSIST_LINE" ] && [ "$PERSIST_LINE" -gt "$CLAIM_LINE" ]; then r=0; else r=1; fi
check "step 4: the book was PERSISTED after the claim" $r \
  "No [logbook] persisted after the first [claim]. The Collection page reads NVS, so it would have shown nothing."

grep -aq "\[logbook\] disabled -- flushing before logging stops" "$LOG"; check "step 5: disabling FLUSHED first" $? \
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
check "step 6: the collection SURVIVED the power cut ($LAST_LOAD)" $r \
  "The post-power-cut boot loaded 0 types or 0 claimed. The customer lost their collection."

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
printf '  log: %s\n\n' "$LOG"
[ "$fail" -eq 0 ] || exit 1
