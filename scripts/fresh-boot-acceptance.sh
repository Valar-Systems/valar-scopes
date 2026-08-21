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

PORT="${1:-}"
if [ -z "$PORT" ]; then
  echo "usage: $0 <COM port>   e.g. $0 COM119" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date -u +%Y-%m-%dT%H%M%SZ)"
LOG="$ROOT/bench-logs/fresh-boot-acceptance-$STAMP.log"
mkdir -p "$ROOT/bench-logs"

cat <<BANNER

  FRESH-BOOT ACCEPTANCE          port $PORT
  log: $LOG

  Capture starts now and runs until you finish the last step. Do the steps in
  order -- several checks depend on the ORDER, not just the outcome.

BANNER

# DTR/RTS false so attaching does not itself reset the board: step 1 must be YOUR
# factory reset, not one this script caused. A reset we triggered would still
# produce a passing log and prove nothing about the path a customer takes.
PS_CAPTURE="
\$p = New-Object System.IO.Ports.SerialPort $PORT,115200,None,8,one
\$p.ReadTimeout = 4000
\$p.DtrEnable = \$false
\$p.RtsEnable = \$false
\$p.Open()
\$sw = New-Object System.IO.StreamWriter('$(cygpath -w "$LOG" 2>/dev/null || echo "$LOG")', \$true)
\$sw.AutoFlush = \$true
while (\$true) {
  try { \$sw.WriteLine((Get-Date -Format 'HH:mm:ss') + ' ' + \$p.ReadLine()) } catch [TimeoutException] { }
}
"
powershell.exe -NoProfile -Command "Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-Command','$PS_CAPTURE' -WindowStyle Hidden" >/dev/null 2>&1

step() {
  printf '\n  ---- STEP %s ----\n  %s\n\n  press ENTER when done: ' "$1" "$2"
  read -r _
}

step 1 "FACTORY RESET the device (Stats screen menu, or the config page).
     This is the state every new owner starts from, and the state your
     friend's board was handed over in."
step 2 "Join Wi-Fi through the portal, then SET YOUR LOCATION on the config
     page and save. This save is the one that freezes defaults as explicit
     values -- it is why the cfg-rev migration exists."
step 3 "Wait for aircraft, then TAP one to open its card and claim it.
     Note the type code it claims."
step 4 "Open the CONFIG PAGE -> Collection, WITHIN ABOUT A MINUTE of the claim.
     The claim must be visible. This is the check the whole script exists for:
     before the fix it stayed empty for ten minutes."
step 5 "Toggle the spotting logbook OFF and save. Then look at Collection again
     -- what you already claimed must STILL BE THERE. Disabling means stop
     collecting, never discard."
step 6 "PULL THE POWER. Not a reset -- an actual power cut, which is what a
     customer's plug does. Then power back on and let it boot."

printf '\n  capture continuing for 20s to catch the boot...\n'
sleep 20
powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -like '*SerialPort $PORT*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" >/dev/null 2>&1

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
LAST_LOAD="$(grep -a "\[logbook\] loaded" "$LOG" | tail -1)"
LOADED_TYPES="$(printf '%s' "$LAST_LOAD" | sed -n 's/.*loaded \([0-9]*\) types.*/\1/p')"
LOADED_CLAIMED="$(printf '%s' "$LAST_LOAD" | sed -n 's/.*loaded [0-9]* types (\([0-9]*\) claimed.*/\1/p')"
if [ "${LOADED_TYPES:-0}" -gt 0 ] && [ "${LOADED_CLAIMED:-0}" -gt 0 ]; then r=0; else r=1; fi
check "step 6: the collection SURVIVED the power cut ($LAST_LOAD)" $r \
  "The post-power-cut boot loaded 0 types or 0 claimed. The customer lost their collection."

printf '\n  %d passed, %d failed\n' "$pass" "$fail"
printf '  log: %s\n\n' "$LOG"
[ "$fail" -eq 0 ] || exit 1
