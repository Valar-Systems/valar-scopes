#!/usr/bin/env python3
"""Reconcile the manufacturing record against what production actually sees.

    python scripts/reconcile-fleet.py

Two rosters, three answers:

    in both    provisioned, and confirmed authenticating against production
    CSV only   provisioned, but NOT observed by Instrument A recently
    KV only    authenticating against production, absent from the manufacturing
               record -- a unit we shipped or bench-built and cannot account for

WHAT "CSV ONLY" DOES NOT MEAN, AND WHY IT IS PRINTED EVERY RUN
--------------------------------------------------------------
A fw: row is written by Instrument A (proxy/src/fleet.ts) on the AUTHENTICATED
feed path, and the instrument was deployed on 2026-09-01. No row can predate the
deploy, so a board that has not made an authenticated request since then has no
row REGARDLESS of whether it was ever enrolled.

That collapses four very different states into one bucket:

    switched off         |
    boxed for shipping   |  all indistinguishable, all "CSV only"
    never enrolled       |
    wrongly keyed        |

So "CSV only" is "not seen recently", never "never worked". The report prints the
observed instrument start (derived, not hardcoded -- see below) so the size of
"recently" is on screen rather than in someone's head. At 50 units the difference
matters: a unit packed for shipping goes CSV-only within the hour, and a gate
that reads that as a fault will cry wolf on every box that ships.

WHY IT REFUSES RATHER THAN REPORTING ZERO
-----------------------------------------
"Everything is missing" is the single most likely shape of a broken probe, and
this one has three ways to produce it silently: wrangler run from a directory
with no wrangler.toml (the repo root is one), an expired token, and a listing
that succeeds against the wrong namespace. Each returns an empty list, which
would read as "the entire fleet stopped talking to production" -- a spectacular
false alarm, delivered with total confidence.

So an empty listing is EXIT 2 (cannot judge), distinct from exit 1 (a real
discrepancy). The probe proves it can observe presence before it is allowed to
report absence.
"""
import argparse
import csv
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# wrangler resolves its config from the CWD. There is no wrangler.toml at the
# repo root, and from there every lookup returns empty -- which this tool would
# otherwise report as a fleet-wide outage. Not a detail: it has already happened
# once here, to scripts/verify-release.sh.
PROXY = REPO / "proxy"

# The server's own device-id shape (see DeviceIdentity::LeaderboardId and the
# Worker's /^[0-9a-f]{8,32}$/). Validating with the CONSUMER's rule means a value
# this tool accepts is a value the fleet table would accept.
ID_RE = re.compile(r"^[0-9a-f]{8,32}$")


def die(msg: str, code: int = 2) -> None:
    print("\nreconcile: " + msg, file=sys.stderr)
    sys.exit(code)


def npx() -> str:
    """On Windows npx is npx.cmd, and CreateProcess will not find a bare "npx"
    without a shell. Resolve it here rather than reaching for shell=True, which
    would put every argument through cmd's quoting rules as well."""
    p = shutil.which("npx") or shutil.which("npx.cmd")
    if not p:
        die("npx not found on PATH -- cannot reach wrangler, so KV cannot be read")
    return p


def kv_list_names(prefix: str, env: str, errfile: Path) -> list[str]:
    """Raw key names under a prefix. Exit status is checked; shape is the
    caller's business, because a device-id key and a yyyy-mm-dd key are both
    legitimate and only the caller knows which it asked for.

    stderr goes to its own file rather than being merged or dropped: wrangler
    puts auth failures on stderr and a cheerful "report this error?" on stdout,
    so a stdout-only read of a failed call looks like a parse problem instead of
    a 401. Knowing a result is untrustworthy is only half an answer; this is the
    other half.
    """
    cmd = [npx(), "wrangler", "kv", "key", "list", "--binding", "ENRICH_KV",
           "--env", env, "--prefix", prefix, "--remote"]
    with errfile.open("w", encoding="utf-8") as ef:
        r = subprocess.run(cmd, cwd=PROXY, stdout=subprocess.PIPE, stderr=ef,
                           text=True, timeout=180)
    err = errfile.read_text(encoding="utf-8", errors="replace").strip()
    if r.returncode != 0:
        die("wrangler kv key list failed for prefix %s (exit %d).\n--- stderr ---\n%s"
            % (prefix, r.returncode, err or "(empty)"))
    try:
        rows = json.loads(r.stdout)
    except json.JSONDecodeError:
        die("could not parse wrangler output as JSON.\n"
            "--- stdout (first 400) ---\n%s\n--- stderr ---\n%s"
            % (r.stdout[:400], err or "(empty)"))
    return sorted({(o.get("name") or "") for o in rows if o.get("name")})


def kv_list(prefix: str, env: str, errfile: Path) -> list[str]:
    """Device ids under a prefix, validated against the consumer's own id shape."""
    ids, malformed = [], []
    for name in kv_list_names(prefix, env, errfile):
        rest = name[len(prefix):] if name.startswith(prefix) else name
        (ids if ID_RE.match(rest) else malformed).append(rest)
    if malformed:
        print("  note: %d key(s) under %s do not match the id shape and were ignored: %s"
              % (len(malformed), prefix, ", ".join(malformed[:5])))
    return sorted(set(ids))


def kv_get(dev_id: str, env: str, errfile: Path) -> dict | None:
    cmd = [npx(), "wrangler", "kv", "key", "get", "fw:" + dev_id, "--binding", "ENRICH_KV",
           "--env", env, "--remote"]
    with errfile.open("w", encoding="utf-8") as ef:
        r = subprocess.run(cmd, cwd=PROXY, stdout=subprocess.PIPE, stderr=ef,
                           text=True, timeout=120)
    if r.returncode != 0:
        return None
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        return None


def kv_get_raw(key: str, env: str, errfile: Path) -> str | None:
    cmd = [npx(), "wrangler", "kv", "key", "get", key, "--binding", "ENRICH_KV",
           "--env", env, "--remote"]
    with errfile.open("w", encoding="utf-8") as ef:
        r = subprocess.run(cmd, cwd=PROXY, stdout=subprocess.PIPE, stderr=ef,
                           text=True, timeout=120)
    return r.stdout.strip() if r.returncode == 0 else None


def report_instruments(env: str, errfile: Path, detail: dict, days: int, alert: int) -> list[str]:
    """Read the instruments OUT LOUD, every run. Returns warning lines.

    ============================================================================
    WHY THIS SECTION EXISTS, AND IT IS NOT THE SAME BUG AS THE OTHERS
    ============================================================================
    A guard with a path around it fails because one caller skipped it. A rule
    that never fires fails because it was measuring nothing. This is the third
    kind and it is the worst of them: AN INSTRUMENT THAT FIRES CORRECTLY INTO A
    VOID.

    The enrolment ledger recorded a runaway loop faithfully, ~600 events a day,
    for twenty days. The Worker logged every one. Nothing was broken: the code
    was right, the data was right, the number was sitting in KV the whole time.
    It surfaced on 2026-09-01 only because a reconcile run for an UNRELATED
    reason happened to list the keys.

    Nothing about that is reassuring, because everything looked healthy. There
    is no failing test to write, no guard to add, no assertion that would have
    tripped. The only defect was that no one ever read it.

    So the fix is not a new datapoint or new infrastructure -- both already
    existed. The fix is putting the number somewhere a person is already
    looking. This tool gets run whenever anyone asks about the fleet, which
    makes it the cheapest surface there is.

    THE SAME FATE IS AVAILABLE TO INSTRUMENT A. The fw: table was built today
    and will be read attentively during Run 1. After that, unless something
    reads it routinely, it becomes another correct number nobody looks at --
    which is precisely how the ledger got here. It is therefore reported below
    on every run too, not only when someone remembers to ask.
    """
    warn: list[str] = []
    print("INSTRUMENTS -- read every run, because a correct number nobody reads is")
    print("               indistinguishable from no number at all")
    print()

    # --- Instrument: enrolment volume (enr:day:) --------------------------
    # The fw: listing already succeeded by the time we get here, which is what
    # licenses a NEGATIVE reading of this one: KV is demonstrably readable, so
    # "no counters" means no counters rather than "cannot see".
    names = kv_list_names("enr:day:", env, errfile)
    print("  Enrolments per day (last %d)" % days)
    if not names:
        print("    (no enr:day: counters at all -- KV is readable, since fw: listed above,")
        print("     so this is a real absence. Either nothing has ever enrolled or the")
        print("     counters have aged out of their 40-day TTL.)")
    else:
        recent = names[-days:]
        rows = []
        for n in recent:
            v = kv_get_raw(n, env, errfile)
            try:
                rows.append((n[len("enr:day:"):], int(v)))
            except (TypeError, ValueError):
                rows.append((n[len("enr:day:"):], None))
        width = max((c for _, c in rows if c is not None), default=1) or 1
        for day, c in rows:
            if c is None:
                print("    %s  (unreadable)" % day)
                continue
            bar = "#" * max(1, round(20 * c / width)) if c else ""
            flag = "  <== ABOVE BASELINE" if c > alert else ""
            print("    %s  %6d  %s%s" % (day, c, bar, flag))
            if c > alert:
                warn.append("enrolments on %s were %d (baseline %d)" % (day, c, alert))
        # Days with NO key are not zeros in this listing -- they are absent. That
        # distinction is what proved the 2026-09-01 loop was a browser tab and not
        # firmware (a device-side loop cannot skip eight days), so it is stated
        # rather than smoothed into a zero.
        print("    note: days with no enrolments have no key and are simply absent above.")
        print("          Gaps are evidence, not missing data.")
    print("    A quiet bench is single digits. ~600/day was one browser tab left open on")
    print("    the enrol page (fixed 2026-09-01) -- that is the scale this flags at %d." % alert)
    print()

    # --- Instrument A: the fleet firmware table (fw:) ---------------------
    print("  Instrument A (fw: rows) -- OTA outcome observation")
    if not detail:
        print("    no rows fetched (--no-detail); liveness not assessed this run")
    else:
        now = datetime.now(timezone.utc).timestamp()
        ages = sorted((now - r["lastSeen"] / 1000) / 3600
                      for r in detail.values() if r.get("lastSeen"))
        fws: dict[str, int] = {}
        changes = 0
        for r in detail.values():
            fws[str(r.get("fw", "?"))] = fws.get(str(r.get("fw", "?")), 0) + 1
            changes += len(r.get("changes") or [])
        print("    %d row(s); firmware spread: %s"
              % (len(detail), ", ".join("fw%s x%d" % (k, v) for k, v in sorted(fws.items()))))
        print("    last seen: newest %.1f h ago, oldest %.1f h ago" % (ages[0], ages[-1]))
        # RUN 1 READS THIS NUMBER, NOT "is changes[] non-empty". A synthetic test
        # on 2026-09-01 left a real 8->9->8 pair in the table, so a non-empty
        # array is already the resting state and would read as a false positive.
        # The baseline is the COUNT; Run 1 looks for it to increase.
        print("    change entries recorded: %d  <-- Run 1 baseline: look for a NEW entry," % changes)
        print("                                    not for a non-empty array")
    print()
    return warn


def read_csv(path: Path) -> dict:
    """device_id -> row. Tolerates the original 4-column rows and the 5-column
    ones carrying a `source` marker; a row with no marker was written at
    provisioning time by the script, which is what its absence means."""
    if not path.exists():
        die("%s does not exist -- nothing to reconcile against" % path)
    out = {}
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            dev = (row.get("device_id") or "").strip()
            if not dev:
                continue
            row["source"] = (row.get("source") or "provisioned").strip() or "provisioned"
            out[dev] = row
    return out


def ts(ms) -> str:
    if not ms:
        return "-"
    return datetime.fromtimestamp(ms / 1000, timezone.utc).isoformat(timespec="seconds")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default=str(REPO / "provisioned.csv"))
    ap.add_argument("--env", default="production")
    ap.add_argument("--prefix", default="fw:")
    ap.add_argument("--no-detail", action="store_true",
                    help="skip the per-row fetch (loses firstSeen/lastSeen and the derived window)")
    ap.add_argument("--days", type=int, default=7,
                    help="how many days of enrolment counters to show (default 7)")
    ap.add_argument("--enroll-alert", type=int, default=25,
                    help="flag any day whose enrolment count exceeds this (default 25)")
    ap.add_argument("--json", help="also write the report here; must be a git-ignored path")
    args = ap.parse_args()

    # A report carries real device ids. Rather than trusting whoever runs this to
    # pick a safe path, ask git and refuse if the answer is wrong -- a check that
    # executes, instead of a warning in a docstring.
    #
    # The question is "could this file end up committed", NOT "is it gitignored".
    # Those differ for a path OUTSIDE the working tree, which git reports as
    # not-ignored (it has no opinion) while being the safest destination there
    # is. Refusing a temp dir would push people toward --json inside the repo,
    # which is the outcome this guard exists to prevent.
    if args.json:
        dest = Path(args.json).resolve()
        try:
            dest.relative_to(REPO)
            inside = True
        except ValueError:
            inside = False
        if inside:
            g = subprocess.run(["git", "check-ignore", "-q", str(dest)], cwd=REPO)
            if g.returncode != 0:
                die("--json %s is inside the repo and is NOT git-ignored, and the report\n"
                    "        contains real device ids. Write it outside the repo, or add the\n"
                    "        path to .gitignore." % args.json)

    errfile = REPO / ".reconcile-stderr.log"
    print("\n=== fleet reconcile [%s] ===" % args.env)
    print("  record   %s" % args.log)
    print("  kv       ENRICH_KV %s* (cwd %s)" % (args.prefix, PROXY))

    csv_rows = read_csv(Path(args.log))
    kv_ids = kv_list(args.prefix, args.env, errfile)

    # THE CONTROL. Presence must be observable before absence is reportable.
    if not kv_ids:
        die("KV returned ZERO %s keys.\n"
            "        Refusing to judge: an empty listing cannot be told apart from a probe\n"
            "        that cannot read KV (wrong cwd, wrong namespace, expired token).\n"
            "        If the fleet is genuinely empty this is still the right answer --\n"
            "        confirm by hand, from %s:\n"
            "          npx wrangler kv key list --binding ENRICH_KV --env %s --prefix %s --remote\n"
            "--- captured stderr ---\n%s"
            % (args.prefix, PROXY, args.env, args.prefix,
               errfile.read_text(encoding="utf-8", errors="replace").strip() or "(empty)"))
    print("  observed %d live row(s) -- probe can see presence, so absence is meaningful\n"
          % len(kv_ids))

    detail = {}
    if not args.no_detail:
        for d in kv_ids:
            rec = kv_get(d, args.env, errfile)
            if rec:
                detail[d] = rec

    csv_ids = set(csv_rows)
    kv_set = set(kv_ids)
    both = sorted(csv_ids & kv_set)
    csv_only = sorted(csv_ids - kv_set)
    kv_only = sorted(kv_set - csv_ids)

    def show(title: str, ids: list, note: str) -> None:
        print("%s  (%d)" % (title, len(ids)))
        if not ids:
            print("    (none)")
        for d in ids:
            row = csv_rows.get(d)
            rec = detail.get(d)
            bits = []
            if row:
                bits.append("env=" + str(row.get("env", "?")))
                if row["source"] != "provisioned":
                    bits.append("src=" + row["source"])
            if rec:
                bits.append("fw=" + str(rec.get("fw", "?")))
                bits.append("lastSeen=" + ts(rec.get("lastSeen")))
            print("    %s  %s" % (d, "  ".join(bits)))
        if note:
            print("    -> " + note)
        print()

    show("IN BOTH   provisioned + confirmed talking to production", both, "")
    show("CSV ONLY  provisioned, not observed recently", csv_only,
         "NOT 'never enrolled' -- see the window below")
    show("KV ONLY   authenticating, absent from the manufacturing record", kv_only,
         "each of these is an unaccounted-for unit; find out what built it")

    # The window, derived from the data rather than hardcoded, so it stays true
    # as the instrument ages instead of going stale the way a constant would.
    if detail:
        seen = [r.get("firstSeen") for r in detail.values() if r.get("firstSeen")]
        if seen:
            first = min(seen)
            age_h = (datetime.now(timezone.utc).timestamp() - first / 1000) / 3600
            print("CAVEAT -- the size of 'recently', measured, not assumed")
            print("  Earliest observation in this table: %s  (%.1f h ago)" % (ts(first), age_h))
            print("  Instrument A writes a row only on an AUTHENTICATED request and was deployed")
            print("  2026-09-01, so NO row can predate it. A board is 'CSV only' if it has not")
            print("  authenticated in roughly the last %.1f h -- which is equally true of a unit"
                  % age_h)
            print("  switched off, boxed for shipping, never enrolled, or wrongly keyed. This")
            print("  report cannot separate those four, and nothing downstream should claim to.")
            print()

    warn = report_instruments(args.env, errfile, detail, args.days, args.enroll_alert)

    consistent = not csv_only and not kv_only
    print("SUMMARY  in-both=%d  csv-only=%d  kv-only=%d   -> %s"
          % (len(both), len(csv_only), len(kv_only),
             "consistent" if consistent else "DISCREPANCIES"))
    for w in warn:
        print("         INSTRUMENT WARNING: %s" % w)
    print()

    if args.json:
        Path(args.json).write_text(json.dumps({
            "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "env": args.env, "both": both, "csv_only": csv_only, "kv_only": kv_only,
            "detail": detail,
        }, indent=2), encoding="utf-8")
        print("  report written to %s" % args.json)

    errfile.unlink(missing_ok=True)
    sys.exit(0 if (consistent and not warn) else 1)


if __name__ == "__main__":
    main()
