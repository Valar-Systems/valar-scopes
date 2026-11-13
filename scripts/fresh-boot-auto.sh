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
#   ... --handback keep    (run 1) keep a per-field hash of R0, no values, in his config dir
#   ... --handback check   (runs 2, 3) STOP before step 1 unless R0 hashes equal to the kept one
#   ./scripts/fresh-boot-auto.sh --drop-handback    the end of the second slot: remove the kept hash; no board touched
#
# THE OWNER RUNS IT IN HIS OWN TERMINAL (2026-11-13 06:01Z): Git Bash, launched by its full path, never a bare `bash`
# from PowerShell (that can resolve to WSL's). It stops for his Enter at K-wifi and R1, reading the terminal it runs
# in. Everything it prints is copied to bench-logs/fresh-boot-auto-<stamp>.log, which the session grades from.
#
# THE PC ADAPTER IS PINNED: "Wi-Fi", never "Wi-Fi 5" (a virtual copy of the same card; the owner, 06:06Z). The
# preflight refuses unless scripts/fresh-boot-wlan.py reads it listed, disconnected and with both radios on.
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

CAND="HEAD"; BY=""; WINDOW=""; DRY=0; MODE="run"; CONTROLS=0; SABOTAGE=""; HANDBACK=""
while [ $# -gt 0 ]; do
  case "$1" in
    --controls) CONTROLS=1; shift ;;     # rehearsal only: K-guard and K-reset before step 1
    --sabotage) SABOTAGE="${2:-}"; shift 2
                case "$SABOTAGE" in skip-reset|skip-off) ;; *) echo "--sabotage takes skip-reset or skip-off" >&2; exit 2 ;; esac ;;
    --candidate) CAND="${2:-}"; shift 2 ;;
    --by) BY="${2:-}"; shift 2 ;;
    --window) WINDOW="${2:-}"; shift 2 ;;
    --dry-run) DRY=1; shift ;;
    --handback) HANDBACK="${2:-}"; shift 2       # the hand-back check across two bench slots (the reviewer, 11-13 ~06:20Z)
                case "$HANDBACK" in keep|check) ;; *) echo "--handback takes keep or check" >&2; exit 2 ;; esac ;;
    --drop-handback) MODE="drop-handback"; shift ;;
    --selftest) MODE="selftest"; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

say() { printf '%s %s\n' "$(date -u +%H:%M:%SZ)" "$*"; }
die() { say "STOPPED: $*"; exit "${2:-3}"; }

# wait_enter: the owner's Enter, from the terminal the script runs in (stdin first: in his own terminal that is the
# terminal; /dev/tty only when stdin is not one).
wait_enter() {
  if [ -t 0 ]; then read -r _; return $?; fi
  read -r _ < /dev/tty 2>/dev/null
}

# write_r1 <host> <snapshot, as a Windows path> <out.cmd>: R1 as a BATCH FILE he runs by its path. In a batch file
# `%%` is one `%` and %USERPROFILE% expands; the selftest runs one against a closed port and requires "000".
write_r1() {
  printf '@curl -s -o NUL -w "%%%%{http_code}" -H "X-Blipscope: 1" -H "Content-Type: application/x-www-form-urlencoded" --data-binary "@%s" http://%s/save\r\n@echo.\r\n' "$2" "$1" > "$3"
}
winpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else printf '%s' "$1"; fi; }

# wifi_file_ok <file>: exactly one ssid= and one password= line, each with a non-empty value once a trailing CR (and
# only that) is stripped. His file has CRLF ends, so an EMPTY line is "password=\r". Git for Windows' grep strips that
# CR by default and a Linux grep does not, so the CR is stripped here and grep's own handling is off (-U): the same
# answer on both. Reads the file; prints nothing.
wifi_file_ok() {
  [ -f "$1" ] || return 1
  [ "$(sed 's/\r$//' "$1" | grep -U -c '^ssid[=].')" = "1" ] && [ "$(sed 's/\r$//' "$1" | grep -U -c '^password=.')" = "1" ]
}

HANDBACK_FILE="$CFGDIR/e208-handback.hash"   # no values: one salted SHA-256 per field name (fresh-boot-form.py)

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
  # W3: R1's batch file, run by cmd.exe against a closed port: "000" exactly means -w expanded and the data file was
  # read (a missing one makes curl say so on stderr, which must be empty). Windows only: it needs cmd.exe.
  if command -v cmd >/dev/null 2>&1; then
    printf 'x=1\n' > "$T/snap"; write_r1 "127.0.0.1:9" "$(winpath "$T/snap")" "$T/r1.cmd"
    o="$(cmd //c "$(winpath "$T/r1.cmd")" 2>"$T/r1.err" | tr -d '\r\n')"
    ok "W3 R1's batch file runs: printed '$o', stderr $(wc -c < "$T/r1.err") bytes" "$([ "$o" = "000" ] && [ ! -s "$T/r1.err" ] && echo 0 || echo 1)"
  else
    echo "SKIP W3 (no cmd.exe)"
  fi
  # P1: the password-file presence check prints present/absent only, and refuses on a missing line.
  # Placeholder-shaped values (<...>): scripts/check_wifi.py refuses a bare network name after ssid[=] in any tracked
  # file, and a placeholder passes it (docs/wifi-scrub.md). They still carry the SENTINEL the check looks for.
  mkdir -p "$T/cfg"; printf 'ssid=<SENTINEL-NET>\npassword=<SENTINEL-PW>\n' > "$T/cfg/bench-wifi"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD 2>&1)"
  ok "P1 the dry run prints no part of the password file" "$(printf '%s' "$out" | grep -q SENTINEL && echo 1 || echo 0)"
  ok "P1-control: it does print the presence line" "$(printf '%s' "$out" | grep -q 'bench-wifi: present' && echo 0 || echo 1)"
  printf 'ssid=<SENTINEL-NET>\n' > "$T/cfg/bench-wifi"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD 2>&1)"; rc=$?
  ok "P2 a missing password line refuses (exit 2), saying 'absent'" "$([ $rc -eq 2 ] && printf '%s' "$out" | grep -q 'bench-wifi: absent' && echo 0 || echo 1)"
  # P3: his file has CRLF line ends. An EMPTY password line is then "password=\r", which a bare `^password=.` reads as
  # non-empty: the check would be blind in exactly the case it exists for (the reviewer, 2026-11-13 ~06:20Z).
  printf 'ssid=<SENTINEL-NET>\r\npassword=\r\n' > "$T/cfg/bench-wifi"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD 2>&1)"; rc=$?
  ok "P3 CRLF with an EMPTY password line refuses (exit 2), saying 'absent'" "$([ $rc -eq 2 ] && printf '%s' "$out" | grep -q 'bench-wifi: absent' && echo 0 || echo 1)"
  # P4, its control: CRLF with both values present (his file's shape) still reads present.
  printf 'ssid=<SENTINEL-NET>\r\npassword=<SENTINEL-PW>\r\n' > "$T/cfg/bench-wifi"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD 2>&1)"; rc=$?
  ok "P4 CRLF with both values present reads present (exit 0), and prints no part of the file" "$([ $rc -eq 0 ] && printf '%s' "$out" | grep -q 'bench-wifi: present' && ! printf '%s' "$out" | grep -q SENTINEL && echo 0 || echo 1)"
  # P5: wifi_file_ok itself, on any grep: an empty value behind a CR is empty, for either key; full values pass.
  p5=0
  printf 'ssid=<F>\r\npassword=\r\n' > "$T/w1"; wifi_file_ok "$T/w1" && p5=1
  printf 'ssid=\r\npassword=<F>\r\n' > "$T/w2"; wifi_file_ok "$T/w2" && p5=1
  printf 'ssid=<F>\r\npassword=<F>\r\n' > "$T/w3"; wifi_file_ok "$T/w3" || p5=1
  printf 'ssid=<F>\npassword=<F>\n' > "$T/w4"; wifi_file_ok "$T/w4" || p5=1
  ok "P5 wifi_file_ok: CRLF-empty password and CRLF-empty ssid refused; CRLF and LF with values pass" "$p5"
  # HB1-HB4: the hand-back flags' preflight refusals and --drop-handback, on a synthetic config dir.
  hb="$T/cfg/e208-handback.hash"; rm -f "$hb"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD --handback check 2>&1)"; rc=$?
  ok "HB1 --handback check with no kept hash refuses (exit 2)" "$([ $rc -eq 2 ] && printf '%s' "$out" | grep -q 'no kept hash' && echo 0 || echo 1)"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD --handback keep 2>&1)"; rc=$?
  ok "HB2 --handback keep with none kept passes the preflight (the control for HB3)" "$([ $rc -eq 0 ] && printf '%s' "$out" | grep -q 'kept hash is absent' && echo 0 || echo 1)"
  printf 'salt 00\nx\t00\n' > "$hb"; before="$(cat "$hb")"
  out="$(FBA_SELFTEST_NO_WLAN=1 FBA_CFGDIR="$T/cfg" bash "$0" --by selftest --window "selftest" --dry-run --candidate HEAD --handback keep 2>&1)"; rc=$?
  ok "HB3 --handback keep over a kept hash refuses (exit 2) and leaves it as it was" "$([ $rc -eq 2 ] && [ "$(cat "$hb")" = "$before" ] && printf '%s' "$out" | grep -q 'already exists' && echo 0 || echo 1)"
  FBA_CFGDIR="$T/cfg" bash "$0" --drop-handback >/dev/null 2>&1; rc=$?
  ok "HB4 --drop-handback removes it (exit 0, file gone)" "$([ $rc -eq 0 ] && [ ! -e "$hb" ] && echo 0 || echo 1)"
  rm -rf "$T"
  [ $bad -eq 0 ] && echo "SELFTEST PASSED" || echo "SELFTEST FAILED: $bad"
  exit $(( bad ? 1 : 0 ))
fi

if [ "$MODE" = "drop-handback" ]; then
  if [ -f "$HANDBACK_FILE" ]; then rm -f "$HANDBACK_FILE"; fi
  if [ -f "$HANDBACK_FILE" ]; then say "hand-back hash: NOT removed ($(winpath "$HANDBACK_FILE"))"; exit 1; fi
  say "hand-back hash: removed (or was absent); nothing else touched"; exit 0
fi

[ -n "$BY" ] && [ -n "$WINDOW" ] || { echo "usage: $0 --by <session> --window \"<the owner's window words>\" [--candidate <ref>] [--dry-run] | --selftest" >&2; exit 2; }

if [ -z "${FBA_SELFTEST_NO_WLAN:-}" ]; then
  mkdir -p "$ROOT/bench-logs"
  RUNLOG="$ROOT/bench-logs/fresh-boot-auto-$(date -u +%Y%m%dT%H%M%SZ).log"
  exec > >(tee -a "$RUNLOG") 2>&1
  say "run log: $(winpath "$RUNLOG")"
fi

# ------------------------------------------------------------------------------------------------------- preflight
say "PREFLIGHT (nothing on the board is touched yet; window: \"$WINDOW\")"
"${PY[@]}" "$ROOT/scripts/fresh-boot-cadence.py" --candidate "$CAND" || die "the cadence check did not allow an automated run (above)" 2
bash "$ROOT/scripts/fresh-boot-acceptance.sh" --selftest >/dev/null 2>&1 || die "the acceptance's own --selftest failed" 2
"${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" --selftest >/dev/null 2>&1 || die "the form helper's --selftest failed" 2
"${PY[@]}" "$ROOT/scripts/fresh-boot-wlan.py" --selftest >/dev/null 2>&1 || die "the adapter reader's --selftest failed" 2
if [ -n "${FBA_SELFTEST_NO_WLAN:-}" ]; then
  say "adapter check SKIPPED (the driver's own selftest)"
else
  "${PY[@]}" "$ROOT/scripts/fresh-boot-wlan.py" check || die "the PC adapter \"Wi-Fi\" is not free for step 2 (above): absent, connected to something, or a radio off" 2
fi
if wifi_file_ok "$WIFI_FILE"; then
  say "bench-wifi: present"
else
  say "bench-wifi: absent"; die "step 2 needs the owner's file (docs/fresh-boot-automation.md): it falls back to his phone" 2
fi
if [ "$HANDBACK" = "keep" ] && [ -f "$HANDBACK_FILE" ]; then
  die "HAND-BACK: a kept hash already exists; this run would not overwrite it (--drop-handback first, if it is stale)" 2
fi
if [ "$HANDBACK" = "check" ] && [ ! -f "$HANDBACK_FILE" ]; then
  die "HAND-BACK: there is no kept hash to check against (run 1 keeps it with --handback keep)" 2
fi
[ -n "$HANDBACK" ] && say "hand-back: --handback $HANDBACK (the kept hash is $( [ -f "$HANDBACK_FILE" ] && echo present || echo absent ))"
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
if [ "$HANDBACK" = "check" ]; then
  "${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" hash-compare "$HANDBACK_FILE" "$WORK/before.html" \
    || { cleanup_work; die "HAND-BACK: $LABEL's settings are not the ones run 1 snapshotted (the names above). Something changed them between the slots. Nothing was changed by this run" 2; }
  say "HAND-BACK: every field equal to run 1's R0 (by hash; no value read back or printed)"
fi
"${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" body "$WORK/before.html" "$SNAP" || die "R0: the snapshot could not be built; nothing was changed" 2
say "R0: $LABEL's settings snapshotted to $SNAP (the line above names any masked secret: those need re-entering by hand after the run)"
if [ "$HANDBACK" = "keep" ]; then
  "${PY[@]}" "$ROOT/scripts/fresh-boot-form.py" hash-save "$WORK/before.html" "$HANDBACK_FILE" \
    || { cleanup_work; die "HAND-BACK: the hash could not be kept (above); nothing on the board was changed" 2; }
  say "HAND-BACK: run 1's R0 kept as a hash at $(winpath "$HANDBACK_FILE") (names only; removed with --drop-handback at the end of the second slot)"
fi

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
    say "K-wifi FIRST (rehearsal only). In a second window (PowerShell) the owner runs:"
    say "    powershell -ExecutionPolicy Bypass -File \"$(winpath "$ROOT/scripts/fresh-boot-wifi-join.ps1")\" -Control WrongPassword"
    say "The board must STAY in its setup portal: no [WiFi] CONNECTED for 90 s after it prints 'save: HTTP 200'. Press Enter here once it has."
    wait_enter || die "K-wifi: no terminal to wait on"
    if wait_log "$LOG" '\[WiFi\] CONNECTED' "$L1" 90 >/dev/null; then die "K-wifi: the board JOINED on a wrong password" 1; fi
    say "K-wifi: a wrong password left the board in its portal"
  fi
  say "STEP 2 IS THE OWNER'S. In the second window (PowerShell) he runs, on the PC adapter \"Wi-Fi\" (pinned in the script):"
  say "    powershell -ExecutionPolicy Bypass -File \"$(winpath "$ROOT/scripts/fresh-boot-wifi-join.ps1")\""
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
write_r1 "$HOST" '%USERPROFILE%\.config\blipscope\e208-snapshot' "$R1CMD"
say "R1 IS THE OWNER'S: his settings back onto $LABEL. In the second window he runs the file (PowerShell: with & in front):"
say "    & \"$(winpath "$R1CMD")\""
say "Press Enter here once it has printed 200."
wait_enter || die "R1: no terminal to wait on; the snapshot is kept at $SNAP"
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
