#!/usr/bin/env bash
# check-activation-gate.sh -- W4 (docs/device-activation.md, "Watch mode"): what each environment's DRY-RUN says the
# activation gate's mode is. Read from the built artifact's binding list, not from wrangler.toml: a var that the
# toml seems to set and the build does not carry is exactly what this exists to catch.
#
#   bash scripts/check-activation-gate.sh            # production must list "watch", staging "enforce"
#   bash scripts/check-activation-gate.sh enforce    # after Daniel's switch: production must list "enforce"
#
# Exit 0: both as expected. 1: a mismatch, named. 2: a dry run that failed (cannot see).
set -u
cd "$(dirname "$0")/.." || exit 2
want_prod="${1:-watch}"
fail=0
for pair in "production:$want_prod" "staging:enforce"; do
  env="${pair%%:*}"; want="${pair#*:}"
  out="$(mktemp -d)"
  log="$(node scripts/wrangler-run.mjs deploy --dry-run --env "$env" --outdir "$out/bundle" 2>&1)"; rc=$?   # docs/wrangler-run.md
  rm -rf "$out"
  if [ "$rc" -ne 0 ]; then echo "CANNOT SEE: the $env dry run failed (exit $rc)"; exit 2; fi
  got="$(printf '%s\n' "$log" | sed -E 's/\x1b\[[0-9;]*m//g' | grep -oE 'env\.ACTIVATION_GATE \("[^"]*"\)' | head -1)"
  if [ "$got" = "env.ACTIVATION_GATE (\"$want\")" ]; then
    echo "  OK       $env: $got"
  else
    echo "  MISMATCH $env: expected env.ACTIVATION_GATE (\"$want\"), the dry run lists: ${got:-nothing}"
    fail=1
  fi
done
[ "$fail" -eq 0 ] && echo "VERDICT: the gate's mode is as expected in both environments" && exit 0
echo "VERDICT: MISMATCH"; exit 1
