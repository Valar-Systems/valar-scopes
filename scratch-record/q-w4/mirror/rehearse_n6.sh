#!/usr/bin/env bash
# SCRATCH: red-first rehearsal of switch_d2.py (NEW) against switch_d.py (OLD, 361161bdc565), on COPIES of the cache's plan, for the
# orchestrator's three properties (after 06:56Z 2026-11-14):
#   P1 a mirror file never replaces a primary-fetched file;  P2 a mirror file applies only to planned_not_fetched;
#   P3 anything unplaceable is refused outright.
# Every case has its own copy, mirror/rehearse-n6/<case>/; B2 = today's shape (2 primary-fetched), B0 = the shape OLD was built
# for (none fetched), so OLD's own guards are seen without its blanket refusal masking them. Synthetic mirror files only; the real
# cache is only READ. Predictions: mirror/rehearse_n6.pred, registered before this runs.
#   bash runlog.sh mirror-rehearse-n6 bash mirror/rehearse_n6.sh      (from the scratchpad)
set -u
cd "$(dirname "$0")/.."
NEW="python -X utf8 -I mirror/switch_d2.py"; OLD="python -X utf8 -I mirror/switch_d.py"; H="python -X utf8 -I mirror/rh_n6.py"
R=mirror/rehearse-n6
SIX="38021 39167 44007 56029 17005 19123"; EIGHT="$SIX 27081 54093"
step() { echo "--- $1"; shift; "$@"; echo "    exit $?"; }
real() { echo "real $1: plan $(sha256sum ../bm-cache-qw4/plan.json | cut -c1-12), retry-fetched $(sha256sum ../bm-cache-qw4/retry-fetched.json | cut -c1-12), fetch-log $(sha256sum ../bm-cache-qw4/fetch-log.json | cut -c1-12), kept copies in mirror/ $(ls mirror | grep -cE '^(plan|retry)\.')"; }
real before
rm -rf "$R"; mkdir -p "$R"

echo "=== the guards that are not about placement"
step "N-R8 NEW, a rehearsal flag on the REAL cache (must STOP)" $NEW final --rehearsal-skip-identity
$H base $R/N-R0 B2
node -e "setTimeout(() => {}, 30000)" ingest-basemap.ts &
dummy=$!
sleep 4
step "N-R0 NEW apply WITHOUT the proc skip, a dummy process named ingest-basemap.ts alive (must STOP)" $NEW apply --cache $R/N-R0/cache
wait $dummy; echo "    the dummy process ended by itself"
$H state $R/N-R0

echo "=== today's shape, no forgery: OLD refuses outright; NEW mirrors the 6 and skips the 2"
$H base $R/O0 B2
step "O0 OLD apply (must STOP: a primary file was fetched)" $OLD apply --cache $R/O0/cache --rehearsal-skip-proc
$H state $R/O0
$H base $R/N0 B2
$H p1 $R/N0
step "N0.2 NEW apply" $NEW apply --cache $R/N0/cache --rehearsal-skip-proc
$H p1 $R/N0
step "N0.3 NEW apply again (must STOP)" $NEW apply --cache $R/N0/cache --rehearsal-skip-proc
$H synth $R/N0 $SIX
step "N0.5 NEW check (6 mirror DIFFERS, 2 primary-fetched SAME; exit 1)" $NEW check --cache $R/N0/cache
$H state $R/N0
step "N0.6 NEW rekey without the skip (must STOP: identity)" $NEW rekey --cache $R/N0/cache
$H state $R/N0
step "N0.7 NEW rekey, identity skipped" $NEW rekey --cache $R/N0/cache --rehearsal-skip-identity
$H p1 $R/N0
step "N0.8 NEW rekey again (must STOP)" $NEW rekey --cache $R/N0/cache --rehearsal-skip-identity
step "N0.9 NEW final" $NEW final --cache $R/N0/cache --rehearsal-skip-identity
$H tamper $R/N0 44007
step "N0.10 NEW final, a mirrored copy tampered (1 NOT OK)" $NEW final --cache $R/N0/cache --rehearsal-skip-identity
$H untamper $R/N0 44007
step "N0.10b NEW final, restored" $NEW final --cache $R/N0/cache --rehearsal-skip-identity
$H tamper $R/N0 27081
step "N0.11 NEW final, a primary-fetched file tampered (1 NOT OK)" $NEW final --cache $R/N0/cache --rehearsal-skip-identity
$H untamper $R/N0 27081
step "N0.11b NEW final, restored" $NEW final --cache $R/N0/cache --rehearsal-skip-identity
step "N0.12 NEW m4-restore first (must STOP)" $NEW m4-restore --cache $R/N0/cache
step "N0.13 NEW m4-strip" $NEW m4-strip --cache $R/N0/cache
step "N0.14 NEW m4-strip again (must STOP)" $NEW m4-strip --cache $R/N0/cache
step "N0.15 NEW m4-restore" $NEW m4-restore --cache $R/N0/cache
step "N0.16 NEW final after" $NEW final --cache $R/N0/cache --rehearsal-skip-identity
$H p1 $R/N0
$H state $R/N0

echo "=== P1, forged: a MIRROR record + file for primary-fetched 27081"
$H base $R/N1 B2
step "N1 NEW apply" $NEW apply --cache $R/N1/cache --rehearsal-skip-proc
$H synth $R/N1 $SIX 27081
$H p1 $R/N1; $H state $R/N1
step "N1 NEW check (must STOP: cannot place the 27081 mirror record)" $NEW check --cache $R/N1/cache
step "N1 NEW rekey, identity skipped (must STOP the same, before any write)" $NEW rekey --cache $R/N1/cache --rehearsal-skip-identity
$H p1 $R/N1; $H state $R/N1
$H base $R/O1 B2
$H oldpost $R/O1
$H synth $R/O1 $EIGHT
$H p1 $R/O1; $H state $R/O1
step "O1 OLD check" $OLD check --cache $R/O1/cache
step "O1 OLD rekey, identity skipped" $OLD rekey --cache $R/O1/cache --rehearsal-skip-identity
$H p1 $R/O1; $H state $R/O1

echo "=== P2, forged: a mirror entry for 26127 linearwater, planned and FETCHED"
$H base $R/N2a B2
step "N2a NEW apply" $NEW apply --cache $R/N2a/cache --rehearsal-skip-proc
$H synth $R/N2a $SIX 26127_linearwater
$H state $R/N2a
step "N2a NEW check (must STOP: cannot place the 26127 mirror record)" $NEW check --cache $R/N2a/cache
step "N2a NEW rekey, identity skipped (must STOP the same)" $NEW rekey --cache $R/N2a/cache --rehearsal-skip-identity
$H state $R/N2a
$H base $R/N2b B2
step "N2b NEW apply" $NEW apply --cache $R/N2b/cache --rehearsal-skip-proc
$H planedit $R/N2b mirror 26127_linearwater
$H synth $R/N2b $SIX
$H state $R/N2b
step "N2b NEW check (must STOP: the plan mirrors 26127)" $NEW check --cache $R/N2b/cache
step "N2b NEW rekey, identity skipped (must STOP the same)" $NEW rekey --cache $R/N2b/cache --rehearsal-skip-identity
$H state $R/N2b
$H base $R/O2a B0
step "O2a OLD apply" $OLD apply --cache $R/O2a/cache --rehearsal-skip-proc
$H synth $R/O2a $EIGHT 26127_linearwater
step "O2a OLD check" $OLD check --cache $R/O2a/cache
step "O2a OLD rekey, identity skipped" $OLD rekey --cache $R/O2a/cache --rehearsal-skip-identity
step "O2a OLD final" $OLD final --cache $R/O2a/cache --rehearsal-skip-identity
$H state $R/O2a
$H base $R/O2b B0
step "O2b OLD apply" $OLD apply --cache $R/O2b/cache --rehearsal-skip-proc
$H planedit $R/O2b mirror 26127_linearwater
$H synth $R/O2b $EIGHT
step "O2b OLD check" $OLD check --cache $R/O2b/cache
step "O2b OLD rekey, identity skipped" $OLD rekey --cache $R/O2b/cache --rehearsal-skip-identity
step "O2b OLD final" $OLD final --cache $R/O2b/cache --rehearsal-skip-identity
$H state $R/O2b

echo "=== P3, forged: what cannot be placed in either set"
$H base $R/N3a B2; $H forge-unplanned $R/N3a
step "N3a NEW apply (must STOP: a record for an unplanned url)" $NEW apply --cache $R/N3a/cache --rehearsal-skip-proc
$H state $R/N3a
$H base $R/N3b B2; $H forge-fakeprimary $R/N3b 44007
step "N3b NEW apply (must STOP: a primary record that is not a real fetch)" $NEW apply --cache $R/N3b/cache --rehearsal-skip-proc
$H state $R/N3b
$H base $R/N3c B2
step "N3c NEW apply" $NEW apply --cache $R/N3c/cache --rehearsal-skip-proc
$H planedit $R/N3c unmirror 44007
$H synth $R/N3c $SIX
step "N3c NEW check (must STOP: 44007 in neither set)" $NEW check --cache $R/N3c/cache
step "N3c NEW rekey, identity skipped (must STOP the same)" $NEW rekey --cache $R/N3c/cache --rehearsal-skip-identity
$H state $R/N3c
$H base $R/N3d B2; $H drop-log $R/N3d 26127_linearwater
step "N3d NEW apply (must STOP: planned_not_fetched is 7)" $NEW apply --cache $R/N3d/cache --rehearsal-skip-proc
$H state $R/N3d
$H base $R/O3a B0; $H forge-unplanned $R/O3a
step "O3a OLD apply" $OLD apply --cache $R/O3a/cache --rehearsal-skip-proc
$H state $R/O3a
$H base $R/O3b B0; $H forge-fakeprimary $R/O3b 44007
step "O3b OLD apply" $OLD apply --cache $R/O3b/cache --rehearsal-skip-proc
$H state $R/O3b
$H base $R/O3c B0
step "O3c OLD apply" $OLD apply --cache $R/O3c/cache --rehearsal-skip-proc
$H planedit $R/O3c unmirror 44007
$H synth $R/O3c $EIGHT
step "O3c OLD check" $OLD check --cache $R/O3c/cache
step "O3c OLD rekey, identity skipped" $OLD rekey --cache $R/O3c/cache --rehearsal-skip-identity
$H state $R/O3c
$H base $R/O3d B0; $H drop-log $R/O3d 26127_linearwater
step "O3d OLD apply" $OLD apply --cache $R/O3d/cache --rehearsal-skip-proc
$H state $R/O3d
real after
