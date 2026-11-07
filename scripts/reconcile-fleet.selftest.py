"""TX2 and S-TXRECON (docs/test-identity.md, item 4), then R1 and its sabotages (docs/ota-result-reader.md): reconcile-fleet.py leaves the production checks' TEST identity
out of 'KV ONLY'. The script is loaded as a module and run with its KV and CSV reads replaced by a fixture: two units
in the manufacturing record and in KV, plus the test identity in KV only (it has a fw: row, as every smoke run makes,
and no CSV line). EXAMPLE ids, built at run time, so no 16-hex literal sits in the repo; nothing touches KV.

S-TXRECON runs the same fixture through a copy of the source with the exclusion line removed (in memory; the file is
never written), and must report exactly one 'KV ONLY: unaccounted-for unit' and exit 1.

    python scripts/reconcile-fleet.selftest.py
"""
import contextlib
import io
import os
import sys
import tempfile
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = (HERE / "reconcile-fleet.py").read_text(encoding="utf-8")
A, B, T = "a1" * 8, "b2" * 8, "c3" * 8  # two units, and the test identity (EXAMPLE)
EXCLUSION = "    kv_ids = [d for d in kv_ids if d not in test_ids]  # the test identity is not a unit\n"


def run(source: str):
    mod = types.ModuleType("reconcile_fleet_under_test")
    mod.__file__ = str(HERE / "reconcile-fleet.py")
    exec(compile(source, mod.__file__, "exec"), mod.__dict__)
    mod.kv_list = lambda prefix, env, errfile: [A, B, T]  # the fixture's KV listing
    mod.read_csv = lambda path: {A: {}, B: {}}  # the fixture's manufacturing record
    mod.report_instruments = lambda *a, **k: []
    out, code = io.StringIO(), None
    argv = sys.argv
    sys.argv = ["reconcile-fleet.py", "--no-detail", "--log", "fixture.csv"]
    try:
        with contextlib.redirect_stdout(out):
            mod.main()
    except SystemExit as e:
        code = e.code
    finally:
        sys.argv = argv
    return out.getvalue(), code


with tempfile.TemporaryDirectory() as d:
    tokens = Path(d) / "tokens"
    tokens.write_text("# EXAMPLE token file\nkv-read=EXAMPLE-not-a-token\ntest-device=%s\n" % T, encoding="utf-8")
    os.environ["BLIPSCOPE_TOKEN_FILE"] = str(tokens)
    off = 0

    text, code = run(SRC)
    ok = code == 0 and "kv-only=0" in text and T not in text
    off += not ok
    print("TX2: %s -- exit %s; %s; the test id printed: %s"
          % ("as predicted" if ok else "OFF-PREDICTION", code,
             next((l.strip() for l in text.splitlines() if l.startswith("SUMMARY")), "(no summary)"), T in text))

    if SRC.count(EXCLUSION) != 1:
        off += 1
        print("S-TXRECON: BLIND -- the exclusion line was not found exactly once")
    else:
        text, code = run(SRC.replace(EXCLUSION, "    pass  # SABOTAGE S-TXRECON: the exclusion removed\n"))
        ok = code == 1 and "kv-only=1" in text
        off += not ok
        print("S-TXRECON: %s -- exit %s; %s"
              % ("as predicted" if ok else "OFF-PREDICTION", code,
                 next((l.strip() for l in text.splitlines() if l.startswith("SUMMARY")), "(no summary)")))


# --- R1 and its sabotages: A3.1's reader (docs/ota-result-reader.md) -------------------------------------------------
# The block's functions are run on a fixture: no KV, no Analytics Engine, no network. EXAMPLE ids, built at run time.
BEHIND = {n: ("b%d" % n) * 8 for n in range(1, 7)}
CUR1, CUR2, AHEAD, NOFW = "e1" * 8, "e2" * 8, "e3" * 8, "e4" * 8
ROWS = {BEHIND[1]: {"fw": 18}, BEHIND[2]: {"fw": 18}, BEHIND[3]: {"fw": 17}, BEHIND[4]: {"fw": 18}, BEHIND[5]: {"fw": 16},
        BEHIND[6]: {"fw": 18}, CUR1: {"fw": 19}, CUR2: {"fw": 19}, AHEAD: {"fw": 20}, NOFW: {}}
POINTS = [(BEHIND[1], "ok", "2026-11-01 10:00:00"), (BEHIND[2], "refused-foreign", "2026-11-01 10:00:00"),
          (BEHIND[3], "fail--106", "2026-11-01 10:00:00"), (BEHIND[5], "weird-token", "2026-11-01 10:00:00"),
          (BEHIND[6], "refused-no-record", "2026-10-01 10:00:00"), (BEHIND[6], "ok", "2026-11-02 10:00:00"),
          (CUR1, "refused-bad-record", "2026-11-01 10:00:00")]
WANT = {"ok": 2, "refused-foreign": 1, "fail--106": 1, "unrecognised": 1, "no ota point": 1}


def load(source: str):
    mod = types.ModuleType("reconcile_fleet_r1")
    mod.__file__ = str(HERE / "reconcile-fleet.py")
    exec(compile(source, mod.__file__, "exec"), mod.__dict__)
    return mod


def r1(mod) -> dict:
    """Every R1 row, graded. True = as predicted."""
    import re as _re
    res = {}
    b = mod.ota_breakdown(19, ROWS, POINTS)
    lines, warn = mod.ota_lines(19, ROWS, POINTS)
    text = "\n".join(lines)
    res["R1a"] = (b["behind"] == 6 and b["total"] == 10 and b["buckets"] == WANT and "sum 6 = behind 6" in text
                  and not warn)
    res["R1b"] = "refused-foreign 1" in text
    only6 = mod.ota_breakdown(19, {BEHIND[6]: ROWS[BEHIND[6]]}, [x for x in POINTS if x[0] == BEHIND[6]])
    res["R1c"] = only6["buckets"] == {"ok": 1}
    d_ok = True
    for v in ("", "v19", "0"):
        lv, wv = mod.ota_lines(mod.parse_version(v), ROWS, POINTS)
        d_ok &= bool(wv) and not any(_re.search(r"published \d+: behind", x) for x in lv)
    res["R1d"] = d_ok
    le, we = mod.ota_lines(19, ROWS, None, "EXAMPLE reason")
    te = "\n".join(le)
    res["R1e"] = "behind 6" in te and "results unread" in te and bool(we)
    every = text + "\n" + te
    res["R1f"] = not any(d in every for d in ROWS) and "behind 6" in text   # CONTROL: the block is there
    res["R1g"] = "refused-bad-record" not in b["buckets"] and "refused-bad-record" not in text
    return res


R1_PLANTS = [
    ("S-R1drop  tokens of no known shape dropped", ["R1a"],
     "                tok = UNRECOGNISED\n", "                continue  # SABOTAGE S-R1drop\n", "SUM MISMATCH"),
    ("S-R1first the oldest point wins", ["R1a", "R1c"],
     "        if dev not in newest or t > newest[dev][1]:\n", "        if dev not in newest or t < newest[dev][1]:  # SABOTAGE\n", None),
    ("S-R1zero  an unreadable version.txt reads as 0", ["R1d"],
     '    return int(s) if re.fullmatch(r"[1-9]\\d{0,5}", s) else None\n',
     '    return int(s) if re.fullmatch(r"[1-9]\\d{0,5}", s) else 0  # SABOTAGE\n', None),
    ("S-R1id    the block prints the behind devices' ids", ["R1f"],
     '    out.append("    covers radar devices on the cloud feed only (A3.2)',
     '    out.append("    " + " ".join(sorted(rows)))  # SABOTAGE S-R1id\n    out.append("    covers radar devices on the cloud feed only (A3.2)', None),
]

graded = r1(load(SRC))
red = sorted(k for k, v in graded.items() if not v)
ok = not red
off += not ok
print("R1 (the real source): %s -- %d rows, red %s" % ("as predicted" if ok else "OFF-PREDICTION", len(graded), red or "none"))
for name, want, old, new, must in R1_PLANTS:
    if SRC.count(old) != 1:
        off += 1
        print("%s: BLIND -- its anchor was found %d times" % (name, SRC.count(old)))
        continue
    planted = SRC.replace(old, new)
    assert planted != SRC, name
    pm = load(planted)
    got = sorted(k for k, v in r1(pm).items() if not v)
    shown = "\n".join(pm.ota_lines(19, ROWS, POINTS)[0])
    ok = got == sorted(want) and (must is None or must in shown)
    off += not ok
    print("%s: %s -- red %s (want %s)%s" % (name, "as predicted" if ok else "OFF-PREDICTION", got or "none", sorted(want),
                                            "; %s shown: %s" % (must, must in shown) if must else ""))

print("SELFTEST PASSED" if not off else "SELFTEST: %d row(s) off" % off)
sys.exit(1 if off else 0)
