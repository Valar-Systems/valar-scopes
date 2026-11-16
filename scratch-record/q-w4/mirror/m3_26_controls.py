# SCRATCH (map-server): m3_26.py's controls, red first, on COPIES of tree B's manifest under mirror/m3-26-controls/ (absent before).
# Each plant is proved applied (the copy's sha256 differs from the original's) before the checker reads it.
#   K1 the original, unmodified      -> exit 0, "M-3: AS PREDICTED"
#   K2 one character of one note     -> exit 1, the notes row OFF
#   K3 the builder's last character  -> exit 1, the builder row OFF
#   K4 "ftp2.census.gov" written into one note -> exit 1, the ftp2 row OFF (and the notes row OFF)
#   python -X utf8 -I m3_26_controls.py
import hashlib, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCR = os.path.dirname(HERE)
SRC = os.path.join(os.path.dirname(SCR), "bm-cache-qw4", "dry-run", "store", "manifest.json")
ROOT = os.path.join(HERE, "m3-26-controls")
sha = lambda b: hashlib.sha256(b).hexdigest()[:12]


def run(p):
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", os.path.join(HERE, "m3_26.py"), p], capture_output=True, text=True, encoding="utf-8")
    return r.returncode, r.stdout, r.stderr


def main():
    if os.path.exists(ROOT):
        print(f"refused: {ROOT} exists"); return 2
    os.makedirs(ROOT)
    orig = open(SRC, "rb").read()
    m = json.loads(orig)
    dump = lambda o: (json.dumps(o, indent=1, ensure_ascii=False) + "\n").encode("utf-8")
    if dump(m) != orig:
        print("INSTRUMENT: the manifest does not re-serialise byte-equal; plants would change more than intended"); return 3
    cases = []
    k2 = json.loads(orig); n = k2["notes"][0]; k2["notes"][0] = n[:-1] + ("X" if n[-1] != "X" else "Y"); cases.append(("K2", dump(k2), 1, "notes exactly the six lines, byte order: OFF"))
    k3 = json.loads(orig); b = k3["builder"]; k3["builder"] = b[:-1] + ("0" if b[-1] != "0" else "1"); cases.append(("K3", dump(k3), 1, "builder: OFF"))
    k4 = json.loads(orig); k4["notes"][0] = k4["notes"][0] + " ftp2.census.gov"; cases.append(("K4", dump(k4), 1, "no ftp2.census.gov in the bytes: OFF"))
    good = 0
    p = os.path.join(ROOT, "K1-manifest.json"); open(p, "wb").write(orig)
    rc, out, err = run(p)
    ok = rc == 0 and "M-3: AS PREDICTED" in out and err == ""
    good += ok
    print(f"K1 original: exit {rc}; {'AS PREDICTED' if ok else 'NOT AS PREDICTED'}\n" + out)
    for cid, body, want, token in cases:
        p = os.path.join(ROOT, f"{cid}-manifest.json"); open(p, "wb").write(body)
        applied = sha(open(p, "rb").read()) != sha(orig)
        rc, out, err = run(p)
        ok = applied and rc == want and token in out and err == ""
        good += ok
        print(f"{cid}: plant applied {applied}; exit {rc} (predicted {want}); '{token}' {'found' if token in out else 'NOT found'}; "
              f"{'AS PREDICTED' if ok else 'NOT AS PREDICTED'}")
    print(f"M3-26 CONTROLS: {good} of 4 {'AS PREDICTED' if good == 4 else 'NOT AS PREDICTED: the checker is not used'}")
    return 0 if good == 4 else 1


if __name__ == "__main__":
    sys.exit(main())
