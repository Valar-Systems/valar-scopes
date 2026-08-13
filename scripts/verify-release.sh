#!/usr/bin/env bash
# verify-release.sh -- run AFTER publishing a GitHub Release, before any flash run.
#
# WHY A SCRIPT AND NOT A CHECKLIST IN A DOC. The failure this guards is measured in
# customer shipments: a unit flashed at a FW_VERSION ahead of the published version.txt
# has a dead OTA path from the moment it is boxed, and the gate only moves forward, so
# every such board needs a USB flash by hand. For 50 units in customers' homes that is a
# recall, not a recovery. A checklist you read at 11pm is exactly the wrong instrument.
#
# Every check here has bitten us or is one line from something that did:
#   - the `-L`. `releases/latest/download/...` is a REDIRECT. Without -L curl returns an
#     empty body and 302, which reads exactly like a broken release rather than a missing
#     flag. Cost a false alarm on 2026-08-13.
#   - the asset COUNT. The shipping env vs the CI matrix has diverged before: the cloud
#     feed lived in an env CI never built, so every released radar binary had it compiled
#     out, and the ini looked entirely deliberate.
#   - the CLOUD BASE grep on the BUILT BINARY. `-U` beats a later `-D` in PlatformIO
#     whatever order the ini shows, so an env can build with no backend URL at all and
#     compile clean. Invisible in the source; one grep on the artifact.
#
#   ./scripts/verify-release.sh v7          # verify a published release
#   ./scripts/verify-release.sh v7 --strict # also fail on WARN (asset count drift)
#
# Needs: gh (authenticated), curl.

set -uo pipefail

TAG="${1:-}"
STRICT=0
[ "${2:-}" = "--strict" ] && STRICT=1
if [ -z "$TAG" ]; then
  echo "usage: $0 <tag> [--strict]     e.g. $0 v7" >&2
  exit 2
fi

REPO="Valar-Systems/valar-scopes"
PILOT_SLUG="s3-128"          # the SKU the pilot ships on
LATEST_URL="https://github.com/$REPO/releases/latest/download/version.txt"

pass=0; fail=0; warn=0
ok()   { printf '  \033[32mPASS\033[0m  %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; fail=$((fail+1)); }
note() { printf '  \033[33mWARN\033[0m  %s\n' "$1"; warn=$((warn+1)); }

printf '\n=== verifying release %s ===\n\n' "$TAG"

# ---- 1. the source of truth: what does the tree think it is? ----------------
SRC_FW="$(grep -oE 'FW_VERSION[[:space:]]*=[[:space:]]*[0-9]+' src/OtaUpdater.h | grep -oE '[0-9]+$')"
if [ -z "$SRC_FW" ]; then
  bad "could not read FW_VERSION from src/OtaUpdater.h -- everything below is unanchored"
  echo; exit 1
fi
ok "tree FW_VERSION = $SRC_FW  (src/OtaUpdater.h)"

# ---- 2. version.txt as the FLEET resolves it --------------------------------
# NOTE THE -L. This is the exact URL OtaUpdater.cpp fetches; resolve it the same way.
LATEST_RAW="$(curl -sL --max-time 30 -w '\n%{http_code}' "$LATEST_URL" 2>/dev/null)"
LATEST_CODE="$(printf '%s' "$LATEST_RAW" | tail -1)"
LATEST_VER="$(printf '%s' "$LATEST_RAW" | sed '$d' | tr -d '[:space:]')"

if [ "$LATEST_CODE" != "200" ]; then
  bad "latest/download/version.txt returned HTTP $LATEST_CODE (body: '${LATEST_VER:-<empty>}')"
  echo "        If this is 302 with an empty body you dropped the -L; that is a client bug, not a release bug."
elif [ -z "$LATEST_VER" ]; then
  bad "latest/download/version.txt served an EMPTY body -- devices would parse 0 and never update"
elif [ "$LATEST_VER" = "$SRC_FW" ]; then
  ok "published version.txt = $LATEST_VER, matches the tree"
else
  bad "published version.txt = $LATEST_VER but the tree is $SRC_FW"
  if [ "$SRC_FW" -gt "$LATEST_VER" ] 2>/dev/null; then
    echo "        *** THIS IS THE RECALL CONDITION. A board flashed from this tree ships at"
    echo "        *** $SRC_FW against a published $LATEST_VER, so it can NEVER update. Do not flash."
  fi
fi

# ---- 3. the pilot SKU's binary actually exists on the tag -------------------
ASSETS="$(gh release view "$TAG" --repo "$REPO" --json assets --jq '.assets[].name' 2>/dev/null)"
if [ -z "$ASSETS" ]; then
  bad "could not list assets for $TAG (is it published? is gh authenticated?)"
else
  if printf '%s\n' "$ASSETS" | grep -qx "firmware-$PILOT_SLUG.bin"; then
    ok "firmware-$PILOT_SLUG.bin present (the pilot SKU)"
  else
    bad "firmware-$PILOT_SLUG.bin MISSING -- the pilot SKU has no image on this release"
  fi
  if printf '%s\n' "$ASSETS" | grep -qx "version.txt"; then
    ok "version.txt attached"
  else
    bad "version.txt MISSING -- no device can discover this release"
  fi

  # ---- 4. asset count against the PREVIOUS release ------------------------
  # Not a fixed number: the matrix grows as SKUs are added. A DROP is the signal --
  # that is the shape "an env CI never built" takes when you look at the output.
  N_NOW="$(printf '%s\n' "$ASSETS" | grep -c '^firmware-.*\.bin$')"
  PREV="$(gh release list --repo "$REPO" --limit 20 --json tagName,isPrerelease \
          --jq '[.[] | select(.isPrerelease == false) | .tagName] | .[1]' 2>/dev/null)"
  if [ -n "$PREV" ]; then
    N_PREV="$(gh release view "$PREV" --repo "$REPO" --json assets \
              --jq '[.assets[].name | select(startswith("firmware-") and endswith(".bin"))] | length' 2>/dev/null)"
    if [ -z "$N_PREV" ]; then
      note "could not count $PREV's assets; this release has $N_NOW"
    elif [ "$N_PREV" -eq 0 ] 2>/dev/null; then
      # A zero baseline makes the comparison PASS whatever this release contains --
      # the check would be reporting success while measuring nothing. Say so instead.
      # (Seen for real: v4 predates the multi-SKU asset naming, so a v6-vs-v4 run
      # "passed" with no information in it at all.)
      note "$PREV has 0 firmware binaries, so the count comparison proves NOTHING here."
      note "this release has $N_NOW -- eyeball it against the CI matrix in firmware.yml by hand"
    elif [ "$N_NOW" -lt "$N_PREV" ] 2>/dev/null; then
      bad "$N_NOW firmware binaries, DOWN from $N_PREV on $PREV -- a SKU stopped building"
      printf '%s\n' "$ASSETS" | sed 's/^/          /'
    else
      ok "$N_NOW firmware binaries (previous release $PREV had $N_PREV)"
    fi
  else
    note "no previous full release to compare against; this release has $N_NOW binaries"
  fi
fi

# ---- 5. READ THE ARTIFACT: does the shipped pilot binary carry the backend? -
# The whole point. A radar image with the cloud feed compiled out boots fine, shows a
# healthy panel, and never contacts the proxy -- and nothing in the source says so.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if gh release download "$TAG" --repo "$REPO" --pattern "firmware-$PILOT_SLUG.bin" \
     --dir "$TMP" >/dev/null 2>&1; then
  BIN="$TMP/firmware-$PILOT_SLUG.bin"
  SZ=$(wc -c < "$BIN")
  ok "downloaded firmware-$PILOT_SLUG.bin ($SZ bytes)"
  # grep the binary itself, not the ini that was supposed to produce it.
  if grep -aq "scopes.valarsystems.com" "$BIN"; then
    ok "cloud base URL present IN THE BINARY (scopes.valarsystems.com)"
  else
    bad "cloud base URL ABSENT from the binary -- this image never contacts the proxy"
    echo "        A -U in platformio.ini beats a later -D whatever order the file shows."
  fi
  # A positive control for the grep itself: if this string is missing too, the search
  # is broken rather than the flag, and the check above proves nothing.
  if grep -aq "Blipscope" "$BIN"; then
    ok "control string found -- the binary grep is working"
  else
    bad "control string 'Blipscope' not found either: the GREP is broken, not the flag."
    echo "        Treat the cloud-URL result above as UNKNOWN, not as a pass or a fail."
  fi
else
  bad "could not download firmware-$PILOT_SLUG.bin -- cannot verify the artifact"
fi

printf '\n=== %d passed, %d failed, %d warnings ===\n' "$pass" "$fail" "$warn"
if [ "$fail" -gt 0 ]; then
  printf '\033[31mDO NOT START A FLASH RUN.\033[0m\n\n'
  exit 1
fi
if [ "$warn" -gt 0 ] && [ "$STRICT" -eq 1 ]; then
  printf '\033[33mwarnings present and --strict set.\033[0m\n\n'
  exit 1
fi
printf '\033[32mRelease %s is safe to flash against.\033[0m\n' "$TAG"
printf 'Still to do by hand: the two Board #1 rehearsals in RELEASING.md (OTA leg + FIRST-RUN leg).\n\n'
