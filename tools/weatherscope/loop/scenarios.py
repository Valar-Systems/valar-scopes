"""3.3c's scenario choices (docs/weatherscope-stage3.md A8.10), read off the 15 loop frames by the frozen rules:

  approaching  the LATEST frame whose nearest echo is <= 15 mi from the loop crop's centre
  dry          the LATEST frame whose nearest echo is beyond 15 mi, or that has none
  here now     the approaching frame (+ a synthetic "-RA" five minutes before its clock)
  warning      the approaching frame (+ a synthetic SVR warning; storm position = its nearest echo cell)
  If a rule finds no frame: STOP. Nothing is substituted.

    python -I tools/weatherscope/loop/scenarios.py           prints the per-frame table and the choices
    python -I tools/weatherscope/loop/scenarios.py --write   also writes test/fixtures/wx/loop/scenarios.ref.json
    python -I tools/weatherscope/loop/scenarios.py --check   exit 0 only if the committed file equals a fresh computation

The metric is ring_ref.py's (planar, 111 km/deg, longitude scaled by the cosine of the centre's latitude), and the
decoder is ring_ref.py's own Python WXC1 decoder, loaded from its file -- not WxCrop.h. TWO CONTROLS, both before any
distance is believed:
  (a) the anchor: this tool's distance loop, run on kdfw-60mi.wxc, must reproduce kdfw-60mi.ring.txt;
  (b) every loop frame's decoded cells must hash to its MANIFEST.json codesSha256.
Prints distances, bearings and frame names only -- no coordinate. The reference file holds none either.
"""
import hashlib
import importlib.util
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
WX = os.path.join(ROOT, "test", "fixtures", "wx")
LOOP = os.path.join(WX, "loop")
OUT = os.path.join(LOOP, "scenarios.ref.json")
RING_MI = 15.0
KM_PER_DEG, KM_PER_MI = 111.0, 1.609344
COMPASS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]

spec = importlib.util.spec_from_file_location("ring_ref", os.path.join(WX, "ring_ref.py"))
ring_ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ring_ref)


def nearest(raw):
    """(distance mi, bearing deg, cells tied at the minimum, their compass points) from the crop's centre; None if dry."""
    lat_nw, lon_nw, cell_lat, cell_lon, rows, cols, codes = ring_ref.decode(raw)
    home_lat = lat_nw - (rows - 1) / 2 * cell_lat
    home_lon = lon_nw + (cols - 1) / 2 * cell_lon
    kx, ky = KM_PER_DEG * math.cos(math.radians(home_lat)), KM_PER_DEG
    best, at = None, []
    for r in range(rows):
        dy = (lat_nw - r * cell_lat - home_lat) * ky
        for c in range(cols):
            if 1 <= codes[r * cols + c] <= 13:
                dx = (lon_nw + c * cell_lon - home_lon) * kx
                d2 = dx * dx + dy * dy
                if best is None or d2 < best - 1e-12:
                    best, at = d2, [(dx, dy)]
                elif abs(d2 - best) <= 1e-12:
                    at.append((dx, dy))
    if best is None:
        return codes, None
    bearings = [(math.degrees(math.atan2(dx, dy)) + 360.0) % 360.0 for dx, dy in at]
    points = sorted({COMPASS[int(math.floor((b + 22.5) / 45.0)) % 8] for b in bearings})
    return codes, (math.sqrt(best) / KM_PER_MI, bearings[0], len(at), points)


def until_local(epoch):
    """The frozen expiry, frame clock + 45 min, read on the alert's own -05:00 clock: 'UNTIL h:mm AM/PM'."""
    t = epoch + 45 * 60 - 5 * 3600
    h, m = (t // 3600) % 24, (t // 60) % 60
    return f"UNTIL {12 if h % 12 == 0 else h % 12}:{m:02d} {'AM' if h < 12 else 'PM'}", t


def main(argv):
    # (a) the anchor
    _, k = nearest(open(os.path.join(WX, "kdfw-60mi.wxc"), "rb").read())
    want = open(os.path.join(WX, "kdfw-60mi.ring.txt")).read().strip()
    if f"{k[0]:.6f}" != want:
        sys.exit(f"ANCHOR FAILED: this tool reads {k[0]:.6f} mi on kdfw-60mi.wxc, ring_ref.py committed {want}")
    print(f"anchor: kdfw-60mi.wxc reads {k[0]:.6f} mi, equal to kdfw-60mi.ring.txt")

    frames = json.load(open(os.path.join(LOOP, "MANIFEST.json")))["frames"]
    rows = []
    for f in frames:
        codes, n = nearest(open(os.path.join(LOOP, f["name"]), "rb").read())
        got = hashlib.sha256(codes).hexdigest()
        if got != f["codesSha256"]:   # (b)
            sys.exit(f"CONTROL FAILED: {f['name']} decodes to {got[:12]}, MANIFEST says {f['codesSha256'][:12]}")
        rows.append((f, n))
        d = "none" if n is None else f"{n[0]:9.4f} mi  bearing {n[1]:6.1f} deg  ({n[2]} cell(s) at the minimum: {','.join(n[3])})"
        print(f"  {f['name']}  {'<=15' if n and n[0] <= RING_MI else ' >15'}  nearest echo {d}")
    print("control: all 15 frames decode to their MANIFEST codesSha256")

    near = [(f, n) for f, n in rows if n is not None and n[0] <= RING_MI]
    far = [(f, n) for f, n in rows if n is None or n[0] > RING_MI]
    if not near or not far:
        sys.exit(f"STOP (A8.10): {len(near)} frame(s) within 15 mi, {len(far)} beyond. A rule found no frame; nothing is substituted.")
    (fa, na), (fd, nd) = near[-1], far[-1]
    if len(na[3]) != 1:
        sys.exit(f"STOP: the approaching frame's nearest echo is {na[2]} tied cells in {na[3]}; the storm's bearing is ambiguous")
    text, _ = until_local(fa["frameEpoch"])
    ref = {
        "rule": "A8.10: approaching = latest frame with nearest echo <= 15 mi; dry = latest beyond 15 mi or none",
        "metric": "ring_ref.py: planar, 111 km/deg, lon x cos(centre lat); centre = the crop's middle cell position",
        "approaching": {"frame": fa["name"], "frameEpoch": fa["frameEpoch"], "nearestEchoMi": round(na[0], 6)},
        "dry": {"frame": fd["name"], "frameEpoch": fd["frameEpoch"],
                "nearestEchoMi": None if nd is None else round(nd[0], 6)},
        "hereNow": {"frame": fa["name"], "obsOffsetS": -300, "wx": "-RA", "synthetic": True},
        "warning": {"frame": fa["name"], "synthetic": True,
                    "stormDistanceMi": round(na[0], 6), "stormBearingDeg": round(na[1], 4), "stormCompass": na[3][0],
                    "tiedCells": na[2], "until": text},
        "framesWithin15": [f["name"] for f, _ in near],
    }
    print(f"approaching: {fa['name']}  ({na[0]:.6f} mi, {na[3][0]})")
    print(f"dry:         {fd['name']}  ({'no echo' if nd is None else f'{nd[0]:.6f} mi'})")
    print(f"warning:     storm {na[0]:.6f} mi {na[3][0]} ({na[1]:.2f} deg), {na[2]} tied cell(s); {text}")
    text_out = json.dumps(ref, indent=1) + "\n"
    if argv[1:] == ["--check"]:
        committed = open(OUT).read() if os.path.exists(OUT) else ""
        ok = committed == text_out
        print("committed scenarios.ref.json " + ("matches" if ok else "DIFFERS"))
        return 0 if ok else 1
    if argv[1:] == ["--write"]:
        open(OUT, "w", newline="\n").write(text_out)
        print(f"wrote {os.path.relpath(OUT, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
