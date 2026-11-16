#!/usr/bin/env bash
# SCRATCH (map-server): builds s4_render as fb_counts' fbc_off is built (no contraction), in WSL, then proves the binary exists.
# Prints the tree headers' sha256 so the registration can pin the exact device code the pictures come from.
#   MSYS_NO_PATHCONV=1 bash runlog.sh s4-build wsl.exe bash /mnt/c/.../fbcounts/s4_build.sh
cd "$(dirname "$0")" || exit 9
rm -f s4_render2
for h in tree/include/MapLayer.h tree/include/BasemapPackage.h tree/include/WxLayer.h tree/src/weatherscope/WxBasemap.h; do
  echo "header $h $(sha256sum "$h" | cut -c1-12)"
done
echo "source s4_render2.cpp $(sha256sum s4_render2.cpp | cut -c1-12)"
g++ -std=c++11 -Wall -Wextra -O2 -ffp-contract=off -Itree/include -Itree/src/weatherscope s4_render2.cpp -o s4_render2 > s4_build2.log 2>&1; a=$?
echo "build exit=$a log=$(wc -c < s4_build2.log)B"
cat s4_build2.log
if [ -x s4_render2 ]; then echo "s4_render2 $(stat -c %s s4_render2)B $(sha256sum s4_render2 | cut -c1-12)"; else echo "s4_render2 MISSING"; exit 8; fi
exit $a
