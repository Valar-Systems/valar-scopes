#!/usr/bin/env python3
"""skus.yml -- parsed once, here, for everybody who needs it.

THE POINT IS THAT THERE IS ONE PARSER. The workflow matrix, check_release_envs
and the publish-receipts gate all shell out to this file. Three parsers for one
rule is three rules, and the second one is always the stale one -- which is how
a leg ends up shipping in one place and build-only in another.

No YAML library: skus.yml is a flat list of one-line flow mappings, chosen so
this stays dependency-free and so the rows remain greppable by eye.

  python scripts/skus.py --validate         # enforce the rules; exit 1 if broken
  python scripts/skus.py --matrix-json      # GitHub Actions matrix `include`
  python scripts/skus.py --shipping-slugs   # space-separated, for the gate
  python scripts/skus.py --shipping-envs
  python scripts/skus.py --all-envs
  python scripts/skus.py --check-readers   # who else reads the build matrix?
  python scripts/skus.py --selftest         # prove the validator can refuse
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(os.path.dirname(HERE), "skus.yml")

ROW = re.compile(
    r"^\s*-\s*\{\s*env:\s*(?P<env>[A-Za-z0-9_.-]+)\s*,"
    r"\s*status:\s*(?P<status>[a-z-]+)\s*"
    r"(?:,\s*slug:\s*(?P<slug>[A-Za-z0-9_.-]+)\s*)?\}\s*$"
)

VALID_STATUS = ("shipping", "build-only")


class SkuError(Exception):
    pass


def parse(text):
    """Rows, in file order. Raises SkuError on anything it cannot read exactly."""
    rows = []
    for lineno, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped == "skus:":
            continue
        if not stripped.startswith("-"):
            continue
        m = ROW.match(line)
        if not m:
            # A row this cannot read is NOT skipped. Skipping is how a leg
            # silently leaves the matrix, and a leg that vanishes from the build
            # is a SKU nobody compiles until a release.
            raise SkuError("line %d: cannot parse as a SKU row: %s" % (lineno, stripped))
        rows.append({"env": m.group("env"),
                     "status": m.group("status"),
                     "slug": m.group("slug"),
                     "line": lineno})
    return rows


def validate(rows):
    """The rules, enforced. Returns the rows; raises SkuError with every problem."""
    problems = []
    seen_env, seen_slug = {}, {}

    for r in rows:
        if r["status"] not in VALID_STATUS:
            problems.append("line %d: env %s has status '%s'; must be one of %s"
                            % (r["line"], r["env"], r["status"], " / ".join(VALID_STATUS)))
            continue

        # THE RULE THIS FILE EXISTS FOR, both directions.
        if r["status"] == "build-only" and r["slug"]:
            problems.append(
                "line %d: env %s is build-only but carries slug '%s'.\n"
                "        A build-only leg publishes nothing, so a slug would let it\n"
                "        withhold the fleet's version.txt when it fails -- which is\n"
                "        exactly what darkened OTA discovery on 2026-09-17.\n"
                "        Either drop the slug, or promote it to status: shipping."
                % (r["line"], r["env"], r["slug"]))
        if r["status"] == "shipping" and not r["slug"]:
            problems.append(
                "line %d: env %s is shipping but has no slug.\n"
                "        The slug names firmware-<slug>.bin, which is the only way a\n"
                "        device can request an image. A shipping SKU without one\n"
                "        publishes nothing and no device can ever update."
                % (r["line"], r["env"]))

        if r["env"] in seen_env:
            problems.append("line %d: env %s already declared on line %d"
                            % (r["line"], r["env"], seen_env[r["env"]]))
        seen_env[r["env"]] = r["line"]

        if r["slug"]:
            if r["slug"] in seen_slug:
                problems.append("line %d: slug %s already used on line %d -- two legs "
                                "would publish the same asset name"
                                % (r["line"], r["slug"], seen_slug[r["slug"]]))
            seen_slug[r["slug"]] = r["line"]

    # A FILE THAT SHIPS NOTHING makes the publish gate vacuous: with no expected
    # receipts, "every shipping leg published" is trivially true and version.txt
    # advances no matter what failed.
    if not [r for r in rows if r["status"] == "shipping"]:
        problems.append("no shipping SKU at all. The publish gate would pass "
                        "vacuously and version.txt would advance uncertified.")

    if problems:
        raise SkuError("\n".join(problems))
    return rows


def load(path=DEFAULT):
    with open(path, encoding="utf-8") as fh:
        return validate(parse(fh.read()))


# ---------------------------------------------------------------------------
# WHO ELSE READS THE BUILD MATRIX? Swept, not remembered.
#
# On 2026-09-17 the matrix moved from .github/workflows/firmware.yml into
# skus.yml. Four consumers were updated from a list someone had in mind, and a
# fifth -- scripts/check-adsbdb-out.sh -- was not. It kept parsing the workflow,
# found no rows, refused to scan, and every leg's launch gate failed. No receipt
# was written, version.txt was withheld, and the whole fleet lost OTA discovery
# over a product that does not exist.
#
# The lesson is not "remember the fifth one". A named list is a snapshot of who
# was reading at the moment somebody wrote it down, and the reader set is
# whatever is actually there. So this sweeps, and fails when the set changes.
#
# EVERY ENTRY BELOW IS PROSE. Nothing in this list parses the workflow any more:
# they are comments, docs, and one rule repeated in platformio.ini. A NEW file
# mentioning firmware.yml is not necessarily wrong -- it is unreviewed, which is
# the thing that cost the fleet an afternoon.
ALLOWED_MENTIONS = {
    ".github/workflows/workers.yml",     # a comment about what firmware.yml covers
    "scripts/check-adsbdb-out.sh",       # comments recording why the parser moved
    "platformio.ini",                    # the "the env IS the product" rule, x3
    "RELEASING.md",                      # release prose
    "docs/ota-control-plan.md",          # plan prose
}

SKIP_DIRS = (".git", ".pio", "node_modules", "bench-logs", ".venv")


def _walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            yield os.path.join(dirpath, fn)


def check_readers(root="."):
    """Fail when a file starts referring to the workflow matrix unreviewed."""
    found, unreadable = set(), []
    for path in _walk(root):
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        if rel == "scripts/skus.py":
            continue                      # this file names the file it guards
        try:
            with open(path, encoding="utf-8", errors="strict") as fh:
                text = fh.read()
        except (UnicodeDecodeError, OSError):
            continue                      # binaries are not readers
        if "firmware.yml" in text:
            found.add(rel)
        # The floor shape that made the outage silent: a magic minimum row count.
        if "MIN_MATRIX_ENVS" in text or "MIN_PUBLISHED_SLUGS" in text:
            unreadable.append(rel)

    problems = []
    new = sorted(found - ALLOWED_MENTIONS)
    if new:
        problems.append(
            "these files reference firmware.yml and are not on the reviewed list:\n"
            + "".join("    %s\n" % f for f in new)
            + "  If one of them PARSES the matrix, point it at skus.py instead -- a\n"
              "  second parser is how the fleet lost OTA discovery on 2026-09-17.\n"
              "  If it is only prose, add it to ALLOWED_MENTIONS in scripts/skus.py.")
    gone = sorted(ALLOWED_MENTIONS - found)
    if gone:
        problems.append(
            "these are on the reviewed list but no longer mention firmware.yml:\n"
            + "".join("    %s\n" % f for f in gone)
            + "  Drop them from ALLOWED_MENTIONS -- a stale allowlist hides the next\n"
              "  arrival behind an entry nobody re-checked.")
    if unreadable:
        problems.append(
            "a MIN_* row-count floor is back in:\n"
            + "".join("    %s\n" % f for f in sorted(unreadable))
            + "  A floor guards a parser that stopped matching; it is the wrong shape\n"
              "  when the count is a property of skus.yml, which refuses an empty file.")
    return problems


def _selftest():
    """Prove the validator REFUSES. A checker that cannot fail is not evidence."""
    rc = 0

    def case(name, text, want_ok, expect_in=None):
        nonlocal rc
        try:
            validate(parse(text))
            got_ok, msg = True, ""
        except SkuError as e:
            got_ok, msg = False, str(e)
        if got_ok != want_ok:
            print("  FAIL  %s: expected %s, got %s  %s"
                  % (name, "accept" if want_ok else "REFUSE",
                     "accept" if got_ok else "refuse", msg.splitlines()[0] if msg else ""))
            rc = 1
        elif expect_in and expect_in not in msg:
            print("  FAIL  %s: refused, but not for the stated reason" % name)
            print("        wanted to see: %s" % expect_in)
            rc = 1
        else:
            print("  ok    %s" % name)

    GOOD = ("skus:\n"
            "  - { env: blipscope-s3-128, status: shipping, slug: s3-128 }\n"
            "  - { env: orbitscope-s3-146, status: build-only }\n")
    case("a valid file is accepted", GOOD, True)

    case("a build-only leg carrying a slug is REFUSED",
         "skus:\n"
         "  - { env: blipscope-s3-128, status: shipping, slug: s3-128 }\n"
         "  - { env: orbitscope-s3-146, status: build-only, slug: space-s3-146 }\n",
         False, "is build-only but carries slug")

    case("a shipping leg with no slug is REFUSED",
         "skus:\n  - { env: blipscope-s3-128, status: shipping }\n",
         False, "is shipping but has no slug")

    case("a file with no shipping SKU is REFUSED",
         "skus:\n  - { env: orbitscope-s3-146, status: build-only }\n",
         False, "no shipping SKU at all")

    case("an unknown status is REFUSED",
         "skus:\n  - { env: a, status: maybe, slug: x }\n",
         False, "must be one of")

    case("a duplicate env is REFUSED",
         "skus:\n"
         "  - { env: dup, status: shipping, slug: a }\n"
         "  - { env: dup, status: build-only }\n",
         False, "already declared")

    case("two legs sharing a slug are REFUSED",
         "skus:\n"
         "  - { env: a, status: shipping, slug: same }\n"
         "  - { env: b, status: shipping, slug: same }\n",
         False, "already used")

    case("an unreadable row is REFUSED, not skipped",
         "skus:\n  - { env: blipscope-s3-128 }\n",
         False, "cannot parse as a SKU row")

    # The real file must itself be valid, and must say what we think it says.
    try:
        rows = load()
        ship = [r["slug"] for r in rows if r["status"] == "shipping"]
        print("  ok    %s parses: %d leg(s), shipping slug(s): %s"
              % (os.path.basename(DEFAULT), len(rows), " ".join(ship)))
    except SkuError as e:
        print("  FAIL  the real skus.yml does not validate:\n%s" % e)
        rc = 1
    except OSError as e:
        print("  FAIL  cannot read the real skus.yml: %s" % e)
        rc = 1

    print("SELFTEST PASSED" if rc == 0 else "SELFTEST FAILED")
    return rc


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--validate"
    if mode == "--selftest":
        return _selftest()
    try:
        rows = load()
    except (SkuError, OSError) as e:
        print("skus.yml is invalid:\n%s" % e, file=sys.stderr)
        return 1

    if mode == "--validate":
        ship = [r for r in rows if r["status"] == "shipping"]
        print("skus.yml OK: %d leg(s), %d shipping (%s), %d build-only."
              % (len(rows), len(ship), " ".join(r["slug"] for r in ship),
                 len(rows) - len(ship)))
        return 0
    if mode == "--matrix-json":
        inc = [({"env": r["env"], "slug": r["slug"]} if r["slug"] else {"env": r["env"]})
               for r in rows]
        print(json.dumps({"include": inc}, separators=(",", ":")))
        return 0
    if mode == "--shipping-slugs":
        print(" ".join(r["slug"] for r in rows if r["status"] == "shipping"))
        return 0
    if mode == "--shipping-envs":
        print(" ".join(r["env"] for r in rows if r["status"] == "shipping"))
        return 0
    if mode == "--all-envs":
        print(" ".join(r["env"] for r in rows))
        return 0
    if mode == "--check-readers":
        problems = check_readers(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        if problems:
            print("SKU READER SWEEP FAILED\n", file=sys.stderr)
            for pr in problems:
                print("  " + pr, file=sys.stderr)
            return 1
        print("reader sweep OK: %d reviewed mention(s), no parser but skus.py."
              % len(ALLOWED_MENTIONS))
        return 0

    print("unknown mode: %s" % mode, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
