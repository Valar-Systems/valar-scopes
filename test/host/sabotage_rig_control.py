"""Q70 (b)'s control (docs/weatherscope-stage3.md A12.88): a driver keeps going past a plant it cannot grade.

The real sabotage_wx_logic.py runs three times, nothing copied or edited: clean, then with SABOTAGE_FORCE_RIG naming its
first plant (S-R1) and its last (S-X). Each forced run must exit 2, name exactly that plant as the RIG on its last line,
and print every OTHER plant's line exactly as the clean run does -- so the plants after a RIG are still graded. The clean
run must exit 0 with no RIG.

    python -I test/host/sabotage_rig_control.py
Exit 0 all as above; 1 not.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DRIVER = os.path.join(HERE, "sabotage_wx_logic.py")
PLANT = re.compile(r"^(S-[A-Z0-9]+[a-z]?\d*): ")
FIRST, LAST = "S-R1", "S-X"


def run(force):
    env = dict(os.environ)
    env.pop("SABOTAGE_FORCE_RIG", None)
    if force:
        env["SABOTAGE_FORCE_RIG"] = force
    r = subprocess.run([sys.executable, DRIVER], capture_output=True, text=True, env=env)
    return r.returncode, r.stdout.splitlines()


def plants(lines):
    """Each plant's lines, in order, keyed by plant name."""
    out = {}
    for line in lines:
        m = PLANT.match(line)
        if m:
            out.setdefault(m.group(1), []).append(line)
    return out


def main():
    ok = True
    rc, clean = run(None)
    cp = plants(clean)
    good = rc == 0 and not any(l.startswith("the RIG:") for l in clean) and FIRST in cp and LAST in cp
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} RIG-CTL clean: exit {rc}, {len(cp)} plants graded, no RIG")
    for force in (FIRST, LAST):
        rc, lines = run(force)
        fp = plants(lines)
        others = {k: v for k, v in fp.items() if k != force}
        same = others == {k: v for k, v in cp.items() if k != force}
        named = bool(lines) and lines[-1] == f"the RIG: {[force]!r}"
        recorded = any(l == f"{force}: the RIG -- recorded; the next plant runs" for l in lines)
        good = rc == 2 and named and recorded and same and len(others) == len(cp) - 1
        ok &= good
        print(f"  {'ok  ' if good else 'FAIL'} RIG-CTL {force} forced: exit {rc} (want 2); named alone on the last line: "
              f"{named}; the other {len(others)} plants' lines equal the clean run's: {same}")
    print("RIG-CTL " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
