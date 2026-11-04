"""TX2 and S-TXRECON (docs/test-identity.md, item 4): reconcile-fleet.py leaves the production checks' TEST identity
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

print("SELFTEST PASSED" if not off else "SELFTEST: %d row(s) off" % off)
sys.exit(1 if off else 0)
