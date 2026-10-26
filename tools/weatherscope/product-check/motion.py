"""Weatherscope Stage 2, 2.0: the motion test (docs/weatherscope-stage2.md, addendum 2).
Two consecutive SeamlessHSR files, 2 min apart, on F1 and F3, through run 1's pipeline unchanged.
    python -I motion.py <scratchpad>
"""
import json, os, sys
from datetime import datetime
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import check, fetch

PAIRS = [("F1", "KDFW", datetime(2024, 5, 21, 22, 0, 0), datetime(2024, 5, 21, 22, 2, 0), 1841, 11285),
         ("F3", "KATL", datetime(2024, 11, 14, 9, 52, 0), datetime(2024, 11, 14, 9, 54, 0), 4466, 36653)]
out = []
for name, icao, t0, t1, hi, either in PAIRS:
    r = {"name": name, "icao": icao}
    metas, screens = [], []
    for k, t in (("early", t0), ("late", t1)):
        ft, _ = fetch.nearest("hsr", t)
        assert ft == t, f"{name}: nearest SeamlessHSR file to {t} is {ft}, not the exact file"
        meta, d = check.decode("hsr", t, icao, ":crop", f"M-{name}-{k}")
        metas.append(meta)
        screens.append(check.screen(os.path.join(d, f"{icao}-hsr.wxc"), os.path.join(d, "hsr.screen")))
    r["files"] = [m["fileTime"].isoformat() for m in metas]
    r["gap_s"] = (metas[1]["fileTime"] - metas[0]["fileTime"]).total_seconds()
    r["census_ok"] = all(check.census_ok(m["census"]) for m in metas)
    r["census"] = [m["census"] for m in metas]
    ce, cl = metas[0]["centres"][icao]["code"], metas[1]["centres"][icao]["code"]
    ctrl = [check.measures(s, s, c, c) for s, c in ((screens[0], ce), (screens[1], cl))]
    r["control"] = [{"up": c["M3"]["+1"] + c["M3"][">=+2"], "down": c["M3"]["<=-1"], "either": c["echo_either"]} for c in ctrl]
    m = check.measures(screens[1], screens[0], cl, ce)          # level(late) - level(early)
    up, down, n = m["M3"]["+1"] + m["M3"][">=+2"], m["M3"]["<=-1"], m["echo_either"]
    base = 100 * hi / either
    share = 100 * up / n
    reading = ("(a)" if abs(share - base) <= 4 else "(b)" if share < base / 2 else "outside")
    r.update({"either": n, "up": up, "down": down, "up_pct": share, "down_pct": 100 * down / n,
              "run1_hsr_higher_pct": base, "reading": reading, "M1_class": m["M1_class"], "M2": m["M2"]})
    out.append(r)
    print(f"{name} {icao}: files {r['files'][0][11:]} -> {r['files'][1][11:]} (gap {r['gap_s']:.0f} s), census ok {r['census_ok']}; "
          f"control up/down {[(c['up'], c['down']) for c in r['control']]}")
    print(f"   over {n:,} px with echo in either: WENT UP {up:,} = {share:.2f} %  |  went down {down:,} = {100*down/n:.2f} %"
          f"  |  run 1 'SeamlessHSR higher' {base:.2f} %  ->  reading {reading}")
json.dump(out, open(os.path.join(check.DATA, "motion.json"), "w"), indent=1, default=str)
