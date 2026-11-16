#!/usr/bin/env bash
# SCRATCH (map-server): step8_26.pred Amendment 5d.5's PATH LIST (rule (b)) for elig26.mts and the 5d.5 grader: every input, resolved
# as they resolve it (elig26: new URL(p, S), S = the scratchpad; imports from mirror/; the grader: os.path.join(S, ...)). Existence
# only; elig26.json must be ABSENT (elig26 writes it). Exit 0 only with 0 missing and elig26.json absent.
#   bash mirror/p5d5_paths.sh      (from the scratchpad)
S="$(pwd)"; miss=0; have=0
need() { if [ -f "$1" ]; then have=$((have+1)); else echo "MISSING ($2): ${1#$S/}"; miss=$((miss+1)); fi; }
for f in src/basemap/format.ts src/basemap/frame.ts; do need "$S/dry-clone/proxy/$f" "elig26 import"; done
for g in 38021 39167 44007 56029 17005 19123; do need "$S/s4-pkg-after/$g-after.bmpk" "elig26 package"; need "$S/mirror/s4-lines/$g.txt" "elig26 L"; done
pre=0; [ -e "$S/mirror/elig26.json" ] && { echo "EXISTS, must be absent: mirror/elig26.json"; pre=1; }
echo "PATHS 5d.5: $have present, $miss missing; elig26.json present $pre"
[ "$miss" = 0 ] && [ "$pre" = 0 ]
