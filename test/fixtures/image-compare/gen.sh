#!/usr/bin/env bash
# Regenerates image-compare's fixture stores from src/ with the firmware's own toolchain and the firmware's code flags.
# The stores are committed; CI only reads them. Run from this directory:  XT=<toolchain bin dir> bash gen.sh
# Each case is two stores, a/ and b/: src/*.o, firmware.elf, firmware.map (-Map) and inputs.json.
# ONLY="conv wrap vtab" (amendment 1j) builds only those cases, removing only their stores: every other store is left
# byte-for-byte as committed. ONLY names only cases whose block below is guarded by want(); unset, everything is rebuilt.
set -euo pipefail
ONLY="${ONLY:-}"
want() { [ -z "$ONLY" ] || case " $ONLY " in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
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
if [ -z "$ONLY" ]; then rm -rf stores && mkdir stores; else for c in $ONLY; do rm -rf "stores/$c"; done; fi

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

if [ -z "$ONLY" ]; then   # the cases before amendment 1j: built only by a full run
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

# amendment 1f: fill. B is assembled with -Wa,--no-target-align, which moves only the assembler's alignment fill
for c in fill fillchg; do
  a=$(store $c/a); b=$(store $c/b); bsrc=fp.c; [ $c = fillchg ] && bsrc=fp_chg.c
  obj "$a" main_fp.c main.c; obj "$a" fp.c fp.c
  obj "$b" main_fp.c main.c; "$CC" $CFLAGS -Wa,--no-target-align -o "$b/src/fp.c.o" "src/$bsrc"
  link "$a" entry "" src/main.c.o src/fp.c.o
  link "$b" entry "" src/main.c.o src/fp.c.o
done

# 1f: an instruction's width. swap: the same move, narrow (A) then wide (B). swapop: B a different operation
for c in swap swapop; do
  a=$(store $c/a); b=$(store $c/b); bsrc=swap_b.S; [ $c = swapop ] && bsrc=swap_op.S
  obj "$a" main_sw.c main.c; "$CC" $CFLAGS -o "$a/src/sw.S.o" src/swap_a.S
  obj "$b" main_sw.c main.c; "$CC" $CFLAGS -o "$b/src/sw.S.o" "src/$bsrc"
  link "$a" entry "" src/main.c.o src/sw.S.o
  link "$b" entry "" src/main.c.o src/sw.S.o
done

# amendment 1g: a static initializer's exception table (.text.startup.<name>), C++ with -fexceptions
a=$(store inittry/a); b=$(store inittry/b)
objx "$a" main_si.cpp main.cpp; objx "$a" si_a.cpp si.cpp
objx "$b" main_si.cpp main.cpp; objx "$b" si_b.cpp si.cpp
link "$a" entry "" src/main.cpp.o src/si.cpp.o
link "$b" entry "" src/main.cpp.o src/si.cpp.o

# amendment 1h': unsplit .rodata named by content (assembly: GCC would merge or reorder the constants)
for c in rodshift rodown rodswap; do
  a=$(store $c/a); b=$(store $c/b)
  case $c in rodshift) sa=rs_shift_a.S; sb=rs_shift_b.S ;; rodown) sa=rs_shift_a.S; sb=rs_own_b.S ;;
             rodswap) sa=rs_swap_a.S; sb=rs_swap_b.S ;; esac
  obj "$a" main_rs.c main.c; "$CC" $CFLAGS -o "$a/src/rs.S.o" "src/$sa"
  obj "$b" main_rs.c main.c; "$CC" $CFLAGS -o "$b/src/rs.S.o" "src/$sb"
  link "$a" entry "" src/main.c.o src/rs.S.o
  link "$b" entry "" src/main.c.o src/rs.S.o
done

# amendment 1i: LINK-ONLY's controls. callswap: one call target swapped at the same length (assembly, _call8)
a=$(store callswap/a); b=$(store callswap/b)
obj "$a" main_cs.c main.c; obj "$a" cs_t.c cs_t.c; "$CC" $CFLAGS -o "$a/src/cs.S.o" src/cs_a.S
obj "$b" main_cs.c main.c; obj "$b" cs_t.c cs_t.c; "$CC" $CFLAGS -o "$b/src/cs.S.o" src/cs_b.S
link "$a" entry "" src/main.c.o src/cs.S.o src/cs_t.c.o
link "$b" entry "" src/main.c.o src/cs.S.o src/cs_t.c.o
# rodread: read-only data a function reads, in another object (tbl changes, ktbl does not) and in its own (ltbl)
a=$(store rodread/a); b=$(store rodread/b)
obj "$a" main_rr.c main.c; obj "$a" rd.c rd.c; obj "$a" rr_a.c rr.c
obj "$b" main_rr.c main.c; obj "$b" rd.c rd.c; obj "$b" rr_b.c rr.c
link "$a" entry "" src/main.c.o src/rd.c.o src/rr.c.o
link "$b" entry "" src/main.c.o src/rd.c.o src/rr.c.o
fi

# amendment 1j (section 26). conv: fill's sources, linked --no-relax, so pick's four long calls to ext stay six-byte
# l32r a8 / callx8 a8 sites in both images (the selftest rewrites one into the linker's converted form)
if want conv; then
  a=$(store conv/a); b=$(store conv/b)
  obj "$a" main_fp.c main.c; obj "$a" fp.c fp.c
  obj "$b" main_fp.c main.c; "$CC" $CFLAGS -Wa,--no-target-align -o "$b/src/fp.c.o" src/fp.c
  link "$a" entry "-Wl,--no-relax" src/main.c.o src/fp.c.o
  link "$b" entry "-Wl,--no-relax" src/main.c.o src/fp.c.o
fi
# 1j. wrap: the link's flags are read from src/wrap_ld_flags, the same file the cases pass to --ld-flags. lo_w's call to
# hw goes to __wrap_hw in .text_hi: in call range in A (relaxed), out of it in B (kept)
if want wrap; then
  a=$(store wrap/a); b=$(store wrap/b)
  WRAP="$(cat src/wrap_ld_flags)"
  for s in "$a" "$b"; do obj "$s" main_wr.c main.c; obj "$s" wr.c wr.c; obj "$s" wr_hi.c wr_hi.c; done
  link "$a" entry "$WRAP -Wl,--section-start=.text_hi=0x40070000" src/main.c.o src/wr.c.o src/wr_hi.c.o
  link "$b" entry "$WRAP -Wl,--section-start=.text_hi=0x40090000" src/main.c.o src/wr.c.o src/wr_hi.c.o
fi
# 1j. vtab: the map lists vt.c's later .str1.1 sections at vtab's own address with their pre-merge sizes (asserted)
if want vtab; then
  a=$(store vtab/a); b=$(store vtab/b)
  for s in "$a" "$b"; do obj "$s" main_vt.c main.c; obj "$s" vt.c vt.c; done
  link "$a" entry "" src/main.c.o src/vt.c.o
  link "$b" entry "" src/main.c.o src/vt.c.o
  for s in "$a" "$b"; do
    "$PY" - "$s/firmware.map" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
ents = re.findall(r"^ (\.rodata\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+) src/vt\.c\.o$", text, re.M)
at = {n: (int(a, 16), int(z, 16)) for n, a, z in ents}
v = at[".rodata.vtab"][0]
tie = [n for n, (a, z) in at.items() if n.endswith(".str1.1") and a == v and z > at[".rodata.vtab"][1]]
assert len(tie) >= 2, "vtab: the map does not tie a larger .str1.1 section with vtab (%s)" % sorted(at.items())
print("vtab tie in %s: vtab at 0x%x, %s" % (sys.argv[1], v, ", ".join(sorted(tie))))
PY
  done
fi
# amendment 1k (section 27). lstr: lsr reads .LS0+4, a local label in a merged-string section; lcs reads .LC9+4, a local
# label in a merged constant pool. The relocations must name the LOCAL labels themselves (asserted, from readelf).
if want lstr; then
  a=$(store lstr/a); b=$(store lstr/b)
  for s in "$a" "$b"; do obj "$s" main_ls.c main.c; "$CC" $CFLAGS -o "$s/src/lsr.S.o" src/lsr.S; done
  link "$a" entry "" src/main.c.o src/lsr.S.o
  link "$b" entry "" src/main.c.o src/lsr.S.o
  RE="$XT/xtensa-esp32s3-elf-readelf"; [ -x "$RE" ] || RE="$RE.exe"
  for s in "$a" "$b"; do
    r="$("$RE" -rW "$s/src/lsr.S.o")"
    echo "$r" | grep -q '\.LS0 + 4$' && echo "$r" | grep -q '\.LC9 + 4$' \
      || { echo "lstr: a relocation does not name .LS0 + 4 and .LC9 + 4 (the assembler used a section symbol?)" >&2; echo "$r" >&2; exit 3; }
    echo "lstr reloc in $s: $(echo "$r" | grep -E '\.L(S0|C9) \+ 4$' | awk '{print $NF" "$(NF-2)" "$(NF-1)}' | tr '\n' ';')"
  done
fi
echo "stores regenerated"
