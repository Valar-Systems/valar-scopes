"""The one synthetic feed for W5'''' and K6 (docs/wx-device.md, "One synthetic feed" and the rulings of 18:10Z and 18:25Z).

GENERATED, NEVER RECORDED: no key, no recorded position, nothing of a real flight. Every contact is a deterministic
function of (seed, its index, seconds since the arm's start), so every arm of a session sees the same contacts, tracks
and labels at the same offset from its boot. Positions exist only as offsets from a centre the caller passes in memory
(K6: the /blips query's own lat/lon; W5'''': E2:08's location read from its page), and are never written by this module.

  W5''''  (local mode, aircraft.json): hexes in SpecialAircraft::NON_ICAO_RANGES, so the device settles enrichment
          offline and no request leaves for them; callsigns "SYNnnn". n follows the schedule: 24 until the window
          (boot + 180 s), then plateaus of 24, 28, 32, 36 for 90 s each, then 36.
  K6      (the replay proxy's /blips and /v1/enrich): hexes in one allocated, non-military block, so the device enriches
          them; airline-style callsigns; the proxy answers /v1/enrich for exactly these 60 hexes from common real
          values (type and operator). /blips honours the request's limit (40) and alternates two overlapping sets,
          contacts 0-39 and 20-59, so the device's retention (graceMs) holds 60, as the cloud path does.

Contacts 0, 1 and 2 are the nearest, the highest and the fastest at every moment, so NEAR, HIGH and FAST land on three
distinct contacts (3 circles and 3 tags a frame). Tracks are circles about the centre, so a contact never leaves the
disc and never jumps (a jump would draw a trail across the scope).

    python -I tools/wx-replay/synth_feed.py --selftest
"""
import math
import random
import re
import sys

KM_PER_DEG = 111.32
NON_ICAO_LO, NON_ICAO_HI = 0x230000, 0x2FFFFF   # src/SpecialAircraft.cpp NON_ICAO_RANGES (asserted in the selftest)
W5_BASE = 0x230101
K6_BASE = 0xA10101                              # US civil; checked against the military ranges in the selftest
TYPES = [("B738", "Boeing 737-800"), ("A320", "Airbus A320"), ("A21N", "Airbus A321neo"), ("B739", "Boeing 737-900"),
         ("E75L", "Embraer E175"), ("CRJ9", "Bombardier CRJ-900"), ("B38M", "Boeing 737 MAX 8"), ("A319", "Airbus A319"),
         ("DH8D", "De Havilland Dash 8-400"), ("B77W", "Boeing 777-300ER")]
OPERATORS = [("SWA", "Southwest Airlines"), ("ASA", "Alaska Airlines"), ("UAL", "United Airlines"),
             ("DAL", "Delta Air Lines"), ("AAL", "American Airlines"), ("SKW", "SkyWest Airlines"),
             ("QXE", "Horizon Air"), ("JBU", "JetBlue Airways")]
CATEGORY = {3: "A3", 4: "A4", 5: "A5", 6: "A6"}   # OpenSky enum -> the local feed's string
SCHEDULE = [(180, 24), (270, 28), (360, 32), (450, 36)]   # W5'''': (from s, n); before 180 s, 24


class Feed:
    def __init__(self, kind, n, seed=20261030):
        assert kind in ("w5", "k6")
        self.kind, self.n = kind, n
        self.c = [self._make(i, random.Random(seed * 1000 + i)) for i in range(n)]

    def _make(self, i, rng):
        if i == 0:   r, alt, kt = 0.08, 11000, 250          # the nearest
        elif i == 1: r, alt, kt = 0.55, 43000, 470          # the highest
        elif i == 2: r, alt, kt = 0.70, 35000, 620          # the fastest
        else:
            r = 0.20 + 0.72 * rng.random()
            alt = int(2000 + 37000 * ((i * 0.6180339887) % 1.0)) // 100 * 100   # spread across the altitude bands
            kt = int(140 + 380 * rng.random())
        base = W5_BASE if self.kind == "w5" else K6_BASE
        al, _ = OPERATORS[i % len(OPERATORS)]
        return {"hex": f"{base + i:06x}",
                "cs": f"SYN{i + 1:03d}" if self.kind == "w5" else f"{al}{100 + 7 * i}",
                "r": r, "th0": 2 * math.pi * rng.random(), "dir": 1 if i % 2 == 0 else -1,
                "alt": alt, "kt": kt, "cat": (3, 4, 4, 4, 5, 6)[i % 6],
                "type": TYPES[i % len(TYPES)], "op": OPERATORS[i % len(OPERATORS)][1]}

    def state(self, i, t, lat0, lon0, range_km):
        """Contact i at t s after the arm's start: (lat, lon, track deg, alt ft, gs kt)."""
        c = self.c[i]
        rk = c["r"] * 0.92 * range_km
        w = c["dir"] * (c["kt"] * 1.852 / 3600.0) / rk          # rad/s along the circle
        th = c["th0"] + w * t
        east, north = rk * math.sin(th), rk * math.cos(th)
        ve, vn = rk * w * math.cos(th), -rk * w * math.sin(th)
        track = math.degrees(math.atan2(ve, vn)) % 360.0
        lat = lat0 + north / KM_PER_DEG
        lon = lon0 + east / (KM_PER_DEG * math.cos(math.radians(lat0)))
        return lat, lon, track, c["alt"], c["kt"]

    def w5_ids(self, t):
        """W5'''': the contacts served t s after the arm's start."""
        n = 24
        for start, k in SCHEDULE:
            if t >= start: n = k
        return list(range(min(n, self.n)))

    def k6_ids(self, request_no, limit):
        """K6: the request's set, alternating 0-39 and 20-59, honouring the request's limit."""
        ids = list(range(0, 40)) if request_no % 2 == 0 else list(range(20, 60))
        return ids[:max(0, min(limit, 40))]

    def blips(self, ids, t, now_epoch, lat0, lon0, range_km):
        """The production /blips shape (src/CloudFeed.cpp ParseBlips): v, t, a[[hex, cs, lat*1e4, lon*1e4, altFt, gsKt,
        trackDeg, vrateFpm, category, ageS]]."""
        rows = []
        for i in ids:
            lat, lon, trk, alt, kt = self.state(i, t, lat0, lon0, range_km)
            rows.append([self.c[i]["hex"], self.c[i]["cs"], round(lat * 1e4), round(lon * 1e4), alt, kt, round(trk) % 360,
                         0, self.c[i]["cat"], 0])
        return {"v": 1, "t": int(now_epoch), "a": rows}

    def aircraft_json(self, ids, t, now_epoch, lat0, lon0, range_km):
        """The local feed's shape (src/models/Aircraft.cpp ParseLocalAircraft)."""
        out = []
        for i in ids:
            lat, lon, trk, alt, kt = self.state(i, t, lat0, lon0, range_km)
            out.append({"hex": self.c[i]["hex"], "flight": f"{self.c[i]['cs']:<8}", "lat": round(lat, 6),
                        "lon": round(lon, 6), "alt_baro": alt, "gs": kt, "track": round(trk, 1),
                        "category": CATEGORY[self.c[i]["cat"]], "seen_pos": 0.4, "type": "adsb_icao"})
        return {"now": round(now_epoch, 1), "messages": 0, "aircraft": out}

    def enrich(self, hexcode):
        """K6's local /v1/enrich answer (src/CloudFeed.cpp ParseEnrich), for exactly this feed's hexes; None otherwise."""
        if self.kind != "k6": return None
        for c in self.c:
            if c["hex"] == hexcode.lower():
                code, name = c["type"]
                return {"v": 1, "r": "", "t": code, "tn": name, "op": c["op"], "o": "", "d": "", "p": None, "pk": ""}
        return None


def selftest(root):
    bad = 0

    def check(ok, what):
        nonlocal bad
        print(f"  {'ok  ' if ok else 'FAIL'} {what}")
        bad += not ok

    sa = open(f"{root}/src/SpecialAircraft.cpp", encoding="utf-8").read()
    non_icao = [(int(a, 16), int(b, 16)) for a, b in
                re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}",
                           sa[sa.index("NON_ICAO_RANGES[]"):sa.index("};", sa.index("NON_ICAO_RANGES[]"))])]
    mil_block = sa[sa.index("MIL_RANGES[]"):sa.index("};", sa.index("MIL_RANGES[]"))]
    mil = [(int(a, 16), int(b, 16)) for a, b in re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}", mil_block)]
    check(non_icao == [(NON_ICAO_LO, NON_ICAO_HI)] and len(mil) > 5,
          f"the source's ranges read: non-ICAO {[(hex(a), hex(b)) for a, b in non_icao]}, {len(mil)} military")
    w5, k6 = Feed("w5", 36), Feed("k6", 60)
    inr = lambda h, rs: any(a <= int(h, 16) <= b for a, b in rs)
    check(all(inr(c["hex"], non_icao) for c in w5.c), "W5'''': every hex is non-ICAO (enrichment settled offline)")
    check(not any(inr(c["hex"], non_icao) or inr(c["hex"], mil) for c in k6.c),
          "K6: no hex is non-ICAO or military (the device enriches them)")
    check(len({c["hex"] for c in k6.c}) == 60 and len({c["cs"] for c in k6.c}) == 60, "K6: 60 distinct hexes and callsigns")
    check(Feed("k6", 60).blips(range(60), 123.0, 1e9, 44.0, -121.0, 160) == k6.blips(range(60), 123.0, 1e9, 44.0, -121.0, 160),
          "deterministic: two feeds from one seed give the same /blips")
    lat0, lon0, rng_km = 44.06, -121.32, 160.0
    far = 0.0
    for t in range(0, 900, 7):
        for i in range(60):
            lat, lon, _, _, _ = k6.state(i, t, lat0, lon0, rng_km)
            d = math.hypot((lat - lat0) * KM_PER_DEG, (lon - lon0) * KM_PER_DEG * math.cos(math.radians(lat0)))
            far = max(far, d / rng_km)
    check(far <= 0.93, f"every contact stays inside the disc (furthest {far:.3f} of the range)")
    ok_hl = True
    for t in range(0, 900, 11):
        for ids in (w5.w5_ids(t), list(range(0, 40)), list(range(20, 60))):
            if 0 not in ids: continue      # the 20-59 set: the device still holds 0-2 by retention
            st = {i: (k6 if len(ids) > 36 or max(ids) > 35 else w5).state(i, t, lat0, lon0, rng_km) for i in ids}
            feed = k6 if len(ids) > 36 or max(ids) > 35 else w5
            dist = {i: feed.c[i]["r"] for i in ids}
            ok_hl &= min(dist, key=dist.get) == 0 and max(ids, key=lambda i: feed.c[i]["alt"]) == 1 and \
                max(ids, key=lambda i: feed.c[i]["kt"]) == 2
    check(ok_hl, "nearest, highest and fastest are contacts 0, 1 and 2 at every sampled moment: three distinct highlights")
    check([len(w5.w5_ids(t)) for t in (0, 179, 180, 269, 270, 360, 450, 539, 600)] == [24, 24, 24, 24, 28, 32, 36, 36, 36],
          "the W5'''' schedule: 24 until the window, then 24, 28, 32, 36 for 90 s each")
    a, b = k6.k6_ids(0, 40), k6.k6_ids(1, 40)
    check(len(a) == 40 and len(b) == 40 and set(a) | set(b) == set(range(60)) and k6.k6_ids(0, 25) == list(range(25)),
          "K6: each response honours the limit (<= 40), and the two sets together hold all 60")
    row = k6.blips([5], 0, 1e9, lat0, lon0, rng_km)["a"][0]
    check(len(row) == 10 and isinstance(row[2], int) and row[8] in (3, 4, 5, 6), f"/blips row: 10 fields in schema order {row[:2]}...")
    e = k6.enrich(k6.c[7]["hex"])
    check(e is not None and e["t"] and e["op"] and e["v"] == 1 and k6.enrich("abcdef") is None and w5.enrich(w5.c[0]["hex"]) is None,
          "/v1/enrich: type and operator for exactly K6's hexes, nothing for any other")
    aj = w5.aircraft_json(w5.w5_ids(200), 200, 1e9, lat0, lon0, rng_km)["aircraft"][0]
    check({"hex", "flight", "lat", "lon", "alt_baro", "gs", "track", "category", "seen_pos"} <= set(aj) and aj["seen_pos"] < 5,
          "aircraft.json: the fields ParseLocalAircraft reads, seen_pos fresh")
    print("SYNTH FEED SELFTEST PASSED" if not bad else f"SYNTH FEED SELFTEST: {bad} FAILED")
    return 1 if bad else 0


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        import os
        sys.exit(selftest(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
    print(__doc__)
    sys.exit(2)
