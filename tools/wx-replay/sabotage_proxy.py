"""Proves proxy.py's selftest can fail (docs/wx-device.md, the replay route). Each plant goes into a COPY of proxy.py
in a temporary folder: the plant is asserted to have applied (needle present once, copy changed), the copy's
--selftest must exit non-zero, and the real file's sha256 is checked unchanged at the end.
    python -I tools/wx-replay/sabotage_proxy.py"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REAL = os.path.join(HERE, "proxy.py")

PLANTS = [
    ("S-xff: the forward adds X-Forwarded-For",
     '    return [(n, host) if n.lower() == "host" else (n, v) for n, v in items if n.lower() not in drop]\n',
     '    return [(n, host) if n.lower() == "host" else (n, v) for n, v in items if n.lower() not in drop]'
     ' + [("X-Forwarded-For", "203.0.113.9")]\n'),
    ("S-host: Host is forwarded unrenamed",
     '    return [(n, host) if n.lower() == "host" else (n, v) for n, v in items if n.lower() not in drop]\n',
     '    return [(n, v) for n, v in items if n.lower() not in drop]\n'),
    ("S-conn: the fields Connection names are kept",
     '            names |= {t.strip().lower() for t in v.split(",") if t.strip()}\n',
     '            pass\n'),
    ("S-server: the reply adds Server and Date",
     '        self.send_response_only(status, reason)   # send_response would add Server and Date\n',
     '        self.send_response(status, reason)\n'),
    ("S-logq: the log gains a query field, and its guard is gone",
     '        line = f"{utc()} {route} {kind} {status} {nbytes}"\n'
     '        if not LOG_LINE.match(line):              # a field outside the fixed set is never written\n'
     '            raise ValueError("log line outside the fixed fields")\n',
     '        line = f"{utc()} {route} {kind} {status} {nbytes} lat=1"\n'),
    ("S-allow: every client is answered",
     '        if self.client_address[0] != P.allow_ip:\n',
     '        if False:\n'),
    ("S-scan: the scan never finds the key",
     '            key |= any(n in data for n in k)\n',
     '            key |= False\n'),
    ("S-instr: no key seen reads clean, not instrument",
     '        if not self.keys:\n            return f"SCAN instrument',
     '        if False:\n            return f"SCAN instrument'),
    ("S-synth-leak: K6's own hexes forwarded, not answered",
     '            if e is not None:   # exactly the feed\'s own hexes; any other is forwarded below\n',
     '            if False:\n'),
]
# Plants predicted to turn EXACTLY one named case red (docs/wx-device.md, P4); the others need only turn it red.
EXACT = {"S-synth-leak: K6's own hexes forwarded, not answered":
         "nothing forwarded for a K6 hex: its enrich is answered locally"}


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    before = sha(REAL)
    src = open(REAL, encoding="utf-8").read()
    bad = 0
    with tempfile.TemporaryDirectory(prefix="wx-replay-sabotage-") as tmp:
        copy = os.path.join(tmp, "proxy.py")
        shutil.copyfile(os.path.join(HERE, "synth_feed.py"), os.path.join(tmp, "synth_feed.py"))   # proxy.py imports it
        open(copy, "w", encoding="utf-8", newline="\n").write(src)
        r = subprocess.run([sys.executable, "-I", copy, "--selftest"], capture_output=True, text=True, timeout=120)
        ctrl = r.returncode == 0
        print(f"  {'ok ' if ctrl else 'BAD'}  CONTROL: the unplanted copy's selftest passes (exit {r.returncode})")
        bad += 0 if ctrl else 1
        for name, needle, plant in PLANTS:
            if src.count(needle) != 1:
                print(f"  BAD  {name}: the needle is not in proxy.py exactly once, so the plant cannot apply")
                bad += 1
                continue
            planted = src.replace(needle, plant)
            assert planted != src
            open(copy, "w", encoding="utf-8", newline="\n").write(planted)
            assert open(copy, encoding="utf-8").read() == planted
            r = subprocess.run([sys.executable, "-I", copy, "--selftest"], capture_output=True, text=True, timeout=120)
            fails = [l.strip()[6:] for l in r.stdout.splitlines() if l.strip().startswith("FAIL")]
            red = r.returncode != 0 and (name not in EXACT or fails == [EXACT[name]])
            why = (f"{len(fails)} FAIL: {fails[0]}" + (" ..." if len(fails) > 1 else "")) if fails else \
                (r.stderr.strip().splitlines() or ["no output"])[-1][:100]
            print(f"  {'ok ' if red else 'BAD'}  {name}: selftest exit {r.returncode} ({why})")
            bad += 0 if red else 1
    after = sha(REAL)
    same = after == before
    print(f"  {'ok ' if same else 'BAD'}  the real proxy.py is unchanged (sha256 {after[:12]})")
    bad += 0 if same else 1
    print(f"sabotage: {'every plant turned the selftest red' if not bad else str(bad) + ' problem(s)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
