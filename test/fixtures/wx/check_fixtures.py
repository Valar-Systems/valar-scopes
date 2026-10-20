"""Second reference for the WXC1 fixtures: decode each one with the spike's INDEPENDENT Python decoder
(decode() in tools/wx/a3_check.py on spike/wx-overlay, written from the format rather than the TS code)
and compare with the sha256 that crop.ts's decodeCrop recorded in MANIFEST.json.

    python -I test/fixtures/wx/check_fixtures.py <spike-checkout> test/fixtures/wx

a3_check.py imports eccodes at module level for its own checks; decode() does not use it, so a stub stands
in. CONTROL first: one cell's code changed in the KDFW crop must come back as a MISMATCH, or no result
below can be believed. Exit 0 only if the control fires and every fixture agrees.
"""
import hashlib
import importlib.util
import json
import os
import sys
import tempfile
import types


def load_decoder(spike):
    sys.modules.setdefault("eccodes", types.ModuleType("eccodes"))      # unused by decode()
    spec = importlib.util.spec_from_file_location("a3_check", os.path.join(spike, "tools", "wx", "a3_check.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.decode


def py_verdict(decode, path):
    """('codes', sha256) when the Python decoder accepts the crop, ('refuse', error type) when it raises."""
    try:
        codes = decode(path)[5]
    except Exception as e:          # the Python reference refuses by raising (assert / IndexError)
        return ("refuse", type(e).__name__)
    return ("codes", hashlib.sha256(codes.tobytes()).hexdigest())


def main(spike, folder):
    decode = load_decoder(spike)
    man = json.load(open(os.path.join(folder, "MANIFEST.json"), encoding="utf-8"))

    # CONTROL: change one code in the body of the KDFW crop (a run byte's high nibble) -> must MISMATCH
    src = open(os.path.join(folder, "kdfw-60mi.wxc"), "rb").read()
    want = next(f for f in man["fixtures"] if f["name"] == "kdfw-60mi.wxc")["expect"]["codesSha256"]
    b = bytearray(src)
    i = 32
    while (b[i] & 15) == 0:         # walk run by run to the first SHORT run (1-15): its length is untouched
        i += 1
        while b[i] & 0x80:          # skip the long run's LEB128 length bytes
            i += 1
        i += 1
    b[i] = ((((b[i] >> 4) + 1) % 14) << 4) | (b[i] & 15)
    ctl = os.path.join(folder, "_control.tmp")
    open(ctl, "wb").write(bytes(b))
    try:
        kind, got = py_verdict(decode, ctl)
    finally:
        os.remove(ctl)
    control_ok = kind == "codes" and got != want
    print(f"control (one code changed): {'MISMATCH, as it must' if control_ok else 'NOT DETECTED -- results below are not trustworthy'}")

    bad = 0
    for f in man["fixtures"]:
        kind, got = py_verdict(decode, os.path.join(folder, f["name"]))
        if "refuse" in f["expect"]:
            ok = kind == "refuse"
            note = f"crop.ts refuses ({f['expect']['refuse']}); Python {'refuses (' + got + ')' if ok else 'ACCEPTS'}"
        else:
            ok = kind == "codes" and got == f["expect"]["codesSha256"]
            note = f"codes sha256 {'agree' if ok else 'DIFFER'} ({f['expect']['codesSha256'][:12]})"
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} {f['name']:28s} {note}")
    print(f"{len(man['fixtures']) - bad}/{len(man['fixtures'])} fixtures agree between crop.ts and a3_check.py")

    # The round-trip corpus: good crops must decode to the encoder's own cells; differing twins must be refused.
    cor = man["corpus"]
    def records(name):
        b = open(os.path.join(folder, name), "rb").read()
        i, out = 0, []
        while i < len(b):
            n = int.from_bytes(b[i:i + 4], "little")
            out.append(b[i + 4:i + 4 + n])
            i += 4 + n
        return out
    good, sab = records(cor["good"]), records(cor["sabotaged"])
    ok_good = ok_sab = 0
    with tempfile.TemporaryDirectory() as td:
        for k, (meta, g, s) in enumerate(zip(cor["records"], good, sab)):
            gp, sp = os.path.join(td, f"g{k}.wxc"), os.path.join(td, f"s{k}.wxc")
            open(gp, "wb").write(g)
            open(sp, "wb").write(s)
            kind, got = py_verdict(decode, gp)
            ok_good += kind == "codes" and got == meta["cellsSha256"]
            if meta["twinDiffers"]:
                ok_sab += py_verdict(decode, sp)[0] == "refuse"
    n_twins = sum(1 for m in cor["records"] if m["twinDiffers"])
    print(f"corpus: {ok_good}/{len(cor['records'])} good crops round-trip to the encoder's cells; "
          f"{ok_sab}/{n_twins} sabotaged twins refused")
    corpus_ok = ok_good == len(cor["records"]) == len(good) and ok_sab == n_twins
    return 0 if control_ok and not bad and corpus_ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
