"""Weatherscope Stage 2, step 2.0: SeamlessHSR vs the composite (docs/weatherscope-stage2.md + addendum 1).

    python -I check.py <scratchpad> run
Every frame: fetch both products' nearest files, decode in memory (frame.ts), render the 240x240 screen
(render_ref.py, Stage 1's reference), then the anchor, the controls, and the measures. Prints ICAO codes,
times, counts and shares; never a coordinate.
"""
import json, os, subprocess, sys
from datetime import datetime, timedelta
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import fetch

SP = sys.argv[1]
SPIKE = os.path.join(SP, "spike-a5db678")
AIRPORTS = os.path.join(SPIKE, "tools", "wx", "airports-a2.txt")
# The renderer is PINNED: run 1 used render_ref.py sha256 450e01cc4ff6... (#434, now main 122e118). A branch
# switch in the clone once swapped in an older copy with no --out (2026-10-20), so the copy is fixed and checked.
RENDER = os.path.join(SP, "pinned", "render_ref.py")
import hashlib as _h
assert _h.sha256(open(RENDER, "rb").read()).hexdigest().startswith("450e01cc4ff6"), "render_ref.py is not the run-1 version"
DATA = os.path.join(SP, "wxdata2")
THRESH = (1, 2, 4)                       # code >= 1 (15 dBZ), >= 2 (20), >= 4 (30)
RING_PX = 30                             # 15 mi of a 60 mi radius = 120 px * 15/60

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"FAILED ({r.returncode}): {' '.join(cmd[:4])}...\n{r.stdout[-800:]}\n{r.stderr[-800:]}")
    return r.stdout

def decode(product, t, icao, opts, label):
    ft, key = fetch.nearest(product, t)
    gz = fetch.download(key, os.path.join(DATA, "grib", product))
    out = os.path.join(DATA, "frames", label)
    run(["node", "--experimental-strip-types", os.path.join(HERE, "frame.ts"), SPIKE, AIRPORTS, gz, out, product,
         f"{icao}{opts}"])
    meta = json.load(open(os.path.join(out, f"{product}.json")))
    meta["fileTime"] = ft
    return meta, out

def screen(wxc, out_bin):
    run([sys.executable, "-I", RENDER, SPIKE, wxc, "--out", out_bin])
    return open(out_bin, "rb").read()

def census_ok(c):
    return c["odd"] == 0

def lvl(c):
    return c if 1 <= c <= 13 else 0

def cls(c):
    return "echo" if 1 <= c <= 13 else {0: "dry", 14: "nocov", 15: "missing"}.get(c, "?")

RING = [y * 240 + x for y in range(240) for x in range(240) if (x + 0.5 - 120) ** 2 + (y + 0.5 - 120) ** 2 <= RING_PX ** 2]

def measures(a, b, ca, cb):
    disc = [i for i in range(57600) if a[i] != 255]
    assert disc == [i for i in range(57600) if b[i] != 255], "the two screens' discs differ"
    n = len(disc)
    same_cls = sum(1 for i in disc if cls(a[i]) == cls(b[i]))
    same_code = sum(1 for i in disc if a[i] == b[i])
    ea = sum(1 for i in disc if 1 <= a[i] <= 13); eb = sum(1 for i in disc if 1 <= b[i] <= 13)
    either = [i for i in disc if 1 <= a[i] <= 13 or 1 <= b[i] <= 13]
    d = [lvl(a[i]) - lvl(b[i]) for i in either]
    m3 = {"<=-1": sum(1 for x in d if x <= -1), "0": sum(1 for x in d if x == 0), "+1": sum(1 for x in d if x == 1), ">=+2": sum(1 for x in d if x >= 2)}
    p0 = sum(1 for x in d if x >= 0) / len(d) if d else None
    na = sum(1 for i in disc if a[i] == 14); nb = sum(1 for i in disc if b[i] == 14)
    ring = lambda s, t: any(t <= s[i] <= 13 for i in RING)
    m5 = {t: {"ring": (ring(a, t), ring(b, t)), "centre": (t <= ca <= 13, t <= cb <= 13)} for t in THRESH}
    ringmax = (max((lvl(a[i]) for i in RING), default=0), max((lvl(b[i]) for i in RING), default=0))
    return {"disc": n, "M1_class": same_cls / n, "M1_code": same_code / n, "M2": (ea / n, eb / n), "echo_either": len(either),
            "M3": m3, "P0": p0, "M4": (na / n, nb / n), "M5": m5, "ringmax": ringmax, "centre": (ca, cb)}

def frame(name, icao, t):
    comp, cdir = decode("comp", t, icao, ":crop:sab", name)
    hsr, hdir = decode("hsr", t, icao, ":crop", name)
    gap = abs((comp["fileTime"] - hsr["fileTime"]).total_seconds())
    res = {"name": name, "icao": icao, "t": t.isoformat(), "comp_file": comp["fileTime"].isoformat(),
           "hsr_file": hsr["fileTime"].isoformat(), "gap_s": gap,
           "census": {"comp": comp["census"], "hsr": hsr["census"]},
           "crops": {"comp": comp["crops"], "hsr": hsr["crops"]}}
    res["anchor_ok"] = census_ok(comp["census"]) and census_ok(hsr["census"])
    if not res["anchor_ok"] or gap > 120:
        return res
    sc = screen(os.path.join(cdir, f"{icao}-comp.wxc"), os.path.join(cdir, "comp.screen"))
    ss = screen(os.path.join(cdir, f"{icao}-comp-sab.wxc"), os.path.join(cdir, "comp-sab.screen"))
    sh = screen(os.path.join(hdir, f"{icao}-hsr.wxc"), os.path.join(hdir, "hsr.screen"))
    cc, ch = comp["centres"][icao]["code"], hsr["centres"][icao]["code"]
    res["raw_centre"] = (comp["centres"][icao]["raw"], hsr["centres"][icao]["raw"])
    res["self"] = measures(sc, sc, cc, cc)
    cc_sab = comp["centres"][icao]["code"]           # the centre's sabotaged code, recomputed below
    raw = comp["centres"][icao]["raw"]
    cc_sab = cc if raw in (-999, -99) else (0 if raw - 5 < 15 else min(13, 1 + int((raw - 5 - 15) // 5)))
    res["sab"] = measures(sc, ss, cc, cc_sab)
    res["real"] = measures(sc, sh, cc, ch)
    return res

def centre_only(t, icao):
    comp, _ = decode("comp", t, icao, "", "c1-" + t.strftime("%Y%m%d%H%M"))
    hsr, _ = decode("hsr", t, icao, "", "c1-" + t.strftime("%Y%m%d%H%M"))
    return {"t": t.isoformat(), "comp_file": comp["fileTime"].isoformat(), "hsr_file": hsr["fileTime"].isoformat(),
            "anchor_ok": census_ok(comp["census"]) and census_ok(hsr["census"]),
            "comp": comp["centres"][icao], "hsr": hsr["centres"][icao]}

if __name__ == "__main__" and sys.argv[2] == "run":
    sel = json.load(open(os.path.join(DATA, "selection.json")))
    results = {"frames": [], "f2_search": [], "change1": []}
    T = lambda s: datetime.fromisoformat(s)
    def save():
        json.dump(results, open(os.path.join(DATA, "results.json"), "w"), indent=1, default=str)
    # F1: Stage 1's frame (composite file 22:00:42Z)
    results["frames"].append(frame("F1", "KDFW", datetime(2024, 5, 21, 22, 0, 42))); save()
    # F2: the first METAR candidate whose composite has echo (>= 15 dBZ) within 15 mi
    for c in sel["F2_candidates"]:
        t = T(c["t"]); icao = c["icao"]
        comp, cdir = decode("comp", t, icao, ":crop", "F2try-" + icao + t.strftime("%Y%m%d%H%M"))
        sc = screen(os.path.join(cdir, f"{icao}-comp.wxc"), os.path.join(cdir, "comp.screen"))
        hit = any(1 <= sc[i] <= 13 for i in RING)
        results["f2_search"].append({"icao": icao, "t": c["t"], "comp_file": comp["fileTime"].isoformat(), "echo_within_15mi": hit}); save()
        if hit:
            r = frame("F2", icao, t)
            if r.get("anchor_ok") and r["gap_s"] <= 120:
                results["frames"].append(r); save(); break
            results["f2_search"][-1]["replaced"] = "anchor or gap"; save()
    for name in ("F3", "F5", "F4"):
        results["frames"].append(frame(name, sel[name]["icao"], T(sel[name]["t"]))); save()
    for s in sel["F4"]["sample"]:
        results["change1"].append(centre_only(T(s), sel["F4"]["icao"])); save()
    print("done:", len(results["frames"]), "frames,", len(results["f2_search"]), "F2 tries,", len(results["change1"]), "change-1 hours")
