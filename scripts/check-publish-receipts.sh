#!/usr/bin/env bash
# =============================================================================
# EVERY SLUG-FUL LEG PUBLISHED, OR version.txt DOES NOT SHIP.
#
#   ./scripts/check-publish-receipts.sh --selftest   # prove it; reads no receipts
#   ./scripts/check-publish-receipts.sh <receipts-dir>
#
# WHY THIS EXISTS. `version` used to carry `needs: build`, and `build` is a
# MATRIX. GitHub hands a dependent job the AGGREGATE of every leg, so a
# slug-LESS harness env -- one that publishes nothing -- could fail and thereby
# skip the job that uploads version.txt. That is exactly what happened to v9 on
# 2026-09-01: animtest-s3-128 failed the adsbdb gate's anchor control, `version`
# was skipped, and the whole fleet's OTA gate returned 404 while ten verified
# binaries sat on the release. A row that publishes nothing must not be able to
# block publication.
#
# THE DIRECTION IS THE WHOLE POINT, AND IT IS ASYMMETRIC.
#   slug-FUL leg fails  -> its receipt is absent -> THIS SCRIPT FAILS -> no
#                          version.txt. An image the launch gate never certified
#                          must never become the version the fleet chases.
#   slug-LESS leg fails -> no receipt was ever expected -> version.txt ships.
#                          The leg still goes red; it just stops being load-bearing.
#
# The discriminator is `slug`, read from the matrix -- not a second list to keep
# in step. An env with a slug publishes firmware-<slug>.bin and must pass the
# gate; an env without one publishes nothing. Add a SKU with a slug and it is
# required here automatically; add a harness env and it automatically is not.
#
# WHY A RECEIPT AND NOT "IS THE ASSET ON THE RELEASE". Because the release
# already holds assets from earlier runs. "firmware-s3-128.bin exists" is also
# true of a stale upload from a run whose gate failed -- precisely the state this
# must refuse. A receipt is written by THIS run's leg only after its gate and its
# upload have both succeeded, so it cannot be satisfied by history.
#
# Floored, anchored and selftested before its output is used, for the same reason
# check-adsbdb-out.sh is: "everything is missing" and "nothing was expected" are
# the two shapes a broken parser takes, and one of them reads as success.
# =============================================================================
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1

MATRIX=".github/workflows/firmware.yml"

# A FLOOR, not a transcribed list. A parser returning zero slugs would make this
# check vacuously pass -- the same failure mode that let the square-photo probe
# report an empty library as healthy.
MIN_PUBLISHED_SLUGS=8

# The anchor: the default SKU's slug. A parse without it is not a parse of this
# matrix, however many rows came back.
ANCHOR_SLUG="s3-128"

# Rows that PUBLISH: `- { env: NAME, slug: SLUG }`. Commented rows are excluded
# (the leading `-` is anchored). Rows with no `slug:` are excluded BY DESIGN --
# that exclusion is the discriminator this whole script turns on.
parse_matrix_slugs() {
  sed -n 's/^[[:space:]]*-[[:space:]]*{[[:space:]]*env:[^,}]*,[[:space:]]*slug:[[:space:]]*\([A-Za-z0-9_-][A-Za-z0-9_-]*\).*/\1/p' "$1"
}

# Compare WHOLE LISTS, never a substring. A `case "$got" in *token*)` assertion
# passes against a token wearing a comment hash -- see CLAUDE.md on the launch
# gate's selftest printing eight PASSes against a parser broken on purpose.
join_ws() { tr -d '\r' | tr '\n' ' ' | sed 's/  */ /g; s/^ //; s/ *$//'; }

# Which expected slugs have no receipt file. Empty output = all present.
missing_receipts() {
  _dir="$1"; shift
  for _s in "$@"; do
    [ -f "$_dir/$_s" ] || printf '%s\n' "$_s"
  done
}

# ---------------------------------------------------------------------------
if [ "${1:-}" = "--selftest" ]; then
  rc=0
  tmp="$(mktemp)"
  {
    printf '    strategy:\n      matrix:\n        include:\n'
    printf '          - { env: alpha-s3-128,  slug: s3-128 }\n'
    printf '          - { env: bravo-harness }\n'
    printf '          # - { env: charlie-retired, slug: retired }\n'
    printf '          #- { env: delta-retired }\n'
    printf '          - { env: echo-s3-21, slug: s3-21 }\n'
    printf '      - name: a step, not a row, mentioning env: foxtrot and slug: nope\n'
  } > "$tmp"

  # alpha + echo publish; bravo is slug-less; charlie/delta are commented out;
  # the step name is not a row at all.
  want="s3-128 s3-21"
  got="$(parse_matrix_slugs "$tmp" | join_ws)"
  if [ "$got" = "$want" ]; then
    printf '  PASS  fixture parses to exactly: %s\n' "$got"
  else
    printf '  FAIL  fixture parse mismatch\n'
    printf '          want: [%s]\n' "$want"
    printf '          got:  [%s]\n' "$got"
    rc=1
  fi
  rm -f "$tmp"

  # THE CONTROL THAT MATTERS: prove a missing receipt is DETECTED. A checker that
  # cannot fail is not evidence, and this failure direction is the one that stops
  # an uncertified image becoming the fleet's target version.
  rdir="$(mktemp -d)"
  : > "$rdir/s3-128"
  miss="$(missing_receipts "$rdir" s3-128 s3-21 | join_ws)"
  if [ "$miss" = "s3-21" ]; then
    printf '  PASS  a missing receipt is detected (BLOCKING direction works)\n'
  else
    printf '  FAIL  missing receipt NOT detected; got: [%s]\n' "$miss"
    rc=1
  fi

  : > "$rdir/s3-21"
  miss="$(missing_receipts "$rdir" s3-128 s3-21 | join_ws)"
  if [ -z "$miss" ]; then
    printf '  PASS  a complete receipt set passes (PASSING direction works)\n'
  else
    printf '  FAIL  complete set reported missing: [%s]\n' "$miss"
    rc=1
  fi

  # An EMPTY receipts dir must report everything missing, not vacuously pass.
  rm -f "$rdir"/s3-128 "$rdir"/s3-21
  miss="$(missing_receipts "$rdir" s3-128 s3-21 | join_ws)"
  if [ "$miss" = "s3-128 s3-21" ]; then
    printf '  PASS  an empty receipt dir fails (does not vacuously pass)\n'
  else
    printf '  FAIL  empty dir did not report all missing; got: [%s]\n' "$miss"
    rc=1
  fi
  rm -rf "$rdir"

  # The real matrix must still parse to something anchored and above the floor.
  real="$(parse_matrix_slugs "$MATRIX" | join_ws)"
  n=0
  for s in $real; do n=$((n + 1)); done
  if [ "$n" -ge "$MIN_PUBLISHED_SLUGS" ]; then
    printf '  PASS  %s parses to %d publishing slug(s), floor %d\n' "$MATRIX" "$n" "$MIN_PUBLISHED_SLUGS"
  else
    printf '  FAIL  parsed only %d publishing slug(s), floor %d\n' "$n" "$MIN_PUBLISHED_SLUGS"
    rc=1
  fi
  case " $real " in
    *" $ANCHOR_SLUG "*) printf '  PASS  anchor slug %s present\n' "$ANCHOR_SLUG" ;;
    *) printf '  FAIL  anchor slug %s missing -- this is not the matrix\n' "$ANCHOR_SLUG"; rc=1 ;;
  esac

  if [ "$rc" -eq 0 ]; then
    printf 'SELFTEST PASSED\n'
  else
    printf 'SELFTEST FAILED\n' >&2
  fi
  exit "$rc"
fi

# ---------------------------------------------------------------------------
DIR="${1:-}"
if [ -z "$DIR" ]; then
  echo "usage: $0 <receipts-dir> | --selftest" >&2
  exit 2
fi

SLUGS="$(parse_matrix_slugs "$MATRIX")"
FLAT="$(printf '%s' "$SLUGS" | join_ws)"
n=0
for s in $SLUGS; do n=$((n + 1)); done

if [ "$n" -lt "$MIN_PUBLISHED_SLUGS" ]; then
  echo "FATAL: parsed only $n publishing slug(s) from $MATRIX, floor is $MIN_PUBLISHED_SLUGS." >&2
  echo "       Whatever was parsed, it is not this project's build matrix." >&2
  exit 2
fi

case " $FLAT " in
  *" $ANCHOR_SLUG "*) ;;
  *) echo "FATAL: anchor slug $ANCHOR_SLUG absent from the parse -- not this matrix." >&2; exit 2 ;;
esac

if [ ! -d "$DIR" ]; then
  echo "FATAL: receipts directory '$DIR' does not exist." >&2
  echo "       Absence of the directory is NOT absence of a requirement." >&2
  exit 2
fi

echo "expecting $n publishing slug(s): $FLAT"
echo "receipts present:                $(ls -1 "$DIR" 2>/dev/null | join_ws)"

# shellcheck disable=SC2086
MISS="$(missing_receipts "$DIR" $SLUGS | join_ws)"
if [ -n "$MISS" ]; then
  echo >&2
  echo "PUBLISH GATE FAILED: no receipt from these slug-ful leg(s): $MISS" >&2
  echo "  A receipt is written only after that SKU's launch gate AND its asset" >&2
  echo "  upload have both succeeded. Missing means the image was not certified," >&2
  echo "  so version.txt must NOT advance -- devices would chase a build that no" >&2
  echo "  gate passed." >&2
  exit 1
fi

echo "OK: every slug-ful leg published. version.txt may ship."
