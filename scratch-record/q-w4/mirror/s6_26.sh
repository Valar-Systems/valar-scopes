#!/usr/bin/env bash
# SCRATCH (map-server): step8_26.pred Amendment 5b, S-6 (the refusal, with its plant) and then S-7 (the put count), as ONE driver so
# the restore cannot be skipped. Run from the scratchpad, inside a 5a.1 child, with the egress block in the environment.
#   1. sea-regions.json (83e5940fe68a) is byte-copied to mirror/s6-26/ and the copy proved equal; mirror/s6-26 must not exist before.
#   2. PLANT: the store's sea-regions.json is removed, and its absence proved.
#   3. S-6: `d1 --plan` through runlog (tag s6-refuse26): predicted exit 1, naming the LAKES file as in no region.
#   4. RESTORE from the byte copy, sha256 proved 83e5940fe68a. An EXIT trap restores too, so a failure in 3 cannot leave the plant.
#   5. S-7: `d1 --plan` through runlog (tag s7-plan26) on the restored store: predicted exit 0.
# Exit 0 when 3 exits 1 and 5 exits 0 and the restore is proved; 1 otherwise; 2 refused; 3 INSTRUMENT.
#   bash mirror/s6_26.sh
set -u
SCR="$(pwd)"
C="$(dirname "$SCR")/bm-cache-qw4"
CW="$(cygpath -m "$C")"
SR="$C/dry-run/store/sea-regions.json"
D="$SCR/mirror/s6-26"
BAK="$D/sea-regions.json"
s12() { sha256sum "$1" | cut -c1-12; }
[ -e "$D" ] && { echo "refused: mirror/s6-26 exists"; exit 2; }
[ "$(s12 "$SR")" = 83e5940fe68a ] || { echo "INSTRUMENT: the store's sea-regions.json is not 83e5940fe68a"; exit 3; }
mkdir "$D" && cp -p "$SR" "$BAK" && [ "$(s12 "$BAK")" = 83e5940fe68a ] || { echo "INSTRUMENT: the byte copy is not 83e5940fe68a"; exit 3; }
echo "copy: mirror/s6-26/sea-regions.json $(s12 "$BAK")"
restored=0
restore() {
  [ -e "$SR" ] || cp -p "$BAK" "$SR"
  if [ "$(s12 "$SR")" = 83e5940fe68a ]; then restored=1; echo "restore: sea-regions.json 83e5940fe68a, proved"; else restored=0; echo "restore: NOT PROVED"; fi
}
trap restore EXIT
rm "$SR"
[ -e "$SR" ] && { echo "INSTRUMENT: the plant did not apply"; exit 3; }
echo "plant: the store's sea-regions.json removed; absent: yes"
(cd "$SCR/dry-clone/proxy" && bash ../../runlog.sh s6-refuse26 node --import tsx scripts/ingest-basemap.ts d1 --plan --cache "$CW" > ../../run-logs/s6-refuse26.console 2>&1)
r1=$?
echo "S-6 run exit $r1"
restore
trap - EXIT
[ "$restored" = 1 ] || { echo "INSTRUMENT: the restore is not proved; S-7 does not run"; exit 3; }
(cd "$SCR/dry-clone/proxy" && bash ../../runlog.sh s7-plan26 node --import tsx scripts/ingest-basemap.ts d1 --plan --cache "$CW" > ../../run-logs/s7-plan26.console 2>&1)
r2=$?
echo "S-7 run exit $r2"
[ "$r1" = 1 ] && [ "$r2" = 0 ] && exit 0
exit 1
