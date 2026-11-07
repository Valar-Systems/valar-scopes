"""3.5c's capture (docs/weatherscope-stage3.md A12.130, A12.131): six api.weather.gov documents for the Dallas fixture
place, fetched once by the session -- never by a device (D2) -- and written RAW into a directory OUTSIDE the repo.

    python -I tools/weatherscope/nws/capture_nws.py <out dir outside the repo>

The place is the fixture crop's centre (test/fixtures/wx/kdfw-60mi.wxc's header: the north-west cell's centre plus half
the grid), rounded to the 4 decimals api.weather.gov takes. It is never printed, and neither is any URL, office, grid
point or station id: the raw documents carry them, which is why they stay outside the repo. Prints counts only, and
grades A12.131's structural predictions.
Exit 0 every prediction held; 1 one did not (stop and report); 2 the rig (a fetch failed, or the out dir is in the repo).
"""
import json, os, struct, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CROP = os.path.join(ROOT, "test", "fixtures", "wx", "kdfw-60mi.wxc")
UA = "Valar-Systems Weatherscope 3.5c capture (support@valarsystems.com)"
API = "https://api.weather.gov"
WARNING_SCREEN = {"Tornado Warning", "Snow Squall Warning", "Severe Thunderstorm Warning", "Flash Flood Warning"}


def place():
    h = open(CROP, "rb").read(32)
    assert h[:4] == b"WXC1", "not a WXC1 crop"
    lat_nw, lon_nw = struct.unpack_from("<ii", h, 12)
    cell_lat, cell_lon = struct.unpack_from("<II", h, 20)
    rows, cols = struct.unpack_from("<HH", h, 28)
    lat = (lat_nw - (rows - 1) / 2.0 * cell_lat) / 1e6
    lon = (lon_nw + (cols - 1) / 2.0 * cell_lon) / 1e6
    return f"{lat:.4f}", f"{lon:.4f}"


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/geo+json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.status, json.loads(r.read().decode("utf-8"))


def inside_repo(path):
    """True if `path` is the repo or under it, however either is spelled. Compared by file identity, ancestor by
    ancestor: a string compare missed a Windows 8.3 short name (DANIEL~1) against the long one on 2026-11-06, and the
    first capture then wrote its raw documents inside the worktree (A12.132)."""
    p = os.path.abspath(path)
    while True:
        if os.path.exists(p) and os.path.samefile(p, ROOT):
            return True
        parent = os.path.dirname(p)
        if parent == p:
            return False
        p = parent


def selftest():
    """No network: the refusal, both spellings. Prints one line per check and 'N checks, M failures'."""
    checks = []

    def check(cid, ok, what):
        checks.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'}  {cid}: {what}")

    check("CN1", inside_repo(ROOT), "the repo root itself is inside")
    check("CN2", inside_repo(os.path.join(ROOT, "tmp-raw", "day")), "a new directory under the repo is inside")
    short = ROOT
    if os.name == "nt":
        import ctypes
        buf = ctypes.create_unicode_buffer(1024)
        if ctypes.windll.kernel32.GetShortPathNameW(ROOT, buf, 1024):
            short = buf.value
    check("CN3", inside_repo(os.path.join(short, "tmp-raw")),
          f"the same, spelled {'with 8.3 short names' if short != ROOT else 'as is (no short name here)'}")
    check("CN4", not inside_repo(os.path.join(os.path.dirname(ROOT), "nws-raw-outside")),
          "CONTROL: a sibling of the repo is outside")
    failures = checks.count(False)
    print(f"{len(checks)} checks, {failures} failures")
    return 1 if failures else 0


ROWS = []


def row(name, ok, detail):
    ROWS.append(ok)
    print(f"{name}: {'AS PREDICTED' if ok else 'OFF-PREDICTION'} -- {detail}")


def main(argv):
    if argv[1:] == ["--selftest"]:
        return selftest()
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = os.path.abspath(argv[1])
    if inside_repo(out):
        print("REFUSED: the raw documents carry coordinates and ids; write them outside the repo")
        return 2
    os.makedirs(out, exist_ok=True)
    lat, lon = place()
    docs = {}
    try:
        s, docs["points"] = get(f"{API}/points/{lat},{lon}")
        p = docs["points"]["properties"]
        row("1 points", s == 200 and all(p.get(k) for k in ("forecast", "forecastHourly", "observationStations")),
            f"status {s}; forecast, forecastHourly, observationStations present")
        s, docs["forecast"] = get(p["forecast"])
        per = docs["forecast"]["properties"]["periods"]
        row("2 forecast", s == 200 and len(per) == 14 and all(x.get("temperatureUnit") == "F" and x.get("temperature") is not None for x in per),
            f"status {s}; {len(per)} periods; every temperature in F and non-null: "
            f"{all(x.get('temperatureUnit') == 'F' and x.get('temperature') is not None for x in per)}")
        s, docs["hourly"] = get(p["forecastHourly"])
        hp = docs["hourly"]["properties"]["periods"]
        row("3 hourly", s == 200 and len(hp) >= 48 and all(x.get("temperatureUnit") == "F" for x in hp),
            f"status {s}; {len(hp)} periods (>= 48); every temperature in F: {all(x.get('temperatureUnit') == 'F' for x in hp)}")
        s, docs["stations"] = get(p["observationStations"])
        feats = docs["stations"].get("features", [])
        row("4 stations", s == 200 and len(feats) >= 1, f"status {s}; {len(feats)} station(s)")
        s, docs["observation"] = get(feats[0]["id"].rstrip("/") + "/observations/latest")
        row("5 observation", s == 200, f"status {s}")
        s, docs["alerts"] = get(f"{API}/alerts/active?point={lat},{lon}")
        af = docs["alerts"].get("features")
        row("6 alerts", s == 200 and isinstance(af, list), f"status {s}; a feature list: {isinstance(af, list)}")
    except Exception as e:
        print(f"the RIG: a fetch failed ({type(e).__name__}); nothing is trimmed")
        return 2
    for k, v in docs.items():
        json.dump(v, open(os.path.join(out, f"{k}.json"), "w", encoding="utf-8"))
    # Reported, not predicted (A12.131): what the weather holds.
    pop_null = sum(1 for x in hp[:48] if (x.get("probabilityOfPrecipitation") or {}).get("value") is None)
    per_pop_null = sum(1 for x in per if (x.get("probabilityOfPrecipitation") or {}).get("value") is None)
    o = docs["observation"]["properties"]
    nulls = [k for k in ("temperature", "windDirection", "windSpeed", "windGust", "relativeHumidity")
             if (o.get(k) or {}).get("value") is None]
    events = [f["properties"].get("event") for f in af]
    others = [e for e in events if e not in WARNING_SCREEN]
    print(f"reported: hourly chance nulls {pop_null} of 48; period chance nulls {per_pop_null} of {len(per)}; "
          f"first period daytime {per[0].get('isDaytime')}; observation nulls {nulls or 'none'}; "
          f"alerts {len(events)} ({len(events) - len(others)} the warning screen's, {len(others)} watches/advisories/other)")
    print(f"wrote {len(docs)} raw document(s) outside the repo")
    return 0 if all(ROWS) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
