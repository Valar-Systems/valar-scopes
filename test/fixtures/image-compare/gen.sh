#!/usr/bin/env bash
# Regenerates image-compare's fixture stores from src/ with the firmware's own toolchain and the firmware's code flags.
# The stores are committed; CI only reads them. Run from this directory:  XT=<toolchain bin dir> bash gen.sh
# Each case is two stores, a/ and b/: src/*.o, firmware.elf, firmware.map (-Map) and inputs.json.
set -euo pipefail
: "${XT:?set XT to the xtensa toolchain bin directory}"
CC="$XT/xtensa-esp32s3-elf-gcc"; CXX="$XT/xtensa-esp32s3-elf-g++"; AR="$XT/xtensa-esp32s3-elf-ar"
[ -x "$CC" ] || CC="$CC.exe"; [ -x "$CXX" ] || CXX="$CXX.exe"; [ -x "$AR" ] || AR="$AR.exe"
CFLAGS="-c -Os -mlongcalls -ffunction-sections -fdata-sections"
PY=""   # a python that runs (on Windows, python3 can be the Store's stub), as test/host/run.sh finds one
for cand in python3 python py; do
  c="$(command -v "$cand" 2>/dev/null)" || continue
  if "$c" -c "pass" >/dev/null 2>&1; then PY="$c"; break; fi
done
[ -n "$PY" ] || { echo "gen.sh: no working python" >&2; exit 2; }
rm -rf stores && mkdir stores

obj() {  # obj <store> <source> <canonical name>
  case "$2" in *.cpp) "$CXX" $CFLAGS -fno-exceptions -fno-rtti -o "$1/src/$3.o" "src/$2" ;;
               *)     "$CC" $CFLAGS -o "$1/src/$3.o" "src/$2" ;; esac
}
link() {  # link <store> <entry> <extra ld flags> <objects...>
  local st="$1" entry="$2" extra="$3"; shift 3
  local ld; ld="$(echo "$st" | sed 's#[^/]*#..#g')/fixture.ld"   # relative: a Windows linker can't read an MSYS path
  ( cd "$st" && "$CC" -nostdlib -Wl,--gc-sections -Wl,-Map=firmware.map $extra -T "$ld" \
      -e "$entry" -o firmware.elf "$@" )
  "$PY" - "$st" <<'PY'
import hashlib, json, os, re, sys
st = sys.argv[1]
loads = [m.group(1).strip() for m in (re.match(r"^LOAD (.+)$", l.rstrip("\r\n")) for l in open(os.path.join(st, "firmware.map"))) if m]
inputs = []
for p in loads:
    if p == "linker stubs":
        continue
    with open(os.path.join(st, p), "rb") as f:
        inputs.append({"path": p, "sha256": hashlib.sha256(f.read()).hexdigest(), "copy": p})
json.dump({"inputs": inputs}, open(os.path.join(st, "inputs.json"), "w"), indent=1)
PY
}
store() { mkdir -p "stores/$1/src"; echo "stores/$1"; }

base() {  # base <store> <f source> : main, f, g, ext, h, relax
  obj "$1" main.c main.c; obj "$1" "$2" f.c; obj "$1" g.c g.c; obj "$1" ext.c ext.c; obj "$1" h.c h.c; obj "$1" relax.c relax.c; obj "$1" hi.c hi.c; obj "$1" h2.c h2.c
  # h and h2 go through an archive, as most of a firmware link does: members, and the map's lib(member) spelling
  ( cd "$1" && "$AR" rcsD src/libh.a src/h.c.o src/h2.c.o && rm src/h.c.o src/h2.c.o )
}
LINKS="src/main.c.o src/f.c.o src/g.c.o src/ext.c.o src/relax.c.o src/hi.c.o src/libh.a"

for c in null body grow gc local relax; do
  case $c in body) fb=f_body.c ;; grow) fb=f_grow.c ;; gc) fb=f_gc.c ;; *) fb=f_a.c ;; esac
  fa=f_a.c; [ $c = local ] && { fa=f_local_a.c; fb=f_local_b.c; }
  ea=""; eb=""; [ $c = relax ] && { ea="-Wl,--section-start=.text_hi=0x40070000"; eb="-Wl,--section-start=.text_hi=0x40090000"; }
  a=$(store $c/a); b=$(store $c/b)
  base "$a" "$fa"; base "$b" "$fb"
  link "$a" entry "$ea" $LINKS; link "$b" entry "$eb" $LINKS
done

a=$(store pick/a); b=$(store pick/b)
for s in "$a" "$b"; do obj "$s" main_pick.c main.c; obj "$s" pa.cpp pa.cpp; obj "$s" pb.cpp pb.cpp; done
link "$a" entry "" src/main.c.o src/pa.cpp.o src/pb.cpp.o
link "$b" entry "" src/main.c.o src/pb.cpp.o src/pa.cpp.o

a=$(store strpool/a); b=$(store strpool/b)
obj "$a" main_str.c main.c; obj "$a" tables_a.c tables.c
obj "$b" main_str.c main.c; obj "$b" tables_b.c tables.c
link "$a" entry "" src/main.c.o src/tables.c.o
link "$b" entry "" src/main.c.o src/tables.c.o

a=$(store offset/a); b=$(store offset/b)
obj "$a" main_off.c main.c; obj "$a" off_a.c off.c
obj "$b" main_off.c main.c; obj "$b" off_b.c off.c
link "$a" entry "" src/main.c.o src/off.c.o
link "$b" entry "" src/main.c.o src/off.c.o

a=$(store rodata/a); b=$(store rodata/b)
obj "$a" main_rod.c main.c; obj "$a" rod_a.c rod.c
obj "$b" main_rod.c main.c; obj "$b" rod_b.c rod.c
link "$a" entry "" src/main.c.o src/rod.c.o
link "$b" entry "" src/main.c.o src/rod.c.o

# amendment 1e: a function's exception table is part of the function (-fexceptions for these two cases only)
objx() { "$CXX" $CFLAGS -fexceptions -o "$1/src/$3.o" "src/$2"; }
for c in eh ehtype; do
  a=$(store $c/a); b=$(store $c/b)
  objx "$a" main_eh.cpp main.cpp; objx "$a" ${c}_a.cpp eh.cpp
  objx "$b" main_eh.cpp main.cpp; objx "$b" ${c}_b.cpp eh.cpp
  link "$a" entry "" src/main.cpp.o src/eh.cpp.o
  link "$b" entry "" src/main.cpp.o src/eh.cpp.o
done

for c in t0 t0bad; do
  s=$(store $c); src=WiFiManager.c; [ $c = t0bad ] && src=WiFiManager_bad.c
  obj "$s" "$src" WiFiManager.c; link "$s" about "" src/WiFiManager.c.o
done
echo "stores regenerated"
