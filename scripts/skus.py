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
  python scripts/skus.py --build-envs       # every leg CI builds (shipping + build-only; never retired)
  python scripts/skus.py --sabotage         # prove a retired row cannot be published (A12.105)
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

VALID_STATUS = ("shipping", "build-only", "retired")


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
        # A RETIRED LEG IS NOT BUILT AND PUBLISHES NOTHING (docs/weatherscope-stage3.md A12.105).
        # A slug on one would name an asset for hardware that will never be sold.
        if r["status"] == "retired" and r["slug"]:
            problems.append(
                "line %d: env %s is retired but carries slug '%s'.\n"
                "        A retired leg is not built and publishes nothing; a slug would\n"
                "        name an OTA asset for hardware that is not made or sold.\n"
                "        Either drop the slug, or bring it back as build-only or shipping."
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


def matrix(rows):
    """The CI build matrix's `include`: every row but the retired ones, which are not built."""
    return [({"env": r["env"], "slug": r["slug"]} if r["slug"] else {"env": r["env"]})
            for r in rows if r["status"] != "retired"]


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
    "docs/v17-whats-new.md",             # spec prose citing the release jobs by line; parses nothing
    "scripts/check-tag-workflow.sh",     # reads the version + promote jobs, not the matrix
    "scripts/check-customer-entry.py",   # reads build's needs + its own job (--check-wiring), not the matrix
    "scripts/check-no-bench-hooks.sh",   # a comment naming the job that runs it; scans an image, never the matrix
    "docs/host-tests-ci.md",             # predictions for the host-tests job; names the workflow, parses nothing
    "docs/weatherscope-stage3.md",       # the Stage 3 plan; names the workflow and skus.yml, parses nothing
    "docs/ota-url-check.md",             # predictions for the OTA URL check; names the workflow and skus.yml, parses nothing
    "scripts/check_ota_url.py",          # reads the build job's step order (--check-wiring), never the matrix
    "scripts/check_ota_identity.py",     # reads the build job's step order (--check-wiring); skus.yml via skus.load
    "docs/prebuild-patches.md",          # the pre-build patch audit; names the workflow in prose, parses nothing
    "scripts/check_workflow_concurrency.py",  # reads every workflow's on: and concurrency blocks, never the matrix
}

SKIP_DIRS = (".git", ".pio", "node_modules", "bench-logs", ".venv")


def _walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in filenames:
            yield os.path.join(dirpath, fn)


def check_readers(root=".", allowed=None):
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
    allowed = ALLOWED_MENTIONS if allowed is None else allowed
    new = sorted(found - allowed)
    if new:
        problems.append(
            "these files reference firmware.yml and are not on the reviewed list:\n"
            + "".join("    %s\n" % f for f in new)
            + "  If one of them PARSES the matrix, point it at skus.py instead -- a\n"
              "  second parser is how the fleet lost OTA discovery on 2026-09-17.\n"
              "  If it is only prose, add it to ALLOWED_MENTIONS in scripts/skus.py.")
    gone = sorted(allowed - found)
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


def _readers_selftest():
    """Prove the sweep refuses in each direction, in a throwaway tree."""
    import tempfile
    rc = 0

    def tree(files):
        d = tempfile.mkdtemp(prefix="skureaders-")
        for rel, text in files.items():
            p = os.path.join(d, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as fh:
                fh.write(text)
        return d

    def case(name, files, allowed, want_ok, expect_in):
        nonlocal rc
        problems = check_readers(tree(files), allowed)
        ok = not problems
        msg = "\n".join(problems)
        if ok != want_ok or (expect_in and expect_in not in msg):
            print("  FAIL  %s" % name)
            rc = 1
        else:
            print("  ok    %s" % name)

    reviewed = {"docs/a.md": "see firmware.yml"}
    case("a reviewed set passes", reviewed, {"docs/a.md"}, True, "")
    case("a NEW file mentioning the workflow is REFUSED",
         dict(reviewed, **{"scripts/new.sh": 'MATRIX=".github/workflows/firmware.yml"'}),
         {"docs/a.md"}, False, "scripts/new.sh")
    case("a MIN_ row-count floor is REFUSED",
         dict(reviewed, **{"scripts/old.sh": "MIN_MATRIX_ENVS=8"}),
         {"docs/a.md"}, False, "floor")
    case("a STALE allowlist entry is REFUSED",
         reviewed, {"docs/a.md", "docs/gone.md"}, False, "docs/gone.md")
    print("SELFTEST PASSED" if rc == 0 else "SELFTEST FAILED")
    return rc


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

    case("a retired leg carrying a slug is REFUSED",
         GOOD + "  - { env: blipscope-s3-146, status: retired, slug: s3-146 }\n",
         False, "is retired but carries slug")

    def held(name, ok_, detail):
        nonlocal rc
        print("  %s  %s%s" % ("ok  " if ok_ else "FAIL", name, "" if ok_ else ": " + detail))
        rc |= 0 if ok_ else 1

    rows_ = validate(parse(GOOD + "  - { env: blipscope-s3-146, status: retired }\n"))
    envs_ = [i["env"] for i in matrix(rows_)]
    held("a retired leg is left out of the matrix",
         "blipscope-s3-146" not in envs_ and "orbitscope-s3-146" in envs_, "the matrix is %s" % envs_)

    # The real file must itself be valid, and must say what we think it says.
    try:
        rows = load()
        ship = [r["slug"] for r in rows if r["status"] == "shipping"]
        print("  ok    %s parses: %d leg(s), shipping slug(s): %s"
              % (os.path.basename(DEFAULT), len(rows), " ".join(ship)))
        retired = {r["env"] for r in rows if r["status"] == "retired"}
        leaked = retired & {i["env"] for i in matrix(rows)}
        held("the real file's matrix holds no retired env (%d retired)" % len(retired), not leaked,
             "retired but in the matrix: %s" % sorted(leaked))
    except SkuError as e:
        print("  FAIL  the real skus.yml does not validate:\n%s" % e)
        rc = 1
    except OSError as e:
        print("  FAIL  cannot read the real skus.yml: %s" % e)
        rc = 1

    print("SELFTEST PASSED" if rc == 0 else "SELFTEST FAILED")
    return rc


# ---------------------------------------------------------------------------
# A RETIRED ROW CANNOT BE PUBLISHED: proved against the REAL file and the REAL code
# (docs/weatherscope-stage3.md A12.105). The selftest's cases are synthetic; these
# plant into copies of what is actually in the tree, and each must be refused with
# exactly its own reason. A guard nobody has seen refuse is not evidence.
#
#   S-R1  the real skus.yml with a retired row given its old slug -> --validate refuses,
#         with exactly one problem: "is retired but carries slug"
#   S-R2  this script with the matrix's retired exclusion removed -> its --selftest
#         fails exactly the two matrix cases, and nothing else
# Plus the controls: the unplanted file validates, and the unplanted copy's selftest passes.

S_R1 = ("  - { env: blipscope-s3-146, status: retired }",
        "  - { env: blipscope-s3-146, status: retired, slug: s3-146 }")
# Split so this file holds the anchor once, in matrix(), and not a second time here.
S_R2 = ('            for r in rows if r["status"] != ' + '"retired"]',
        '            for r in rows]  # SABOTAGE S-R2: the retired exclusion removed')
S_R2_FAILS = ["a retired leg is left out of the matrix", "the real file's matrix holds no retired env"]


def _problems(text):
    """validate()'s problems, one per entry (continuation lines are indented)."""
    try:
        validate(parse(text))
        return []
    except SkuError as e:
        return [p for p in str(e).split("\n") if not p.startswith(" ")]


def _sabotage():
    import shutil, subprocess, tempfile
    ok = True
    real_yml = open(DEFAULT, encoding="utf-8").read().replace("\r\n", "\n")
    real_py = open(os.path.abspath(__file__), encoding="utf-8").read().replace("\r\n", "\n")

    clean = _problems(real_yml)
    print("  %s control: the real skus.yml validates (%d problem(s))" % ("ok  " if not clean else "FAIL", len(clean)))
    ok &= not clean

    assert real_yml.count(S_R1[0]) == 1, "S-R1: the anchor row is not in skus.yml exactly once"
    got = _problems(real_yml.replace(S_R1[0], S_R1[1]))
    good = len(got) == 1 and "is retired but carries slug" in got[0]
    print("  %s S-R1 a retired row with a slug: %d problem(s) %s" % ("ok  " if good else "FAIL", len(got), got))
    ok &= good

    def selftest_of(src):
        with tempfile.TemporaryDirectory() as td:
            os.makedirs(os.path.join(td, "scripts"))
            open(os.path.join(td, "scripts", "skus.py"), "w", encoding="utf-8", newline="\n").write(src)
            shutil.copy(DEFAULT, os.path.join(td, "skus.yml"))
            r = subprocess.run([sys.executable, "-I", os.path.join(td, "scripts", "skus.py"), "--selftest"],
                               capture_output=True, text=True)
        fails = [l.strip()[len("FAIL"):].strip() for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
        return r.returncode, fails

    rc, fails = selftest_of(real_py)
    print("  %s control: the unplanted selftest, run from a copy: exit %d, %d failing" % (
        "ok  " if rc == 0 and not fails else "FAIL", rc, len(fails)))
    ok &= rc == 0 and not fails

    assert real_py.count(S_R2[0]) == 1, "S-R2: the anchor line is not in skus.py exactly once"
    rc, fails = selftest_of(real_py.replace(S_R2[0], S_R2[1]))
    names = sorted(f.split(" (")[0].split(":")[0] for f in fails)
    good = rc == 1 and names == sorted(S_R2_FAILS)
    print("  %s S-R2 the matrix keeps retired rows: selftest exit %d, failing %s" % ("ok  " if good else "FAIL", rc, names))
    ok &= good
    print("SABOTAGE " + ("PASSED: a retired row cannot be published" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    mode = argv[1] if len(argv) > 1 else "--validate"
    if mode == "--selftest":
        return _selftest()
    if mode == "--sabotage":
        return _sabotage()
    try:
        rows = load()
    except (SkuError, OSError) as e:
        print("skus.yml is invalid:\n%s" % e, file=sys.stderr)
        return 1

    if mode == "--validate":
        ship = [r for r in rows if r["status"] == "shipping"]
        print("skus.yml OK: %d leg(s), %d shipping (%s), %d build-only, %d retired."
              % (len(rows), len(ship), " ".join(r["slug"] for r in ship),
                 sum(1 for r in rows if r["status"] == "build-only"),
                 sum(1 for r in rows if r["status"] == "retired")))
        return 0
    if mode == "--matrix-json":
        print(json.dumps({"include": matrix(rows)}, separators=(",", ":")))
        return 0
    if mode == "--shipping-slugs":
        print(" ".join(r["slug"] for r in rows if r["status"] == "shipping"))
        return 0
    if mode == "--shipping-envs":
        print(" ".join(r["env"] for r in rows if r["status"] == "shipping"))
        return 0
    if mode == "--build-envs":
        print(" ".join(i["env"] for i in matrix(rows)))
        return 0
    if mode == "--check-readers-selftest":
        return _readers_selftest()
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
