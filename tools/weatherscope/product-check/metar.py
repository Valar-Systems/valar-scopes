"""Weatherscope Stage 2, step 2.0: METAR-side selection rules (docs/weatherscope-stage2.md + addendum 1).

Reads routine hourly METARs from the Iowa Environmental Mesonet ASOS archive (public, keyless), cached on
disk. Applies ONLY the frozen rules; never looks at any radar product. Prints ICAO codes and times, never
a coordinate.
"""
import csv, io, json, os, sys, urllib.request
from datetime import datetime, timedelta

PRECIP = ("DZ", "RA", "SN", "SG", "IC", "PL", "GR", "GS", "UP")

def fetch(cache, icao, y1, m1, y2, m2):
    """Routine METARs (report_type=3) for [y1-m1-01, end of y2-m2], UTC. Cached."""
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, f"{icao}-{y1}{m1:02d}-{y2}{m2:02d}.csv")
    if not os.path.exists(path):
        end = datetime(y2 + (m2 == 12), m2 % 12 + 1, 1)
        url = ("https://mesonet.agron.iastate.edu/cgi-bin/request/asos.py?"
               f"station={icao[1:]}&data=metar&data=tmpf&data=wxcodes&year1={y1}&month1={m1}&day1=1"
               f"&year2={end.year}&month2={end.month}&day2=1&tz=Etc/UTC&format=onlycomma&latlon=no"
               "&missing=M&trace=T&direct=no&report_type=3")
        data = urllib.request.urlopen(url, timeout=120).read().decode("utf-8")
        if not data.startswith("station,valid"):
            raise SystemExit(f"IEM returned no CSV for {icao}: {data[:80]!r}")
        open(path, "w", encoding="utf-8").write(data)
    rows = []
    for r in csv.DictReader(io.StringIO(open(path, encoding="utf-8").read())):
        t = datetime.strptime(r["valid"], "%Y-%m-%d %H:%M")
        toks = [] if r["wxcodes"] in ("M", "") else r["wxcodes"].split()
        tmpf = None if r["tmpf"] in ("M", "") else float(r["tmpf"])
        rmk = r["metar"].split(" RMK ", 1)[1] if " RMK " in r["metar"] else ""
        rows.append({"t": t, "toks": toks, "tmpf": tmpf, "virga": "VIRGA" in rmk, "metar": r["metar"]})
    rows.sort(key=lambda r: r["t"])
    return rows

def at_station(tok):            # vicinity (VC...) is not at the station
    return not tok.lstrip("+-").startswith("VC")

def has_precip(r):
    return any(at_station(t) and any(p in t for p in PRECIP) for t in r["toks"])

def rain_ok(r):                 # F3/F5: a token with RA; no token with SH or TS anywhere (strict)
    if any(("SH" in t or "TS" in t) for t in r["toks"]):
        return False
    return any(at_station(t) and "RA" in t for t in r["toks"])

def snow_ok(r):                 # F4 / change 1: a token with SN that is not BLSN/DRSN alone; no RA
    if any(at_station(t) and "RA" in t for t in r["toks"]):
        return False
    return any(at_station(t) and "SN" in t and t.lstrip("+-") not in ("BLSN", "DRSN") for t in r["toks"])

def warm(r):
    return r["tmpf"] is not None and r["tmpf"] >= 41.0      # 5 degC

def runs_of_3(rows, ok):
    """Yield (a, b, c) consecutive routine METARs 50-70 min apart, each passing ok(), in time order."""
    for i in range(len(rows) - 2):
        a, b, c = rows[i], rows[i + 1], rows[i + 2]
        gap1, gap2 = (b["t"] - a["t"]).total_seconds() / 60, (c["t"] - b["t"]).total_seconds() / 60
        if 50 <= gap1 <= 70 and 50 <= gap2 <= 70 and ok(a) and ok(b) and ok(c):
            yield a, b, c

WEST = ["KPHX", "KLAS", "KABQ", "KELP", "KDEN", "KSLC", "KGJT", "KRNO", "KCYS", "KBIL"]
EAST = ["KATL", "KCLT", "KRDU", "KDCA", "KPHL", "KJFK", "KBOS"]
SNOW = ["KMSP", "KFAR", "KBIS", "KDSM", "KDEN"]

def f2_candidates(cache):
    """F2: VIRGA in remarks, no precipitation at the station; June-September 2024, then 2025; airports in
    order, hours ascending. Yields candidates; the radar half of the rule (composite echo within 15 mi)
    is applied by the caller, in this same order."""
    for icao in WEST:
        for y in (2024, 2025):
            for r in fetch(cache, icao, y, 6, y, 9):
                if r["virga"] and not has_precip(r):
                    yield icao, r

def f3_like(cache, airports, extra=lambda r: True):
    for icao in airports:
        rows = fetch(cache, icao, 2024, 11, 2025, 3)
        for a, b, c in runs_of_3(rows, lambda r: rain_ok(r) and warm(r) and extra(r)):
            return icao, (a, b, c)
    return None

def f4(cache):
    for icao in SNOW:
        rows = fetch(cache, icao, 2024, 12, 2025, 2)
        for a, b, c in runs_of_3(rows, snow_ok):
            sample = [r for r in rows if r["t"] >= a["t"] and snow_ok(r)][:24]
            return icao, (a, b, c), sample
    return None

def show(r):
    return f"{r['t']:%Y-%m-%d %H:%MZ} [{' '.join(r['toks']) or '-'}] {'' if r['tmpf'] is None else round((r['tmpf'] - 32) / 1.8, 1)}C{' VIRGA' if r['virga'] else ''}"

if __name__ == "__main__":
    cache = sys.argv[1]
    out = {}
    f3 = f3_like(cache, EAST)
    f5 = f3_like(cache, WEST, extra=lambda r: not r["virga"])
    f4r = f4(cache)
    for name, hit in (("F3", f3), ("F5", f5)):
        icao, run = hit
        print(f"{name}: {icao}  " + "  |  ".join(show(r) for r in run) + f"  -> frame at the middle hour {run[1]['t']:%Y-%m-%d %H:%MZ}")
        out[name] = {"icao": icao, "t": run[1]["t"].isoformat(), "run": [r["t"].isoformat() for r in run]}
    icao, run, sample = f4r
    print(f"F4: {icao}  " + "  |  ".join(show(r) for r in run) + f"  -> frame {run[1]['t']:%Y-%m-%d %H:%MZ}")
    print(f"F4 change-1 sample: {len(sample)} snowing hours, {sample[0]['t']:%Y-%m-%d %H:%MZ} .. {sample[-1]['t']:%Y-%m-%d %H:%MZ}; "
          f"tokens: {sorted(set(' '.join(r['toks']) for r in sample))}")
    out["F4"] = {"icao": icao, "t": run[1]["t"].isoformat(), "run": [r["t"].isoformat() for r in run],
                 "sample": [r["t"].isoformat() for r in sample]}
    n = 0; cands = []
    for icao, r in f2_candidates(cache):
        cands.append({"icao": icao, "t": r["t"].isoformat()}); n += 1
        if n >= 60: break
    print(f"F2: first {len(cands)} METAR-side candidates (VIRGA, no precipitation), in search order; first: "
          f"{cands[0]['icao']} {cands[0]['t']}")
    out["F2_candidates"] = cands
    json.dump(out, open(os.path.join(cache, "..", "selection.json"), "w"), indent=1)
