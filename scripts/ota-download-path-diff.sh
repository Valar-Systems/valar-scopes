#!/usr/bin/env bash
# ota-download-path-diff.sh <from-ref> <to-ref>
#
# Is the OTA DOWNLOAD PATH's source identical between two releases? docs/factory-image-plan.md, P4': the bench
# proves the update path with a <from> -> <to> update, which runs <from>'s code. That proves <to>'s code only if the
# path is the same source in both, and this is the check that says whether it is.
#
# The path, item by item:
#   - the functions in src/OtaUpdater.cpp that fetch version.txt, build the URL, wait for the clock, download and
#     apply the image, and record the attempt (compared body by body, so an unrelated change elsewhere in the file --
#     v18's reboot cap -- does not hide or fake a difference);
#   - the whole of include/OtaCerts.h (the pinned roots) and src/HttpRequestManager.{h,cpp} (the version fetch);
#   - the platform pin in platformio.ini, which fixes the framework's HTTPUpdate/Update code.
#
# Exit 0: every item SAME. Exit 1: at least one DIFFERS (each is named). Exit 2: the check cannot see -- a ref that
# does not resolve, or a function it cannot extract (an empty body must never compare equal to another empty body).
#
# The control: `ota-download-path-diff.sh v15 v16` must exit 1 (src/HttpRequestManager.cpp changed between them).
set -u
FROM=${1:?usage: ota-download-path-diff.sh <from-ref> <to-ref>}
TO=${2:?usage: ota-download-path-diff.sh <from-ref> <to-ref>}
for r in "$FROM" "$TO"; do
  git rev-parse -q --verify "$r^{commit}" >/dev/null || { echo "CANNOT SEE: $r is not a commit"; exit 2; }
done

FUNCS="MaybeUpdateFirmware FirmwareUrl WaitForClock NoteOtaAttempt NoteOtaFailed drawProgress drawStatus LogOtaSlot"
FILES="include/OtaCerts.h src/HttpRequestManager.h src/HttpRequestManager.cpp"
differs=0

body() {   # body <ref> <function>: the function from its signature line to the first column-0 closing brace
  git show "$1:src/OtaUpdater.cpp" | tr -d '\r' | sed -n "/^[A-Za-z].* $2(/,/^}/p"
}
h() { sha256sum | cut -c1-12; }

echo "OTA download path, $FROM ($(git rev-parse --short=12 "$FROM^{commit}")) -> $TO ($(git rev-parse --short=12 "$TO^{commit}"))"
for f in $FUNCS; do
  a=$(body "$FROM" "$f"); b=$(body "$TO" "$f")
  na=$(printf '%s\n' "$a" | grep -c .); nb=$(printf '%s\n' "$b" | grep -c .)
  if [ "$na" -lt 3 ] || [ "$nb" -lt 3 ]; then echo "CANNOT SEE: $f (lines $na / $nb)"; exit 2; fi
  ha=$(printf '%s\n' "$a" | h); hb=$(printf '%s\n' "$b" | h)
  if [ "$ha" = "$hb" ]; then echo "  SAME     $f()  $ha  ($nb lines)"; else echo "  DIFFERS  $f()  $ha -> $hb"; differs=1; fi
done
for p in $FILES; do
  ha=$(git show "$FROM:$p" 2>/dev/null | tr -d '\r' | h); hb=$(git show "$TO:$p" 2>/dev/null | tr -d '\r' | h)
  if ! git cat-file -e "$FROM:$p" 2>/dev/null || ! git cat-file -e "$TO:$p" 2>/dev/null; then
    echo "CANNOT SEE: $p is missing at one ref"; exit 2
  fi
  if [ "$ha" = "$hb" ]; then echo "  SAME     $p  $ha"; else echo "  DIFFERS  $p  $ha -> $hb"; differs=1; fi
done
pa=$(git show "$FROM:platformio.ini" | tr -d '\r' | grep -m1 '^platform = '); pb=$(git show "$TO:platformio.ini" | tr -d '\r' | grep -m1 '^platform = ')
if [ -z "$pa" ] || [ -z "$pb" ]; then echo "CANNOT SEE: no platform pin"; exit 2; fi
if [ "$pa" = "$pb" ]; then echo "  SAME     the platform pin: ${pb#platform = }"; else echo "  DIFFERS  the platform pin"; differs=1; fi

# For the record, not for the verdict: what else changed in the OTA files (a change outside the path is allowed).
echo "  (whole-file, for the record:)"
git diff --stat "$FROM" "$TO" -- src/OtaUpdater.cpp src/OtaUpdater.h | sed 's/^/    /'
if [ "$differs" -eq 0 ]; then echo "VERDICT: the download path is the same source"; exit 0; fi
echo "VERDICT: the download path DIFFERS"; exit 1
