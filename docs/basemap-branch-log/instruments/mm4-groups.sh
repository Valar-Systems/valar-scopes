#!/usr/bin/env bash
# SCRATCH: MM4's five groups on staging, as ruled (Q-MK1, ~00:15Z; procedure ~00:28Z). Each group: B (the build);
# R about 10 s after B's response; I with If-None-Match = B's ETag about 20 s after B's response (or right after R if R
# ends later). Each group's B is sent more than 60 s (here 75 s) after the previous group's last request ended.
# Bounded: every request has curl's 60 s cap. Prints each request's line; never a credential.
set -uo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export BASE="https://scopes-staging.valarsystems.com"
req() { env -u BLIP_KEY -u BLIP_DEVICE BLIPSCOPE_TOKEN_FILE="$HOME/.config/blipscope/tokens" bash "$D/mm4-request.sh" "$@" 2>>"$D/mm4-groups.err"; }
until_epoch() { while [ "$(date -u +%s)" -lt "$1" ]; do sleep 1; done; }
endof() { sed -E 's/.* end [0-9:Z]+ \(([0-9]+)\).*/\1/' <<<"$1"; }
etagof() { sed -E 's/.*etag ("[^"]*").*/\1/' <<<"$1"; }
last_end=0
for g in 1 2 3 4 5; do
  [ "$last_end" -gt 0 ] && until_epoch $((last_end + 75))
  B="$(req "G$g-B")"; echo "$B"
  bend="$(endof "$B")"; bet="$(etagof "$B")"
  case "$bet" in \"bm1-*) ;; *) echo "G$g: B gave no ETag: STOP"; exit 3 ;; esac
  until_epoch $((bend + 10)); R="$(req "G$g-R")"; echo "$R"
  [ "$(etagof "$R")" = "$bet" ] && echo "G$g: R's ETag equals B's" || echo "G$g: R's ETag DIFFERS from B's"
  until_epoch $((bend + 20)); I="$(req "G$g-I" "$bet")"; echo "$I"
  echo "G$g: I sent $(( $(sed -E 's/.* start [0-9:Z]+ \(([0-9]+)\).*/\1/' <<<"$I") - bend )) s after B's response"
  last_end="$(endof "$I")"
done
echo "done $(date -u +%H:%M:%SZ)"
