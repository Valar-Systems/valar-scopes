#!/usr/bin/env bash
# Blipscope web renderer: the radar firmware compiled to WebAssembly (tools/web-renderer/README.md,
# docs/web-renderer.md).
#
#   tools/web-renderer/build.sh [s3-128|s3-21] [distdir]      default: s3-128 into tools/web-renderer/dist
#
#   SRC_ROOT=<tree>   build the firmware units from another checkout (a release tag's), with this tree's
#                     tools/web-renderer; default: this tree
#   LIBDEPS=<dir>     the firmware's library sources (default <this tree>/.pio/libdeps/blipscope-s3-128,
#                     from: pio pkg install -e blipscope-s3-128)
#   INCREMENTAL=1     reuse an object whose .cpp is older than it. OFF by default: that test cannot see a header
#                     change (a shim, a firmware header), and a stale object links silently -- this is how a
#                     fixed shim once tested as unfixed. A release build never sets it.
#   EXTRA_CXX / EXTRA_LD / EXTRA_EXPORTS   debugging only (sanitizers, probes); a release build sets none
#
# Writes <distdir>/<sku>/blipscope-web-<sku>-v<N>.{js,wasm} (N = the firmware's FW_VERSION), and beside them the
# render module, the font, its licence, the preview page and web-manifest.json. OUTSIDE THE DEVICE BUILD:
# PlatformIO compiles src/ and lib/ only, and this script writes nothing outside its distdir and object cache.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC_ROOT=$(cd "${SRC_ROOT:-$ROOT}" && pwd)
SKU=${1:-s3-128}
case "$SKU" in
  s3-128) VARIANT=BLIPSCOPE_VARIANT_S3_128 ;;
  s3-21)  VARIANT=BLIPSCOPE_VARIANT_S3_21 ;;
  *) echo "unknown SKU '$SKU' (s3-128 or s3-21)"; exit 2 ;;
esac
DIST=${2:-$HERE/dist}
OUT=$DIST/$SKU
LIBDEPS=${LIBDEPS:-$ROOT/.pio/libdeps/blipscope-s3-128}
OBJ=${OBJ:-$HERE/.obj/$SKU-$(basename "$SRC_ROOT")}
# The per-pixel weather layer stays off on the website (docs/wx-device.md, (A)): the check, and its selftest so a
# check that cannot fail is caught here too. Before anything is compiled.
node "$HERE/check_wx_off.mjs" "$HERE"
node "$HERE/check_wx_off.mjs" --selftest
FW=$(sed -n 's/^constexpr int FW_VERSION = \([0-9][0-9]*\);.*/\1/p' "$SRC_ROOT/src/OtaUpdater.h")
[ -n "$FW" ] || { echo "no FW_VERSION in $SRC_ROOT/src/OtaUpdater.h"; exit 3; }
if ! command -v em++ >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null 2>&1
fi
for lib in ArduinoJson/src QRCode/src LovyanGFX/src/lgfx/Fonts; do
  [ -d "$LIBDEPS/$lib" ] || { echo "missing $LIBDEPS/$lib -- run: pio pkg install -e blipscope-s3-128 (or set LIBDEPS)"; exit 3; }
done
mkdir -p "$OUT" "$OBJ"

# The web build has no backend of its own: the cloud base is a placeholder web_http.cpp re-roots onto the page.
DEFS=(-D"$VARIANT" -DBOARD_HAS_PSRAM -DFEATURE_CLOUD_FEED -DWX_LAYER_SHIPS '-DCLOUD_FEED_BASE="https://web.invalid"'
      -DBLIPSCOPE_TLS_PSRAM -DBLIPSCOPE_EXPECT_BIG_NVS '-DBUILD_ENV="web-renderer"' -DBLIPSCOPE_WEB_RENDERER
      -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0
      -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=1)
INCS=(-I"$HERE/shim" -I"$SRC_ROOT/include" -I"$SRC_ROOT/src" -I"$LIBDEPS/ArduinoJson/src" -I"$LIBDEPS/QRCode/src"
      -I"$LIBDEPS/LovyanGFX/src/lgfx/Fonts")
CXXFLAGS=(-std=gnu++17 -O2 ${EXTRA_CXX:-} -fno-exceptions -Wno-unused-function -Wno-unused-variable)

# The firmware's own translation units: unmodified, compiled as they are.
FIRMWARE=(AircraftManager CloudFeed AircraftInfoFields SpecialAircraft IcaoCountry Logbook SevenSegment
          ConfigMigration HeapHealth MqttPublisher OpenSkyAuthTokenHandler TouchWatchdog TlsAllocator
          models/Aircraft anim/Coastlines anim/StateBorders)
WEB=(web_platform web_rtos web_gfx web_http web_config web_ota web_board web_main)
EXPORTS=_main,_web_start,_web_frame,_web_touch,_web_list_ptr,_web_list_len,_web_text_ptr,_web_text_len
EXPORTS=$EXPORTS,_web_screen_size,_web_stats_ptr,_web_brightness,_web_font_ptr,_web_net_stats,_web_config_set
EXPORTS=$EXPORTS,_web_set_api_base,_web_http_probe${EXTRA_EXPORTS:-}

objs=()
build() {   # src obj
  if [ -z "${INCREMENTAL:-}" ] || [ ! -f "$2" ] || [ "$1" -nt "$2" ]; then
    em++ "${CXXFLAGS[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$1" -o "$2"
  fi
  objs+=("$2")
}
for f in "${FIRMWARE[@]}"; do build "$SRC_ROOT/src/$f.cpp" "$OBJ/${f//\//_}.o"; done
emcc -O2 -c "$LIBDEPS/QRCode/src/qrcode.c" -I"$LIBDEPS/QRCode/src" -o "$OBJ/qrcode.o"; objs+=("$OBJ/qrcode.o")
for f in "${WEB[@]}"; do build "$HERE/web/$f.cpp" "$OBJ/$f.o"; done

NAME=blipscope-web-$SKU-v$FW
# DYNAMIC_EXECUTION=0: no eval and no new Function anywhere in the glue (the site's CSP; docs/web-renderer.md, the
# follow-up). It changes no byte of this build, so the artifact cannot show it took: the probe below does.
LINK=(-O2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=33554432
      -sMODULARIZE -sENVIRONMENT=web,node -sDYNAMIC_EXECUTION=0)
em++ "${objs[@]}" "${LINK[@]}" ${EXTRA_LD:-} -o "$OUT/$NAME.js" -sEXPORT_NAME="BlipscopeRadar_${SKU//-/_}" \
  -sEXPORTED_FUNCTIONS="$EXPORTS" -sEXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8,HEAPU32,UTF8ToString,ccall

# The flag's probe, and its control (F4): a program that needs eval, linked with these same flags and again with only
# DYNAMIC_EXECUTION=0 removed. check_site_rules.mjs requires none in the first and exactly one in the second. Not shipped.
CONTROL=()
for f in "${LINK[@]}"; do [ "$f" = -sDYNAMIC_EXECUTION=0 ] || CONTROL+=("$f"); done
[ $(( ${#LINK[@]} - ${#CONTROL[@]} )) -eq 1 ] || { echo "the link flags do not hold -sDYNAMIC_EXECUTION=0 exactly once"; exit 3; }
mkdir -p "$DIST/probe"
em++ "$HERE/probe/dynamic.cpp" "${LINK[@]}" -sEXPORT_NAME=DynamicProbe -o "$DIST/probe/dynamic-probe.js"
em++ "$HERE/probe/dynamic.cpp" "${CONTROL[@]}" -sEXPORT_NAME=DynamicProbe -o "$DIST/probe/dynamic-probe-control.js"

# Beside the model: the render module, the font (one source: proxy/fonts, the leaderboard page's self-hosted
# JetBrains Mono, SIL OFL 1.1) and its licence, the preview page, and the manifest the preview reads.
cp "$HERE/site/render.js" "$DIST/blipscope-web-render-v$FW.js"
cp "$ROOT/proxy/fonts/mono.woff2" "$DIST/blipscope-web-mono-v$FW.woff2"
cp "$ROOT/proxy/fonts/LICENSE-FONTS.txt" "$DIST/blipscope-web-fonts-LICENSE.txt"
cp "$HERE/site/app.js" "$HERE/site/scene.js" "$DIST/"
{
  printf '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
  printf '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n</head>\n<body>\n'
  cat "$HERE/site/page.html"
  printf '</body>\n</html>\n'
} > "$DIST/index.html"
node -e '
  const fs = require("fs"), [dist, sku, fw, name] = process.argv.slice(1);
  const p = dist + "/web-manifest.json";
  let m = {};
  try { m = JSON.parse(fs.readFileSync(p, "utf8")); } catch (e) {}
  if (m.version !== Number(fw)) m = { models: {} };
  m.version = Number(fw);
  m.render = "blipscope-web-render-v" + fw + ".js";
  m.font = "blipscope-web-mono-v" + fw + ".woff2";
  m.models[sku] = { loader: sku + "/" + name + ".js", wasm: sku + "/" + name + ".wasm" };
  fs.writeFileSync(p, JSON.stringify(m, null, 1) + "\n");
' "$DIST" "$SKU" "$FW" "$NAME"
echo "built $SKU v$FW from $(basename "$SRC_ROOT") -> $OUT/$NAME ($(du -h "$OUT/$NAME.wasm" | cut -f1) wasm, $(du -h "$OUT/$NAME.js" | cut -f1) loader)"
