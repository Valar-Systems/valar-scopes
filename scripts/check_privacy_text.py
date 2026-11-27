#!/usr/bin/env python3
"""Retired privacy claims stay retired, in every place they were published.

Each phrase in RETIRED was a public privacy statement found false, and replaced:
  - "... and nothing that identifies your device" (README, the support page): request logs can hold the device's
    identifier and network address (#747, 2026-11-17);
  - "anonymous usage counts": each report is kept against the device's identifier;
  - "not where you are": the radar's requests carry the location, rounded to about 1 km;
  - "operational telemetry only": there are usage counts too;
  - "(its API key and hardware model)": the device identifier and firmware version are sent and kept too;
  - "Counts only — no location" (the leaderboard page): a submission also carries the spotter name and the types;
  - "no location or flight data leaves your device" (the leaderboard page): in cloud mode the radar sends a rounded
    location (the owner, 2026-11-17).
  - "own receiver — then it never talks to us", "own receiver and the radar never talks to us", "which otherwise never
    talks to anyone": an own-receiver device with Aircraft details set to Blipscope Cloud asks us about every aircraft
    in view (the owner, 2026-11-26: true only with details Off);
  - "nothing external is contacted at all", "transmits nothing, by construction": with details Off the device still
    checks GitHub for updates and sets its clock from pool.ntp.org;
  - "not on an update, not ever": firmware 9 moved every "adsbdb direct" choice to Blipscope Cloud;
  - "we're never contacted; a third party is": the adsbdb direct option no longer exists;
  - "when you tap an aircraft, we're sent": the lookups cover every aircraft in view, not only a tapped one;
  - "which for the radar includes the area it asked about": the log keeps exactly what each request asked for, a
    detail card's and a photo's included;
  - "activating a device still logs": the activation record is kept until the device next activates, not only logged.
  - "asks us for that aircraft's photo": a detail card's photo is of the aircraft's type (the owner, 2026-11-26).
This check fails if any of them comes back in any of FILES, in any letter case or line wrapping.

A check that finds nothing proves nothing on its own, so each file also has two controls:
  - its privacy text's own anchor is present, so the check is reading the right text;
  - its replacement wording is present (REQUIRED), so the fix has not quietly gone too.
If a later edit rewords a replacement on purpose, update FILES below in the same change.

Those sentences about what our request log keeps rest on a premise in proxy/wrangler.toml, so four more rows assert it
rather than state it (the invocation-logs guard, ruled 2026-11-26):
  - IL-P  the config parses (a parse failure turns all four red: nothing can be certified);
  - IL-C  CONTROL: staging and production each have an observability table, read from the document itself;
  - IL-A  every observability table the walk reaches (top-level, previews, each env and its previews) sets
          logs.invocation_logs to the boolean false. Absent is red, because Cloudflare's default is ON;
  - IL-N  no table traces, pushes or tails a request envelope: traces.enabled = true, logpush, tail_consumers,
          logs.destinations, or the tracing compatibility flag.
A compatibility_date bump can change Cloudflare's defaults with no config edit at all. No check here can see that,
and the comment at compatibility_date says so: the weak form.

  python3 scripts/check_privacy_text.py              check the tree
  python3 scripts/check_privacy_text.py --selftest   plant each failure into an in-memory copy; each must turn
                                                     exactly its own row red

Exit 0 pass, 1 a row is red, 2 a file cannot be read, 3 the selftest is off its prediction.
"""
import os
import re
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# path -> (the anchor proving the check reads that file's privacy text, the replacement wording that must be present)
FILES = {
    "README.md": ("## privacy & telemetry",
                  ("cloudflare keeps these logs for up to seven days", "ever holds the device's key",
                   "usage counts for each device")),
    "proxy/pages/support.html": ('id="privacy"',
                                 ("cloudflare keeps these logs for up to seven days", "ever holds the device's key",
                                  "usage counts for each device")),
    "proxy/pages/leaderboard.html": ("opt in from your blipscope", ("your spotter name",)),
}
RETIRED = (
    "nothing that identifies your device",
    "anonymous usage counts",
    "not where you are",
    "operational telemetry only",
    "its api key and hardware model)",
    "counts only — no location",
    "no location or flight data leaves your device",
    "own receiver — then it never talks to us",
    "own receiver and the radar never talks to us",
    "which otherwise never talks to anyone",
    "nothing external is contacted at all",
    "transmits nothing, by construction",
    "not on an update, not ever",
    "we're never contacted; a third party is",
    "when you tap an aircraft, we're sent",
    "which for the radar includes the area it asked about",
    "activating a device still logs",
    "asks us for that aircraft's photo",
)

# THE INVOCATION-LOGS PREMISE (rows IL-P, IL-C, IL-A, IL-N; handoff proxy/activation-geo/il-guard-registration.md). What
# the pages say our request log keeps is true only while Cloudflare's per-request invocation event is OFF in every
# observability table of the proxy's config, and Cloudflare's default is ON when the key is absent.
WRANGLER = "proxy/wrangler.toml"
IL_ROWS = {
    "IL-P": f"IL-P {WRANGLER}: the file parses",
    "IL-C": f"IL-C {WRANGLER}: staging and production each have an observability table",
    "IL-A": f"IL-A {WRANGLER}: every observability table sets logs.invocation_logs = false",
    "IL-N": f"IL-N {WRANGLER}: no request envelope is traced, pushed or tailed",
}
# Cloudflare: this flag "will enable Workers Tracing by default" wherever observability is enabled.
TRACING_FLAG = "enable_workers_observability_tracing"


def walk(doc):
    """THE ONE WALK that IL-A and IL-N both read: (name, table) for the document, its previews block, and every
    env.<name> with its own previews block. A table the walk does not yield is one IL-A would vouch for unread."""
    out = [("top-level", doc)]
    if isinstance(doc.get("previews"), dict):
        out.append(("previews", doc["previews"]))
    envs = doc.get("env")
    for name, t in (envs.items() if isinstance(envs, dict) else ()):
        if isinstance(t, dict):
            out.append((name, t))
            if isinstance(t.get("previews"), dict):
                out.append((f"{name}.previews", t["previews"]))
    return out


def envelope_items(name, t):
    """IL-N's tests for one walked table: anything that would trace, push or tail a request envelope. Presence is
    enough for logpush, tail_consumers and logs.destinations, whatever the value (as ruled)."""
    found = set()
    obs = t.get("observability") if isinstance(t.get("observability"), dict) else {}
    traces = obs.get("traces")
    if isinstance(traces, dict) and traces.get("enabled") is True:
        found.add(f"IL-N {name} traces")
    if "logpush" in t:
        found.add(f"IL-N {name} logpush")
    if "tail_consumers" in t:
        found.add(f"IL-N {name} tail_consumers")
    logs = obs.get("logs")
    if isinstance(logs, dict) and "destinations" in logs:
        found.add(f"IL-N {name} logs.destinations")
    flags = t.get("compatibility_flags")
    if isinstance(flags, list) and TRACING_FLAG in flags:
        found.add(f"IL-N {name} compatibility_flags")
    return found


def grade_il(text):
    """The premise as findings: "IL-P", "IL-C", "IL-A <table>", "IL-N <table> <item>". Empty means all four rows pass."""
    try:
        doc = tomllib.loads(text)
    except tomllib.TOMLDecodeError:
        return {"IL-P", "IL-C", "IL-A", "IL-N"}  # nothing can be certified
    found = set()
    # IL-C reads the document DIRECTLY, never the walk: a walk that skipped a table must not also hide that it is gone.
    envs = doc.get("env") if isinstance(doc.get("env"), dict) else {}
    for e in ("staging", "production"):
        if not (isinstance(envs.get(e), dict) and isinstance(envs[e].get("observability"), dict)):
            found.add("IL-C")
    for name, t in walk(doc):
        obs = t.get("observability")
        if isinstance(obs, dict):
            logs = obs.get("logs")
            v = logs.get("invocation_logs") if isinstance(logs, dict) else None
            if v is not False:  # absent is red: Cloudflare's default is ON. The string "false" is red too.
                found.add(f"IL-A {name}")
        found |= envelope_items(name, t)
    return found


def il_red(findings):
    """The IL rows a set of findings turns red."""
    return {k for k in IL_ROWS if any(f == k or f.startswith(k + " ") for f in findings)}


def norm(text):
    """Lower case, every run of whitespace one space, so a re-wrap or a capital cannot hide a match."""
    return re.sub(r"\s+", " ", text).lower()


def rows(path):
    return [f"P1 {path}: no retired phrase is present",
            f"C1 {path}: the privacy text's anchor is present",
            f"C2 {path}: the replacement wording is present"]


def grade(texts):
    """texts: path -> raw text. Returns the set of red row labels."""
    red = set()
    for path, (anchor, required) in FILES.items():
        t = norm(texts[path])
        p1, c1, c2 = rows(path)
        if any(norm(r) in t for r in RETIRED):
            red.add(p1)
        if anchor not in t:
            red.add(c1)
        if not all(r in t for r in required):
            red.add(c2)
    return red


def read_tree():
    texts = {}
    for path in (*FILES, WRANGLER):
        try:
            with open(os.path.join(ROOT, path), encoding="utf-8", newline="") as f:
                texts[path] = f.read()
        except OSError as e:
            print(f"CANNOT READ {path}: {e.__class__.__name__}")
            sys.exit(2)
    return texts


def report(texts):
    red = grade(texts)
    for path in FILES:
        for row in rows(path):
            print(("FAIL " if row in red else "PASS ") + row)
    findings = grade_il(texts[WRANGLER])
    ilred = il_red(findings)
    for key, row in IL_ROWS.items():
        named = sorted(f for f in findings if f.startswith(key + " "))
        print(("FAIL " if key in ilred else "PASS ") + row + (f" -- {', '.join(named)}" if named else ""))
    total = sum(len(rows(p)) for p in FILES) + len(IL_ROWS)
    print(f"{total - len(red) - len(ilred)} of {total} rows pass")
    return red | ilred


def selftest():
    base = read_tree()
    readme, support, lb = "README.md", "proxy/pages/support.html", "proxy/pages/leaderboard.html"

    def with_(path, new):
        t = dict(base)
        if new == t[path]:
            raise SystemExit(f"selftest BLIND: a plant into {path} did not change the text")
        t[path] = new
        return t

    def plant(path, phrase):
        # Capitalised, with a line break and indent inside: norm() must still find it.
        words = phrase.split(" ")
        mid = max(1, len(words) // 2)
        shaped = (" ".join(words[:mid]) + "\n      " + " ".join(words[mid:])).upper()
        return with_(path, base[path] + "\n" + shaped + "\n")

    def drop(path, needle):
        n = norm(base[path])
        if needle not in n:
            raise SystemExit(f"selftest BLIND: {needle!r} is not in {path} to remove")
        return with_(path, n.replace(needle, "x"))

    cases = [("clean tree", dict(base), set())]
    for phrase in RETIRED:
        cases.append((f"README: retired {phrase!r}", plant(readme, phrase), {rows(readme)[0]}))
    cases.append(("support: a retired phrase", plant(support, RETIRED[2]), {rows(support)[0]}))
    cases.append(("leaderboard: a retired phrase", plant(lb, RETIRED[5]), {rows(lb)[0]}))
    for path in FILES:
        cases.append((f"{path}: anchor gone", drop(path, FILES[path][0]), {rows(path)[1]}))
    for path in FILES:
        cases.append((f"{path}: replacement gone", drop(path, FILES[path][1][0]), {rows(path)[2]}))

    # IL1-IL14: plants into the config's text, CRLF folded to LF. Each anchor must match exactly once, or nothing here
    # could say which table a plant landed in.
    wr = base[WRANGLER].replace("\r\n", "\n")

    def il(*pairs):
        t = wr
        for old, new in pairs:
            if t.count(old) != 1:
                raise SystemExit(f"selftest BLIND: {old[:48]!r} is in {WRANGLER} {t.count(old)} times, not once")
            t = t.replace(old, new)
        return with_(WRANGLER, t)

    def il_add(extra):
        return with_(WRANGLER, wr + "\n" + extra)

    def logs(env):
        return f"[env.{env}.observability.logs]\ninvocation_logs = false\n"

    prod_obs = "[env.production.observability]\nenabled = true\nhead_sampling_rate = 1.0\n"
    if wr.count("[env.production.observability.logs]") != 1:
        raise SystemExit(f"selftest BLIND: production's logs header is not in {WRANGLER} exactly once")
    cut = wr.index("[env.production.observability.logs]") + 10
    flag = 'compatibility_flags = ["enable_workers_observability_tracing"]\n'
    cases += [
        ("IL1: production invocation_logs = true",
         il((logs("production"), logs("production").replace("false", "true"))), {"IL-A production"}),
        ("IL2: staging invocation_logs = true",
         il((logs("staging"), logs("staging").replace("false", "true"))), {"IL-A staging"}),
        ("IL3: production's logs table removed", il((logs("production"), "")), {"IL-A production"}),
        ("IL4: production invocation_logs = \"false\", a string",
         il((logs("production"), logs("production").replace("false", '"false"'))), {"IL-A production"}),
        ("IL5: a new env with an observability table and no logs table",
         il_add("[env.canary.observability]\nenabled = true\n"), {"IL-A canary"}),
        ("IL6: a top-level observability table with no logs table",
         il_add("[observability]\nenabled = true\n"), {"IL-A top-level"}),
        ("IL7: production's observability removed, both tables",
         il((prod_obs, ""), (logs("production"), "")), {"IL-C"}),
        ("IL8: the text cut mid-table, so it does not parse",
         with_(WRANGLER, wr[:cut]), {"IL-P", "IL-C", "IL-A", "IL-N"}),
        ("IL9: production traces enabled",
         il_add("[env.production.observability.traces]\nenabled = true\n"), {"IL-N production traces"}),
        ("IL10: logpush under staging",
         il(("[env.staging]\n", "[env.staging]\nlogpush = true\n")), {"IL-N staging logpush"}),
        ("IL11: a top-level tail consumer",
         il_add('[[tail_consumers]]\nservice = "x"\n'), {"IL-N top-level tail_consumers"}),
        ("IL12: production logs.destinations",
         il((logs("production"), logs("production") + 'destinations = ["x"]\n')), {"IL-N production logs.destinations"}),
        ("IL13: production carries the tracing compatibility flag",
         il(("[env.production]\n", "[env.production]\n" + flag)), {"IL-N production compatibility_flags"}),
        ("IL14: a previews observability table with no logs table",
         il_add("[previews.observability]\nenabled = true\n"), {"IL-A previews"}),
    ]

    off = 0
    for name, texts, want in cases:
        got = grade(texts) | grade_il(texts[WRANGLER])
        ok = got == want
        off += not ok
        print(f"{'as predicted' if ok else 'OFF'}: {name}: red {len(got)}, predicted {len(want)}")
        if not ok:
            for row in sorted(got ^ want):
                print(f"    {'unexpected' if row in got else 'missing'}: {row}")
    print(f"selftest: {len(cases) - off} of {len(cases)} cases as predicted")
    return 3 if off else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    if sys.argv[1:]:
        print("usage: check_privacy_text.py [--selftest]")
        sys.exit(2)
    sys.exit(1 if report(read_tree()) else 0)


if __name__ == "__main__":
    main()
