#!/usr/bin/env bash
# Blipscope web demo: the radar's own draw code, compiled to WebAssembly (tools/wasm-demo/README.md).
#
#   tools/wasm-demo/build.sh [s3-128|s3-21] [outdir]      default: s3-128 into tools/wasm-demo/dist/<sku>
#
# Needs Emscripten on PATH (or EMSDK=~/emsdk) and the firmware's library sources, which PlatformIO fetches:
#   pio pkg install -e blipscope-s3-128        (or set LIBDEPS to any .pio/libdeps/<env> holding them)
#
# OUTSIDE THE DEVICE BUILD: PlatformIO compiles src/ and lib/ only; nothing under tools/ is on any env's
# path, and this script writes nothing outside its outdir and its object cache.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SKU=${1:-s3-128}
case "$SKU" in
  s3-128) VARIANT=BLIPSCOPE_VARIANT_S3_128 ;;
  s3-21)  VARIANT=BLIPSCOPE_VARIANT_S3_21 ;;
  *) echo "unknown SKU '$SKU' (s3-128 or s3-21)"; exit 2 ;;
esac
DIST=${2:-$HERE/dist}
OUT=$DIST/$SKU
LIBDEPS=${LIBDEPS:-$ROOT/.pio/libdeps/blipscope-s3-128}
OBJ=${OBJ:-$HERE/.obj/$SKU}
if ! command -v em++ >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null 2>&1
fi
for lib in ArduinoJson/src QRCode/src LovyanGFX/src/lgfx/Fonts; do
  [ -d "$LIBDEPS/$lib" ] || { echo "missing $LIBDEPS/$lib -- run: pio pkg install -e blipscope-s3-128 (or set LIBDEPS)"; exit 3; }
done
mkdir -p "$OUT" "$OBJ"

DEFS=(-D"$VARIANT" -DBOARD_HAS_PSRAM -DFEATURE_CLOUD_FEED '-DCLOUD_FEED_BASE="https://demo.invalid"'
      -DBLIPSCOPE_TLS_PSRAM -DBLIPSCOPE_EXPECT_BIG_NVS '-DBUILD_ENV="wasm-demo"' -DBLIPSCOPE_WEB_DEMO
      -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0
      -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=1)
INCS=(-I"$HERE/shim" -I"$ROOT/include" -I"$ROOT/src" -I"$LIBDEPS/ArduinoJson/src" -I"$LIBDEPS/QRCode/src"
      -I"$LIBDEPS/LovyanGFX/src/lgfx/Fonts")
CXXFLAGS=(-std=gnu++17 -O2 -fno-exceptions -Wno-unused-function -Wno-unused-variable)

# The firmware's own translation units: unmodified, compiled as they are.
FIRMWARE=(AircraftManager CloudFeed AircraftInfoFields SpecialAircraft IcaoCountry Logbook SevenSegment
          ConfigMigration HeapHealth MqttPublisher OpenSkyAuthTokenHandler TouchWatchdog TlsAllocator
          models/Aircraft anim/Coastlines anim/StateBorders)
WEB=(web_platform web_rtos web_gfx web_http web_config web_scene web_ota web_board web_main)

objs=()
build() {   # src obj
  if [ ! -f "$2" ] || [ "$1" -nt "$2" ] || [ -n "${FORCE:-}" ]; then
    em++ "${CXXFLAGS[@]}" "${DEFS[@]}" "${INCS[@]}" -c "$1" -o "$2"
  fi
  objs+=("$2")
}
for f in "${FIRMWARE[@]}"; do build "$ROOT/src/$f.cpp" "$OBJ/${f//\//_}.o"; done
emcc -O2 -c "$LIBDEPS/QRCode/src/qrcode.c" -I"$LIBDEPS/QRCode/src" -o "$OBJ/qrcode.o"; objs+=("$OBJ/qrcode.o")
for f in "${WEB[@]}"; do build "$HERE/web/$f.cpp" "$OBJ/$f.o"; done

em++ "${objs[@]}" -O2 -o "$OUT/radar.js" \
  -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=33554432 \
  -sMODULARIZE -sEXPORT_NAME="BlipscopeRadar_${SKU//-/_}" -sENVIRONMENT=web,node \
  -sEXPORTED_FUNCTIONS=_main,_demo_frame,_demo_touch,_demo_list_ptr,_demo_list_len,_demo_text_ptr,_demo_text_len,_demo_screen_size,_demo_stats_ptr,_demo_brightness,_demo_font_ptr \
  -sEXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8,UTF8ToString

# The page: site/page.html is a body fragment (it is also what the claude.ai preview publishes); the folder
# gets it wrapped as a whole document.
cp "$HERE/site/render.js" "$HERE/site/app.js" "$DIST/"
{
  printf '<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
  printf '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n</head>\n<body>\n'
  cat "$HERE/site/page.html"
  printf '</body>\n</html>\n'
} > "$DIST/index.html"
echo "built $SKU -> $OUT ($(du -h "$OUT/radar.wasm" | cut -f1) wasm, $(du -h "$OUT/radar.js" | cut -f1) loader)"
