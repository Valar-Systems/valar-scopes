#!/usr/bin/env bash
# SY (docs/weatherscope-stage3.md A11.4, A12.43): include/DayNight.h is product-neutral by ruling, so it carries no
# product symbol. The forbidden patterns are wx, Weather and wxpalette: case-sensitive, over the whole file, comments
# included. run.sh points this at the real header; test/host/sabotage_daynight.py points it at a planted copy (S-SY),
# which is how the scan is shown able to fail.
#
#     bash test/host/scan_daynight_symbols.sh <file>
#
# Prints each hit. Exit 1 on any hit, 0 on none, 2 if the file cannot be read.
f="$1"
if [ ! -r "$f" ]; then
  echo "FAIL: cannot read $f (the RIG)"
  exit 2
fi
hits=$(grep -nE 'wx|Weather|wxpalette' "$f")
if [ -n "$hits" ]; then
  echo "$hits" | sed "s|^|  $(basename "$f"):|"
  exit 1
fi
exit 0
