"""The R0/R0b fixtures' sabotages (docs/weatherscope-stage3.md A12.18.3): each plant goes into a COPY of
test/fixtures/wx/r0/, never the real one, and must turn its check red in exactly the frozen way.

    python -I tools/weatherscope/screens/sabotage_r0fx.py

  S-FX-PIN      one pixel of screen-proposed-dry.png changed; the pin left as it was
                -> R0 refused by the pin (exit 2), before any comparison
  S-FX-PIXEL    the same pixel changed AND its pin updated, so only R0a can catch it
                -> R0a fails on screen-proposed-dry only, with exactly 1 unexplained pixel, at the changed position
  S-FX-PIXEL-B  one pixel of screen-bend-B.png changed and its pin updated
                -> R0a' fails on B only, with exactly 1 pixel, at the changed position
Prints no coordinate beyond the planted pixel's screen position.
"""
import hashlib, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FX = os.path.join(ROOT, "test", "fixtures", "wx", "r0")
PX = (60, 100)


def flip_pixel(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    r, g, b = im.getpixel(PX)
    im.putpixel(PX, (r ^ 1, g, b))
    im.save(path)
    assert Image.open(path).convert("RGB").getpixel(PX) == (r ^ 1, g, b), "the plant did not take"


def repin(fx, name):
    p = os.path.join(fx, "MANIFEST.json")
    man = json.load(open(p, encoding="utf-8"))
    man["files"][name] = hashlib.sha256(open(os.path.join(fx, name), "rb").read()).hexdigest()
    open(p, "w", encoding="utf-8", newline="\n").write(json.dumps(man, indent=1, sort_keys=True) + "\n")


def run(cmd):
    r = subprocess.run([sys.executable, "-I", "-B", *cmd], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def main():
    before = {n: hashlib.sha256(open(os.path.join(FX, n), "rb").read()).hexdigest() for n in os.listdir(FX)}
    ref = os.path.join(HERE, "ref35.py")
    geo = os.path.join(HERE, "geo35.py")
    ok = True
    with tempfile.TemporaryDirectory() as td:
        # S-FX-PIN
        fx = os.path.join(td, "pin")
        shutil.copytree(FX, fx)
        flip_pixel(os.path.join(fx, "screen-proposed-dry.png"))
        rc, out, err = run([ref, "--r0", fx])
        good = rc == 2 and "R0 REFUSED" in out and "screen-proposed-dry.png" in out
        print(f"S-FX-PIN: exit {rc}; {'refused by the pin' if 'R0 REFUSED' in out else 'NOT refused'} -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        # S-FX-PIXEL
        fx = os.path.join(td, "pixel")
        shutil.copytree(FX, fx)
        flip_pixel(os.path.join(fx, "screen-proposed-dry.png"))
        repin(fx, "screen-proposed-dry.png")
        rc, out, err = run([ref, "--r0", fx])
        fails = re.findall(r"^\s+FAIL (\S+): (\d+) of 57600 equal; UNEXPLAINED pixels \(first 10, x,y\): (\[.*?\])", out, re.M)
        good = (rc == 1 and len(fails) == 1 and fails[0][0] == "screen-proposed-dry.png" and fails[0][1] == "57599"
                and fails[0][2] == str([PX]) and out.count("FAIL") == 1)
        print(f"S-FX-PIXEL: exit {rc}; R0a fails on {[(f[0], f[1], f[2]) for f in fails]} -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
        # S-FX-PIXEL-B
        fx = os.path.join(td, "pixelB")
        shutil.copytree(FX, fx)
        flip_pixel(os.path.join(fx, "screen-bend-B.png"))
        repin(fx, "screen-bend-B.png")
        rc, out, err = run([geo, "--r0b-fixture", fx])
        fails = re.findall(r"^\s+FAIL screen-bend-([ABC])\.png: (\d+) of 57600 equal; UNEXPLAINED \(first 10, x,y\): (\[.*?\])", out, re.M)
        good = (rc == 1 and len(fails) == 1 and fails[0][0] == "B" and fails[0][1] == "57599" and fails[0][2] == str([PX])
                and out.count("FAIL") == 1)
        print(f"S-FX-PIXEL-B: exit {rc}; R0a' fails on {[(f[0], f[1], f[2]) for f in fails]} -> "
              f"{'AS PREDICTED' if good else 'OFF-PREDICTION'}")
        ok &= good
    after = {n: hashlib.sha256(open(os.path.join(FX, n), "rb").read()).hexdigest() for n in os.listdir(FX)}
    print(f"the real fixtures unchanged: {before == after}")
    return 0 if ok and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
