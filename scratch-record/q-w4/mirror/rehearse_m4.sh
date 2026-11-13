#!/usr/bin/env bash
# SCRATCH: rehearsal of switch_d.py's m4 stages on the rehearsal copy left by rehearse_d.sh. Predictions: mirror/rehearse_m4.pred.
set -u
cd "$(dirname "$0")/.."
P="python -X utf8 -I mirror/switch_d.py"; RC="mirror/rehearse-d/cache"
step() { echo "--- $1"; shift; "$@"; echo "    exit $?"; }
echo "rehearsal plan before: $(sha256sum "$RC/plan.json" | cut -c1-12); real plan: $(sha256sum ../bm-cache-qw4/plan.json | cut -c1-12)"
step "m4-restore first (must STOP)" $P m4-restore --cache "$RC"
step "m4-strip" $P m4-strip --cache "$RC"
step "m4-strip again (must STOP)" $P m4-strip --cache "$RC"
step "m4-restore" $P m4-restore --cache "$RC"
step "final after" $P final --cache "$RC" --rehearsal-skip-identity
echo "real plan after: $(sha256sum ../bm-cache-qw4/plan.json | cut -c1-12)"
