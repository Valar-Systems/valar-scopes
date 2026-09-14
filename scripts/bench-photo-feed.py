#!/usr/bin/env python3
"""Bench-only canned aircraft.json for product photography.

SERVES DATA, COMPILES NOTHING. The device reaches this over HTTP via the
existing local-receiver source (`data-source=local`, `local-url=...`), so no
bench firmware is involved and nothing here can reach a shipping image. That is
checked anyway, with an anchor: the synthetic callsigns must be absent from the
shipping ELF and present in this file.

EVERY IDENTITY HERE IS PROVABLY NOT A REAL AIRCRAFT, because these frames end up
in published marketing images and must never imply that a real flight declared
an emergency:

  * callsigns use the ICAO prefix ZZZ, which is not allocated to any operator;
  * registrations use the form N0xxxx. The FAA never issues an N-number whose
    first character after the N is a zero, so these are well-formed to read and
    impossible to hold;
  * the emergency and military frames use those same synthetic identities.

TYPE CODES ARE REAL, deliberately: the detail card resolves its photograph by
ICAO TYPE rather than by tail, so real type codes give Daniel real aircraft
imagery with no real aircraft implied. A type with no image in the library
renders the silhouette layout, not an empty panel.

Positions are scattered around Bend, OR (44.10 N, -121.29 W) at altitudes that
match what actually flies there: airliners overheard at cruise, GA and
turboprops low.
"""
import json
import math
import random
import time

BEND_LAT, BEND_LON = 44.1034, -121.2858

# (icao_type, altitude band, groundspeed band) -- all REAL ICAO type codes.
ORDINARY = [
    ("B738", 34000, 450), ("A320", 36000, 440), ("B39M", 38000, 460),
    ("A21N", 35000, 455), ("E75L", 31000, 400), ("CRJ9", 33000, 420),
    ("B77W", 39000, 480), ("A333", 37000, 470), ("B752", 32000, 430),
    ("E190", 30000, 410), ("DH8D", 18000, 260), ("AT76", 17000, 250),
    ("PC12", 24000, 270), ("BE20", 21000, 280), ("C208", 11000, 165),
    ("C172", 6500, 110), ("C182", 7500, 125), ("SR22", 9500, 175),
    ("PA28", 5500, 115), ("RV7", 8500, 165), ("BE36", 10500, 170),
    ("C25B", 41000, 430), ("GLF5", 43000, 470), ("C750", 45000, 480),
    ("B06", 2500, 120), ("EC30", 3500, 130), ("P28A", 6000, 110),
    ("M20P", 8000, 155),
]


def scatter(i, n, km_min=8.0, km_max=95.0):
    """Ring-ish scatter so the radar looks populated rather than clustered."""
    rnd = random.Random(1000 + i)          # seeded: the same picture every run
    bearing = (360.0 / n) * i + rnd.uniform(-6, 6)
    km = rnd.uniform(km_min, km_max)
    br = math.radians(bearing)
    dlat = (km * math.cos(br)) / 111.32
    dlon = (km * math.sin(br)) / (111.32 * math.cos(math.radians(BEND_LAT)))
    return round(BEND_LAT + dlat, 5), round(BEND_LON + dlon, 5), round(bearing, 1)


def build():
    now = time.time()
    ac = []

    for i, (typ, alt, gs) in enumerate(ORDINARY):
        lat, lon, brg = scatter(i, len(ORDINARY))
        rnd = random.Random(2000 + i)
        ac.append({
            "hex": "a%05x" % (0x10000 + i * 0x137),   # civil US block, not military
            "flight": "ZZZ%03d  " % (101 + i),        # ZZZ is unallocated
            "r": "N0B%02dA" % (10 + i),               # N0... is never issued
            "t": typ,
            "type": typ,
            "lat": lat, "lon": lon,
            "alt_baro": alt + rnd.randrange(-800, 800, 100),
            "alt_geom": alt + 150,
            "gs": gs + rnd.randint(-20, 20),
            "track": round((brg + 180 + rnd.uniform(-40, 40)) % 360, 1),
            "baro_rate": rnd.choice([0, 0, 0, 640, -640]),
            "squawk": "%04d" % rnd.randint(1200, 6777),
            "category": "A3",
            "seen_pos": round(rnd.uniform(0.2, 3.0), 1),
        })

    # ---- the three special frames -----------------------------------------
    lat, lon, brg = scatter(4, len(ORDINARY), 14, 30)
    ac.append({                                    # MILITARY: hex decides this
        "hex": "ae1f42",                           # inside US mil 0xADF7C8-0xAFFFFF
        "flight": "ZZZ900  ", "r": "N0MIL1", "t": "C130", "type": "C130",
        "lat": lat, "lon": lon, "alt_baro": 23000, "alt_geom": 23150,
        "gs": 290, "track": round(brg, 1), "baro_rate": 0,
        "squawk": "4601", "category": "A4", "seen_pos": 0.6,
    })

    lat, lon, brg = scatter(11, len(ORDINARY), 12, 26)
    ac.append({                                    # 7700 general emergency
        "hex": "a2c401",
        "flight": "ZZZ770  ", "r": "N0EMG1", "t": "B738", "type": "B738",
        "lat": lat, "lon": lon, "alt_baro": 17800, "alt_geom": 17950,
        "gs": 340, "track": round(brg, 1), "baro_rate": -1920,
        "squawk": "7700", "category": "A3", "seen_pos": 0.4,
    })

    lat, lon, brg = scatter(19, len(ORDINARY), 16, 34)
    ac.append({                                    # 7600 radio failure
        "hex": "a5d902",
        "flight": "ZZZ760  ", "r": "N0NRD1", "t": "C172", "type": "C172",
        "lat": lat, "lon": lon, "alt_baro": 7200, "alt_geom": 7350,
        "gs": 105, "track": round(brg, 1), "baro_rate": 0,
        "squawk": "7600", "category": "A1", "seen_pos": 0.9,
    })

    return {"now": now, "messages": 50000 + int(now) % 1000, "aircraft": ac}


if __name__ == "__main__":
    import sys
    doc = build()
    out = sys.argv[1] if len(sys.argv) > 1 else "aircraft.json"
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    n = len(doc["aircraft"])
    print("wrote %s: %d aircraft" % (out, n))
    print("  ordinary : %d" % (n - 3))
    print("  military : ZZZ900 hex ae1f42 (C130)")
    print("  7700     : ZZZ770 (B738)")
    print("  7600     : ZZZ760 (C172)")
