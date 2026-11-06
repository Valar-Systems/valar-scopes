#!/usr/bin/env bash
# SCRATCH: A1's staging reading, one phase (docs/basemap-a1.md §6 and §8 4c, registered at f7bf74af).
#   bash a1-phase.sh sr1    KSEA alone: two requests launched together (SR1), the retry after Retry-After, and a
#                           request 50 s after the first (SR3).
#   bash a1-phase.sh rest   the other nine, one after another: the first request; a second 2 s after the first's start
#                           (SR2); the retry after Retry-After; a request 50 s after the first's start (SR3).
# Every request goes through a1-request.sh (the test identity, headers on stdin). Bounded: curl's 60 s cap per request.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export BASE="https://scopes-staging.valarsystems.com"
req() { env -u BLIP_KEY -u BLIP_DEVICE BLIPSCOPE_TOKEN_FILE="$HOME/.config/blipscope/tokens" bash "$D/a1-request.sh" "$@" 2>>"$D/a1-slot.err"; }
ms() { date -u +%s%3N; }
until_ms() { while [ "$(ms)" -lt "$1" ]; do sleep 0.2; done; }
end_of() { sed -E 's/.* end [0-9:.Z]+ \(([0-9]+)\).*/\1/' "$1"; }
ra_of() { local r; r="$(sed -E 's/.*retry-after ([0-9]+|<none>).*/\1/' "$1")"; [ "$r" = "<none>" ] && r=15; echo "$r"; }
W="$D/a1-w"; mkdir -p "$W"
PHASE="${1:-}"
case "$PHASE" in
  sr1) LIST=("KSEA 47.45 -122.31") ;;
  rest) LIST=("KSFO 37.62 -122.38" "KLAX 33.94 -118.41" "KORD 41.98 -87.90" "KJFK 40.64 -73.78" "KATL 33.64 -84.43"
              "KDFW 32.90 -97.04" "KMIA 25.79 -80.29" "KBOS 42.36 -71.01" "KMSP 44.88 -93.22") ;;
  last4) LIST=("KDFW 32.90 -97.04" "KMIA 25.79 -80.29" "KBOS 42.36 -71.01" "KMSP 44.88 -93.22") ;;  # ruled ~04:32Z, after KJFK's stop
  *) echo "usage: $0 sr1|rest|last4"; exit 2 ;;
esac
echo "phase $PHASE starts $(date -u +%H:%M:%S.%3NZ)"
for a in "${LIST[@]}"; do
  set -- $a; ap="$1"; lat="$2"; lon="$3"
  t0="$(ms)"
  if [ "$ap" = "KSEA" ]; then
    req "$ap-pair-a" "$lat" "$lon" > "$W/$ap-1.txt" & p1=$!
    req "$ap-pair-b" "$lat" "$lon" > "$W/$ap-2.txt" & p2=$!
    wait "$p1" "$p2"; cat "$W/$ap-1.txt" "$W/$ap-2.txt"
    e1="$(end_of "$W/$ap-1.txt")"; e2="$(end_of "$W/$ap-2.txt")"; last=$(( e1 > e2 ? e1 : e2 ))
    until_ms $(( last + 1000 * $(ra_of "$W/$ap-1.txt") ))
  else
    req "$ap-first" "$lat" "$lon" > "$W/$ap-1.txt" & p1=$!
    until_ms $(( t0 + 2000 ))
    req "$ap-second" "$lat" "$lon" > "$W/$ap-2.txt" & p2=$!
    wait "$p1"; wait "$p2"; cat "$W/$ap-1.txt" "$W/$ap-2.txt"
    until_ms $(( $(end_of "$W/$ap-1.txt") + 1000 * $(ra_of "$W/$ap-1.txt") ))
  fi
  req "$ap-retry" "$lat" "$lon" | tee "$W/$ap-3.txt"
  until_ms $(( t0 + 50000 ))
  req "$ap-plus50s" "$lat" "$lon" | tee "$W/$ap-4.txt"
done
echo "phase $PHASE ends $(date -u +%H:%M:%S.%3NZ)"
