#!/usr/bin/env bash
# fresh-boot-auto.sh -- RELEASING.md step 0, the fresh-boot acceptance, without the owner's hands except where they
# must stay his (docs/fresh-boot-automation.md; the owner's answers 2026-11-12 16:57Z and 2026-11-13 01:17Z).
#
# IT CHANGES WHO ACTS, NEVER WHAT IS GRADED. Every check is still scripts/fresh-boot-acceptance.sh's, driven through its
# split modes (--capture-only, --probe-claim, --grade-cut, --grade-off, --grade-boots, --assert-only), exactly as the
# hands-on run on v19 was graded.
#
# WHO DOES WHAT:
#   this script   preflight; R0 (snapshot the board's settings, read-only); step 1 (POST /factory-reset); 2b (a PUBLIC
#                 location); 4 (the probe); 5 (the hub cut on E2:08's port, logged); 6 (logbook off via the page's own
#                 form); the grades; R2 (the restore, compared field by field)
#   THE OWNER     step 2, ONE command: his PC joins the board's setup network and saves HIS Wi-Fi password, read from his
#                 file. This script never types a password into a form and never changes a Wi-Fi profile on this PC
#                 (the reviewer, 02:03:41Z). Step 3, his one claim tap. R1, ONE command: his settings posted back.
#
# PRIVATE DATA. His password and his settings (his location among them) live in %USERPROFILE%\.config\blipscope, are
# read by the commands that need them, and are never printed: scripts/fresh-boot-form.py writes bodies to files and
# prints counts and NAMES only. The snapshot is removed when R2 reads all-equal; otherwise it is kept for him, and the
# report says where.
#
#   ./scripts/fresh-boot-auto.sh --by <session> --window "<the owner's window words>" [--candidate <ref>] [--dry-run]
#   ./scripts/fresh-boot-auto.sh --selftest
#
# Exit 0 graded and restored; 1 a check or the restore read red; 2 refused before anything was touched (cadence due,
# a preflight failed); 3 stopped mid-run (a timeout, a failed POST) -- the report says what state the board is in.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY=(python -I -X utf8)
BOARD="blipscope-bench"                # E2:08 (CLAUDE.md's bench table; board-port.py resolves it by its serial)
LABEL="E2:08"
HUB_PORT=3                             # the YKUSH port map, confirmed by the owner 2026-11-11 ~18:31Z
YKUSH="${FBA_YKUSH:-/c/Program Files (x86)/YEPKIT LDA/ykushcmd/ykushcmd.exe}"
CUT_S=10
# A PUBLIC airport reference point (Seattle-Tacoma), rounded to 0.01 degree: traffic for the claim tap, and nothing
# about the owner (the reviewer, 02:03:41Z). Stored by the board as 4 dp, so K-loc reads these back.
SEA_LAT="47.45"; SEA_LON="-122.31"; SEA_LAT_STORED="47.4500"; SEA_LON_STORED="-122.3100"
CFGDIR="${FBA_CFGDIR:-$HOME/.config/blipscope}"
WIFI_FILE="$CFGDIR/bench-wifi"
SNAP="$CFGDIR/e208-snapshot"
T_RESET=90; T_WIFI=900; T_CLAIM=1200; T_GRADE=600

CAND="HEAD"; BY=""; WINDOW=""; DRY=0; MODE="run"; CONTROLS=0; SABOTAGE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --controls) CONTROLS=1; shift ;;     # rehearsal only: K-guard and K-reset before step 1
    --sabotage) SABOTAGE="${2:-}"; shift 2
                case "$SABOTAGE" in skip-reset|skip-off) ;; *) echo "--sabotage takes skip-reset or skip-off" >&2; exit 2 ;; esac ;;
    --candidate) CAND="${2:-}"; shift 2 ;;
    --by) BY="${2:-}"; shift 2 ;;
    --window) WINDOW="${2:-}"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    --selftest) MODE="selftest"; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

say() { printf '%s %s\n' "$(date -u +%H:%M:%SZ)" "$*"; }
die() { say "STOPPED: $*"; exit "${2:-3}"; }

# wait_log <log> <regex> <after-line> <timeout-s>: the first matching line number after <after-line>, or empty.
wait_log() {
  local log="$1" re="$2" after="$3" t="$4" end n
  end=$(( $(date -u +%s) + t ))
  while :; do
    n="$(grep -a -n -E "$re" "$log" 2>/dev/null | awk -F: -v a="$after" '$1 > a {print $1; exit}')"
    [ -n "$n" ] && { printf '%s' "$n"; return 0; }
    [ "$(date -u +%s)" -ge "$end" ] && return 1
    sleep 2
  done
}

# get_page <host> <out.html>: the config page, to a file (it carries his settings; never printed). Echoes the status.
get_page() { curl -s -m 20 -o "$2" -w '%{http_code}' "http://$1/"; }
post() { # post <host> <path> <curl data args...>: echoes the status, never the body
  local h="$1" p="$2"; shift 2
  curl -s -m 20 -o /dev/null -w '%{http_code}' -H 'X-Blipscope: 1' "$@" "http://$h$p"
}

# ------------------------------------------------------------------------------------------------------- selftest
if [ "$MODE" = "selftest" ]; then
  T="$(mktemp -d)"; bad=0
  ok() { if [ "$2" = "0" ]; then echo "PASS $1"; else echo "FAIL $1"; bad=$((bad + 1)); fi; }
  # W1: wait_log finds a line written AFTER the wait began, and only after the given line.
  printf 'a\n[WiFi] CONNECTED  IP=192.0.2.10\nb\n' > "$T/log"
  ( sleep 3; printf '[WiFi] CONNECTED  IP=192.0.2.11\n' >> "$T/log" ) &
  n="$(wait_log "$T/log" '\[WiFi\] CONNECTED' 3 15)"; ok "W1 wait_log: a line appended later, past the given line (line $n)" "$([ "$n" = "4" ] && echo 0 || echo 1)"
  # W2: it times out rather than matching an old line.
  n="$(wait_log "$T/log" '\[claim\]' 0 3)"; ok "W2 wait_log: no match -> times out, empty" "$([ -z "$n" ] && echo 0 || echo 1)"
  # P1: the password-file presence check prints present/absent only, and refuses on a missing line.
  mkdir -p "$T/cfg"; printf 'ssid=SENTINEL-NET\npassword=SENTINEL-PW\n' > "$T/cfg/bench-wifi"
  out="$(FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD 2>&1)"
  ok "P1 the dry run prints no part of the password file" "$(printf '%s' "$out" | grep -q SENTINEL && echo 1 || echo 0)"
  ok "P1-control: it does print the presence line" "$(printf '%s' "$out" | grep -q 'bench-wifi: present' && echo 0 || echo 1)"
  printf 'ssid=SENTINEL-NET\n' > "$T/cfg/bench-wifi"
  out="$(FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD 2>&1)"; rc=$?
  ok "P2 a missing password line refuses (exit 2), saying 'absent'" "$([ $rc -eq 2 ] && printf '%s' "$out" | grep -q 'bench-wifi: absent' && echo 0 || echo 1)"
  rm -rf "$T"
  [ $bad -eq 0 ] && echo "SELFTEST PASSED" || echo "SELFTEST FAILED: $bad"
  exit $(( bad ? 1 : 0 ))
fi

[ -n "$BY" ] && [ -n "$WINDOW" ] || { echo "usage: $0 --by <session> --window \"<the owner's window words>\" [--candidate <ref>] [--dry-run] | --selftest" >&2; exit 2; }

# ------------------------------------------------------------------------------------------------------- preflight
say "PREFLIGHT (nothing on the board is touched yet; window: \"$WINDOW\")"
"${PY[@]}" "$ROOT/scripts/fresh-boot-cadence.py" --candidate "$CAND" || die "the cadence check did not allow an automated run (above)" 2
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --selftest >/dev/null 2>&1 || die "the acceptance's own --selftest failed" 2
"${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" --selftest >/dev/null 2>&1 || die "the form helper's --selftest failed" 2
if [ -f "$WIFI_FILE" ] && [ "$(grep -c '^ssid=.' "$WIFI_FILE")" = "1" ] && [ "$(grep -c '^password=.' "$WIFI_FILE")" = "1" ]; then
  say "bench-wifi: present"
else
  say "bench-wifi: absent"; die "step 2 needs the owner's file (docs/fresh-boot-automation.md): it falls back to his phone" 2
fi
if [ $DRY -eq 1 ]; then
  say "DRY RUN: preflight passed. The run would now reserve $LABEL, snapshot its settings (R0), factory-reset it, stop for"
  say "the owner's Wi-Fi command, set SEA, wait for his tap, probe, cut port $HUB_PORT, turn the logbook off, grade, stop for"
  say "his restore command, and compare (R2). Nothing was touched."
  exit 0
fi
[ -x "$YKUSH" ] || die "the YKUSH tool is not at its recorded path" 2
"${PY[@]}" "$ROOT/scripts/board-port.py" --reserve "$BOARD" --by "$BY" --for "fresh-boot automated step 0 ($WINDOW)" >/dev/null || die "could not reserve $LABEL" 2
trap '"${PY[@]}" "$ROOT/scripts/board-port.py" --release "$BOARD" --by "$BY" >/dev/null 2>&1' EXIT
PORT="$("${PY[@]}" "$ROOT/scripts/board-port.py" "$BOARD")" || die "$LABEL is not in the port listing" 2
HOST="$("${PY[@]}" "$ROOT/scripts/board-port.py" "$BOARD" --host)" || die "no mDNS name for $LABEL" 2
HOST="$HOST.local"
WORK="$(mktemp -d "$CFGDIR/fba-XXXXXX")"   # pages carry his settings: they live beside his files, removed at the end
cleanup_work() { rm -rf "$WORK"; }

# ------------------------------------------------------------------------------------------------------- R0
s="$(get_page "$HOST" "$WORK/before.html")"; [ "$s" = "200" ] || die "R0: GET / read $s; nothing was changed" 2
"${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" body "$WORK/before.html" "$SNAP" || die "R0: the snapshot could not be built; nothing was changed" 2
say "R0: $LABEL's settings snapshotted to $SNAP (the line above names any masked secret: those need re-entering by hand after the run)"

# ------------------------------------------------------------------------------------------------------- step 1
cap="$(bash "$ROOT/scripts/fresh-boot-acceptance.sh" --capture-only "$PORT")" || die "the capture did not start"
LOG="$(printf '%s\n' "$cap" | sed -n 's/^  capture running\. log: //p' | head -1)"
[ -f "$LOG" ] || die "the capture's log path was not reported"
say "capture: $LOG"
wait_log "$LOG" '\[capture\] attached' 0 30 >/dev/null || die "the capture never attached"
if [ $CONTROLS -eq 1 ]; then
  # K-guard on /location, NOT /factory-reset: if the guard were broken, this must not be able to reset the board.
  s="$(curl -s -m 20 -o /dev/null -w '%{http_code}' --data-urlencode 'lat=0.5' --data-urlencode 'lon=0.5' "http://$HOST/location")"
  [ "$s" = "403" ] && say "K-guard: a POST without X-Blipscope read 403" || die "K-guard: a POST without X-Blipscope read $s, not 403" 1
  LK="$(wc -l < "$LOG")"
  s="$(post "$HOST" /factory-reset -F confirm=nope)"
  wait_log "$LOG" 'factory reset REFUSED' "$LK" 15 >/dev/null || die "K-reset: no REFUSED line for a POST without confirm=RESET (status $s)" 1
  if wait_log "$LOG" '\[reset\] tier=factory' "$LK" 10 >/dev/null; then die "K-reset: the board RESET without confirm=RESET" 1; fi
  say "K-reset: a POST without confirm=RESET was refused, and nothing was reset"
fi
if [ "$SABOTAGE" = "skip-reset" ]; then
  say "S-noreset (rehearsal sabotage): step 1 SKIPPED. The acceptance's step-1 check must read red; step 2 does not apply."
  L1="$(wc -l < "$LOG")"
else
  s="$(post "$HOST" /factory-reset -F confirm=RESET)"; [ "$s" = "200" ] || die "step 1: POST /factory-reset read $s; the board was NOT reset"
  L1="$(wait_log "$LOG" '\[reset\] tier=factory' 0 $T_RESET)" || die "step 1: no [reset] tier=factory line within ${T_RESET}s"
  say "step 1: factory reset seen (capture line $L1)"
fi

# ------------------------------------------------------------------------------------------------------- step 2
if [ "$SABOTAGE" = "skip-reset" ]; then
  IP="$HOST"   # still on its own network: no join, so no CONNECTED line to read an address from
else
  if [ $CONTROLS -eq 1 ]; then
    say "K-wifi FIRST (rehearsal only): the owner runs the step-2 command with -Control WrongPassword. The board must STAY in"
    say "its setup portal: no [WiFi] CONNECTED for 90 s after his command prints 'save: HTTP 200'. Press Enter once it has."
    read -r _ < /dev/tty || die "K-wifi: no terminal to wait on"
    if wait_log "$LOG" '\[WiFi\] CONNECTED' "$L1" 90 >/dev/null; then die "K-wifi: the board JOINED on a wrong password" 1; fi
    say "K-wifi: a wrong password left the board in its portal"
  fi
  say "STEP 2 IS THE OWNER'S. In his own PowerShell, on the PC adapter that is NOT carrying the LAN, he runs:"
  say "    powershell -ExecutionPolicy Bypass -File \"$ROOT\\scripts\\fresh-boot-wifi-join.ps1\" -Adapter \"<that adapter's name>\""
  say "It joins Blipscope-XXXXXX, saves his network from his file, and removes the temporary profile. Waiting ${T_WIFI}s for [WiFi] CONNECTED."
  L2="$(wait_log "$LOG" '\[WiFi\] CONNECTED' "$L1" $T_WIFI)" || die "step 2: no [WiFi] CONNECTED after the reset within ${T_WIFI}s; the board is reset and in its setup portal"
  IP="$(sed -n "${L2}p" "$LOG" | sed -n 's/.*IP=\([0-9.]*\).*/\1/p')"
  [ -n "$IP" ] || die "step 2: the CONNECTED line carries no address"
  export FBA_DEVICE_IP="$IP"
  say "step 2: joined (capture line $L2)"
fi

# ------------------------------------------------------------------------------------------------------- step 2b
s="$(post "$IP" /location --data-urlencode "lat=$SEA_LAT" --data-urlencode "lon=$SEA_LON")"; [ "$s" = "200" ] || die "step 2b: POST /location read $s"
s="$(get_page "$IP" "$WORK/loc.html")"; [ "$s" = "200" ] || die "step 2b: GET / read $s"
"${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" field-equals "$WORK/loc.html" loc0-lat "$SEA_LAT_STORED" >/dev/null \
  && "${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" field-equals "$WORK/loc.html" loc0-lon "$SEA_LON_STORED" >/dev/null \
  || die "K-loc: the posted location did not read back"
say "step 2b: SEA set, and read back (K-loc)"

# ------------------------------------------------------------------------------------------------------- step 3
LL="$(wc -l < "$LOG")"
say "STEP 3 IS THE OWNER'S: on the glass, tap a blip, open its card, claim it. Waiting ${T_CLAIM}s for [claim] ... claimed."
L3="$(wait_log "$LOG" '\[claim\] [^ ]+ claimed' "$LL" $T_CLAIM)" || die "step 3: no claim within ${T_CLAIM}s"
say "step 3: claimed (capture line $L3)"

# ------------------------------------------------------------------------------------------------------- step 4
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --probe-claim "$LOG" || say "step 4: the probe read red (graded at the end)"
s="$(get_page "$IP" "$WORK/after-claim.html")"; say "step 4: Collection opened after the claim (GET / read $s)"

# ------------------------------------------------------------------------------------------------------- step 5
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --grade-cut "$LOG" > "$WORK/gcut.out" 2>&1 &
GPID=$!
sleep 5
off="$(date -u +%Y-%m-%dT%H:%M:%SZ)"; "$YKUSH" -d "$HUB_PORT" >/dev/null 2>&1 || die "step 5: the hub did not switch port $HUB_PORT off"
sleep $CUT_S
on="$(date -u +%Y-%m-%dT%H:%M:%SZ)"; "$YKUSH" -u "$HUB_PORT" >/dev/null 2>&1 || die "step 5: PORT $HUB_PORT LEFT OFF: the hub did not switch it back on"
say "SWITCH LOG: YKUSH port $HUB_PORT, $LABEL, off $off, on $on, session $BY, window \"$WINDOW\""
wait "$GPID"; cat "$WORK/gcut.out"

# ------------------------------------------------------------------------------------------------------- step 6
if [ "$SABOTAGE" = "skip-off" ]; then
  say "S-nooff (rehearsal sabotage): step 6 SKIPPED. The acceptance's step-6 check must read red."
else
  s="$(get_page "$IP" "$WORK/pre-off.html")"; [ "$s" = "200" ] || die "step 6: GET / read $s"
  "${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" body "$WORK/pre-off.html" "$WORK/off.body" --drop logbook || die "step 6: the form could not be built"
  s="$(post "$IP" /save -H 'Content-Type: application/x-www-form-urlencoded' --data-binary "@$WORK/off.body")"; [ "$s" = "200" ] || die "step 6: POST /save read $s"
fi
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --grade-off "$LOG" || say "step 6: the logbook-off grade read red (graded at the end)"

# ------------------------------------------------------------------------------------------------------- the grades
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --grade-boots "$LOG" || say "the boot count read red (graded at the end)"
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --assert-only "$LOG"; GRADE=$?

# ------------------------------------------------------------------------------------------------------- R1, R2
# The command names the board's address, which is never logged (board-port.py --host: "for a tool to hold, not to
# log"), so it goes into a file beside his snapshot and only the file's path is printed.
R1CMD="$CFGDIR/e208-restore.cmd"
printf 'curl -s -o NUL -w "%%{http_code}" -H "X-Blipscope: 1" -H "Content-Type: application/x-www-form-urlencoded" --data-binary "@%%USERPROFILE%%\\.config\\blipscope\\e208-snapshot" http://%s/save\r\n' "$HOST" > "$R1CMD"
say "R1 IS THE OWNER'S: his settings back onto $LABEL. In his own Command Prompt he runs the one line in:"
say "    $R1CMD"
say "Press Enter here once it has printed 200."
read -r _ < /dev/tty || die "R1: no terminal to wait on; the snapshot is kept at $SNAP"
s="$(get_page "$HOST" "$WORK/after.html")"; [ "$s" = "200" ] || die "R2: GET / read $s; the snapshot is kept at $SNAP"
MASKED="$(grep -o 'masked secret fields: .*' <<<"$("${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" body "$WORK/before.html" "$WORK/scratch.body")" | sed 's/masked secret fields: //; s/none//; s/,//g')"
if "${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" compare "$SNAP" "$WORK/after.html" ${MASKED:+--ignore $MASKED}; then
  rm -f "$SNAP" "$R1CMD"; cleanup_work
  say "R2: restored; every field equal (masked secrets aside, named in R0). The snapshot is removed. The window may close."
else
  cleanup_work
  say "R2: NOT restored: the fields named above differ. The window is NOT closed. The snapshot is kept for the owner at $SNAP."
  exit 1
fi
exit $(( GRADE ? 1 : 0 ))
