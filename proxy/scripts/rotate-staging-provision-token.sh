#!/usr/bin/env bash
# rotate-staging-provision-token.sh (docs/test-identity.md, Q-T1 as ruled and answered on 2026-11-04): a NEW
# PROVISION_TOKEN for STAGING ONLY, so the test identity can be minted there. Nobody held staging's old one.
#
# The value: 32 random bytes as 64 hex, from node:crypto, written by redirection straight into
# %USERPROFILE%\.config\valar-flasher\provision-token-staging. It is set with `wrangler secret put` reading STDIN from
# that file, so it is never an argument and never on screen. Every line of output is captured and scanned for the
# value, with the pattern read from the file. Prints lengths, names and booleans only.
#
#   bash scripts/rotate-staging-provision-token.sh        (from proxy/)
#
# Staging only: --env staging is fixed below, and nothing here names production. Refuses if the file already exists.
set -u
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2
F="$HOME/.config/valar-flasher/provision-token-staging"
[ -e "$F" ] && { echo "rotate: REFUSED -- $F already exists; it is not overwritten"; exit 2; }
mkdir -p "$(dirname "$F")" || exit 2
umask 077
node -e "process.stdout.write(require('node:crypto').randomBytes(32).toString('hex'))" > "$F" || { echo "rotate: REFUSED -- the value could not be generated"; rm -f "$F"; exit 2; }
echo "rotate: wrote $F ($(wc -c < "$F" | tr -d ' ') characters; value not shown)"

OUT="$(mktemp)"; ERR="$(mktemp)"
trap 'rm -f "$OUT" "$ERR"' EXIT
npx wrangler secret put PROVISION_TOKEN --env staging < "$F" > "$OUT" 2> "$ERR"
RC=$?
echo "rotate: wrangler secret put PROVISION_TOKEN --env staging: exit $RC"
sed 's/\x1b\[[0-9;]*m//g' "$OUT" | grep -iE "success|uploaded|secret|error|blipscope" | head -6 | sed 's/^/  wrangler: /'
npx wrangler secret list --env staging > "$OUT.list" 2>> "$ERR"
echo "rotate: wrangler secret list --env staging names PROVISION_TOKEN: $(grep -c '"name": *"PROVISION_TOKEN"\|PROVISION_TOKEN' "$OUT.list" | awk '{print ($1 > 0) ? "yes" : "no"}')"
SEEN=$(cat "$OUT" "$ERR" "$OUT.list" | grep -cF -f "$F")
echo "rotate: the value appears in the captured output: $SEEN times"
rm -f "$OUT.list"
if [ "$RC" -ne 0 ]; then
  echo "rotate: FAILED -- the secret was not set; $F is left in place for a retry"
  # stderr is shown only when the scan above proved it holds no copy of the value
  [ "$SEEN" -eq 0 ] && sed 's/\x1b\[[0-9;]*m//g' "$ERR" | grep -v "unsafe\|WARNING\|Processing\|env.staging\|^\s*$" | head -8 | sed 's/^/  stderr: /'
  exit 1
fi
exit 0
