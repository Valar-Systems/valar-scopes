"""3.5c's trim (docs/weatherscope-stage3.md A12.131): the six raw api.weather.gov documents (capture_nws.py) reduced to
what the forecast, conditions and alerts screens read, written as test/fixtures/wx/nws/<case>.json.

    python -I tools/weatherscope/nws/trim_nws.py <raw dir> <case>     e.g. dallas-day
    python -I tools/weatherscope/nws/trim_nws.py --selftest

Kept, and nothing else: the capture's time (the hourly document's generatedAt, "now" in stored mode); the first 48 hourly
entries (start, F, chance); every day-part period (name, daytime, F, chance, short forecast cut to 24 characters --
free text, so a prefix is a true prefix); the observation (time, F, wind direction, wind and gust in mph, humidity),
converted from SI with the rounding pinned as floor(x + 0.5); the alerts (event, start, end). Every value that NWS may
send as null carries a has-flag. No headline (it names offices and areas), no area, no polygon, no id, no URL.
When no captured alert is a watch or an advisory, a SYNTHETIC list, marked so, is stored beside the real one for the
populated alerts screen. REFUSES any key outside the schema, any string with "http", ".gov" or "@", and any number that is
not an integer. Prints counts only. Exit 0 written; 1 refused; 2 the rig.
"""
import datetime, json, math, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
OUT = os.path.join(ROOT, "test", "fixtures", "wx", "nws")
WARNING_SCREEN = {"Tornado Warning", "Snow Squall Warning", "Severe Thunderstorm Warning", "Flash Flood Warning"}
SCHEMA = {
    "case": str, "source": str, "capturedUtc": int,
    "hourly": [{"startUtc": int, "tempF": int, "hasTemp": bool, "pop": int, "hasPop": bool}],
    "periods": [{"name": str, "isDaytime": bool, "tempF": int, "hasTemp": bool, "pop": int, "hasPop": bool, "short": str}],
    "observation": {"timeUtc": int, "tempF": int, "hasTemp": bool, "windDirDeg": int, "hasWindDir": bool,
                    "windMph": int, "hasWind": bool, "gustMph": int, "hasGust": bool, "humidityPct": int,
                    "hasHumidity": bool},
    "alerts": [{"event": str, "startUtc": int, "endUtc": int}],
    "syntheticAlerts": {"synthetic": bool, "note": str, "items": [{"event": str, "startUtc": int, "endUtc": int}]},
}
BANNED = ("http", ".gov", "@")


def r(x):
    """The pinned rounding: floor(x + 0.5) -- the same in every language that reads these records."""
    return int(math.floor(x + 0.5))


def utc(iso):
    return int(datetime.datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp())


def val(obj):
    v = (obj or {}).get("value")
    return v


def trim(raw, case):
    hourly = raw["hourly"]["properties"]
    rec = {"case": case,
           "source": "api.weather.gov, fetched once by the session (A12.131) and trimmed by trim_nws.py",
           "capturedUtc": utc(hourly["generatedAt"])}
    rec["hourly"] = []
    for p in hourly["periods"][:48]:
        pop = val(p.get("probabilityOfPrecipitation"))
        rec["hourly"].append({"startUtc": utc(p["startTime"]), "tempF": int(p["temperature"]) if p.get("temperature") is not None else 0,
                              "hasTemp": p.get("temperature") is not None,
                              "pop": r(pop) if pop is not None else 0, "hasPop": pop is not None})
    rec["periods"] = []
    for p in raw["forecast"]["properties"]["periods"]:
        pop = val(p.get("probabilityOfPrecipitation"))
        rec["periods"].append({"name": p["name"], "isDaytime": bool(p["isDaytime"]),
                               "tempF": int(p["temperature"]) if p.get("temperature") is not None else 0,
                               "hasTemp": p.get("temperature") is not None,
                               "pop": r(pop) if pop is not None else 0, "hasPop": pop is not None,
                               "short": (p.get("shortForecast") or "")[:24]})
    o = raw["observation"]["properties"]
    t, wd, ws, wg, rh = (val(o.get(k)) for k in ("temperature", "windDirection", "windSpeed", "windGust", "relativeHumidity"))
    rec["observation"] = {"timeUtc": utc(o["timestamp"]),
                          "tempF": r(t * 9 / 5 + 32) if t is not None else 0, "hasTemp": t is not None,
                          "windDirDeg": r(wd) % 360 if wd is not None else 0, "hasWindDir": wd is not None,
                          "windMph": r(ws / 1.609344) if ws is not None else 0, "hasWind": ws is not None,
                          "gustMph": r(wg / 1.609344) if wg is not None else 0, "hasGust": wg is not None,
                          "humidityPct": r(rh) if rh is not None else 0, "hasHumidity": rh is not None}
    rec["alerts"] = []
    for f in raw["alerts"].get("features", []):
        a = f["properties"]
        start = a.get("onset") or a.get("effective")
        end = a.get("ends") or a.get("expires")
        rec["alerts"].append({"event": a["event"], "startUtc": utc(start), "endUtc": utc(end)})
    if not [a for a in rec["alerts"] if a["event"] not in WARNING_SCREEN]:
        c = rec["capturedUtc"]
        h = 3600
        rec["syntheticAlerts"] = {
            "synthetic": True,
            "note": "SYNTHETIC: the capture held no watch or advisory; this list exists only for the populated layout",
            "items": [{"event": "Wind Advisory", "startUtc": c + 2 * h, "endUtc": c + 14 * h},
                      {"event": "Dense Fog Advisory", "startUtc": c + 10 * h, "endUtc": c + 18 * h},
                      {"event": "Fire Weather Watch", "startUtc": c + 20 * h, "endUtc": c + 32 * h},
                      {"event": "Frost Advisory", "startUtc": c + 34 * h, "endUtc": c + 42 * h},
                      {"event": "Winter Storm Watch", "startUtc": c + 48 * h, "endUtc": c + 72 * h}]}
    return rec


def check(rec, schema=SCHEMA, path="$"):
    """Every refusal, as (path, reason). Empty means the record may be written."""
    bad = []
    if isinstance(schema, dict):
        if not isinstance(rec, dict):
            return [(path, "not an object")]
        for k, v in rec.items():
            if k not in schema:
                bad.append((f"{path}.{k}", "a key outside the schema"))
            else:
                bad += check(v, schema[k], f"{path}.{k}")
    elif isinstance(schema, list):
        if not isinstance(rec, list):
            return [(path, "not a list")]
        for i, v in enumerate(rec):
            bad += check(v, schema[0], f"{path}[{i}]")
    elif schema is int:
        if isinstance(rec, bool) or not isinstance(rec, int):
            bad.append((path, "not an integer"))
    elif schema is bool:
        if not isinstance(rec, bool):
            bad.append((path, "not a boolean"))
    elif schema is str:
        if not isinstance(rec, str):
            bad.append((path, "not a string"))
        elif any(b in rec.lower() for b in BANNED):
            bad.append((path, "a banned fragment (http, .gov or @)"))
    return bad


def selftest():
    good = {"case": "t", "source": "s", "capturedUtc": 1, "hourly": [], "periods": [], "alerts": [],
            "observation": {"timeUtc": 1, "tempF": 1, "hasTemp": True, "windDirDeg": 0, "hasWindDir": True, "windMph": 0,
                            "hasWind": True, "gustMph": 0, "hasGust": False, "humidityPct": 1, "hasHumidity": True}}
    plants = {
        "a key outside the schema": lambda d: d["observation"].__setitem__("stationId", "X"),
        "http": lambda d: d.__setitem__("source", "see http example"),
        ".gov": lambda d: d.__setitem__("source", "from a .gov page"),
        "@": lambda d: d.__setitem__("source", "mail someone@example"),
        "a float with three decimals": lambda d: d["observation"].__setitem__("tempF", 32.123),
    }
    results = []
    ok = check(good) == []
    print(f"  {'ok  ' if ok else 'FAIL'}  CT0: CONTROL: a clean record passes")
    results.append(ok)
    for i, (name, plant) in enumerate(plants.items(), 1):
        d = json.loads(json.dumps(good))
        plant(d)
        ok = check(d) != []
        print(f"  {'ok  ' if ok else 'FAIL'}  CT{i}: {name} is refused")
        results.append(ok)
    print(f"{len(results)} checks, {results.count(False)} failures")
    return 1 if results.count(False) else 0


def main(argv):
    if argv[1:] == ["--selftest"]:
        return selftest()
    if len(argv) != 3:
        print(__doc__)
        return 2
    raw_dir, case = argv[1], argv[2]
    try:
        raw = {k: json.load(open(os.path.join(raw_dir, f"{k}.json"), encoding="utf-8"))
               for k in ("hourly", "forecast", "observation", "alerts")}
    except Exception as e:
        print(f"the RIG: cannot read the raw documents ({type(e).__name__})")
        return 2
    rec = trim(raw, case)
    bad = check(rec)
    text = json.dumps(rec, indent=1, ensure_ascii=True) + "\n"
    if bad:
        for p, why in bad[:20]:
            print(f"REFUSED {p}: {why}")
        return 1
    os.makedirs(OUT, exist_ok=True)
    dst = os.path.join(OUT, f"{case}.json")
    open(dst, "w", encoding="utf-8", newline="\n").write(text)
    syn = rec.get("syntheticAlerts")
    print(f"wrote {os.path.relpath(dst, ROOT)}: {len(text.encode())} B; hourly {len(rec['hourly'])}, periods "
          f"{len(rec['periods'])} (first daytime {rec['periods'][0]['isDaytime'] if rec['periods'] else '-'}), alerts "
          f"{len(rec['alerts'])}, synthetic alerts {len(syn['items']) if syn else 0}; refusals 0")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
