#!/usr/bin/env bash
# Confirm that /healthz reports the sha we just deployed.
#
# EXTRACTED FROM deploy.sh SO IT CAN BE PROVEN WITHOUT DEPLOYING. Two bugs lived
# in here undetected because reaching them required a real production deploy:
#
#   1. THE RETRY LOOP HAD NO SLEEP. Thirty back-to-back requests against an
#      endpoint that answers in ~100 ms complete in about five seconds, while the
#      comment above them said isolates drain for a few MINUTES. So it printed
#      "not confirmed yet" on perfectly good deploys, as the normal case -- and an
#      operator who sees that after every successful deploy learns to ignore it,
#      then ignores it on the one deploy where it is true. A control that cries
#      wolf gets muted, which is decoration by a different route.
#
#   2. THE FAILURE MESSAGE COULD NOT TELL UNREACHABLE FROM WRONG. Both printed
#      "last reported commit=<none>" followed by the same reassuring "the upload
#      succeeded, re-check in a minute". CANNOT OBSERVE IS NOT NOTHING IS WRONG.
#
# The CLASSIFICATION is a pure function of (reachable, live sha, wanted sha), kept
# separate from the fetching so the selftest can grade all four outcomes with no
# network at all -- this sandbox cannot bind a listening socket, and a check whose
# failure paths can only be reached in production is a check with unwritten
# failure paths.
#
# Usage:  confirm-deploy.sh <host> <sha>
# Env:    HEALTHZ_ATTEMPTS, HEALTHZ_INTERVAL, HEALTHZ_SCHEME
# Exit:   0 confirmed   1 wrong sha   2 cannot observe   3 unstamped
set -u

# outcome <reached:0|1> <live-sha-or-empty> <wanted-sha> -> prints a token
classify() {
  if [ "$1" -eq 0 ]; then echo CANNOT_OBSERVE; return; fi
  case "$2" in
    "$3")       echo CONFIRMED ;;
    UNSTAMPED)  echo UNSTAMPED ;;
    *)          echo WRONG_SHA ;;
  esac
}

report() { # report <token> <live> <sha> <host> <scheme> <minutes>
  case "$1" in
    CONFIRMED)
      printf '  \033[32mconfirmed\033[0m: /healthz reports commit=%s\n' "$2"
      printf '\nNow run the smoke test:  BLIP_KEY=... ./scripts/smoke-prod.sh\n'; return 0 ;;
    CANNOT_OBSERVE)
      printf '  \033[31mCANNOT OBSERVE\033[0m: /healthz never answered 200 in %s minutes.\n' "$6"
      printf '  This is NOT "probably fine" -- nothing has confirmed what is running.\n'
      printf '    curl -sv %s://%s/healthz\n' "$5" "$4"; return 2 ;;
    UNSTAMPED)
      printf '  \033[31mUNSTAMPED\033[0m: /healthz answered, but the build carries no commit.\n'
      printf '  Something deployed over this without going through deploy.sh; the\n'
      printf '  running code is unidentifiable. Re-run this script to stamp it.\n'; return 3 ;;
    *)
      printf '  \033[33mwrong sha\033[0m: /healthz reports commit=%s, expected %s after %s minutes.\n' "$2" "$3" "$6"
      printf '  Either the isolates are unusually slow, or something deployed after\n'
      printf '  this run.  curl -s %s://%s/healthz\n' "$5" "$4"; return 1 ;;
  esac
}

# Sourced by the selftest: define the functions and stop.
[ "${BASH_SOURCE[0]}" != "$0" ] && return 0

HOST="${1:?host}"; SHA="${2:?sha}"
ATTEMPTS="${HEALTHZ_ATTEMPTS:-30}"
INTERVAL="${HEALTHZ_INTERVAL:-10}"     # 30 x 10s = 5 minutes, per the comment above
SCHEME="${HEALTHZ_SCHEME:-https}"

HZ="$(mktemp)"; trap 'rm -f "$HZ"' EXIT
LIVE=""; REACHED=0
for i in $(seq 1 "$ATTEMPTS"); do
  CODE="$(curl -s -o "$HZ" -w '%{http_code}' --max-time 10 "$SCHEME://$HOST/healthz")"
  if [ "$CODE" = "200" ]; then
    REACHED=1
    LIVE="$(grep -oE '"commit":"[^"]*"' "$HZ" | cut -d'"' -f4)"
    [ "$LIVE" = "$SHA" ] && break
  fi
  printf '  %2d/%d  http=%s commit=%s\n' "$i" "$ATTEMPTS" "$CODE" "${LIVE:-<none>}"
  [ "$i" -lt "$ATTEMPTS" ] && sleep "$INTERVAL"
done

report "$(classify "$REACHED" "$LIVE" "$SHA")" "$LIVE" "$SHA" "$HOST" "$SCHEME" \
       "$(( ATTEMPTS * INTERVAL / 60 ))"
