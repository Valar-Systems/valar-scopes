"""R1's week of OPERA CIRRUS DBZH frames (docs/uk-data.md, A1.3, "The week's collection").

Each run:
  1. lists the 24 h cache for today and yesterday (UTC), every DBZH .tiff and .h5 key with its LastModified, and
     appends the listing to <out>/listings.jsonl (the static-listing control reads it);
  2. for every DBZH .tiff not yet held: reads its first 8 KB, parses IFD 0 (classic little-endian TIFF), and range-
     reads the UK window's level-0 tiles, one range per tile row, keeping the bytes RAW:
       <out>/frames/<YYYYMMDDTHHMM>.tiles   the tile bytes, in tile-index order
       <out>/frames/<YYYYMMDDTHHMM>.json    key, V, LastModified (.tiff and .h5), tile indices, offsets, byte counts
  3. once per UTC day, fetches one held frame's .h5 into <out>/h5/ for the daily decode control.
Fixed before the first run: T and B are graded on the .tiff's LastModified (what the ingest reads); the .h5's is
recorded beside it. The UK window is the one cog_window.py counted (the UK land extent + 2.02 deg); its corners are
held only in this script and never printed.
Usage: python -I collect_opera.py <out_dir> <reference DBZH .h5 for the georeference>
"""
import json
import math
import os
import re
import struct
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timedelta, timezone

import h5py
from pyproj import Transformer

BASE = "https://s3.waw3-1.cloudferro.com/openradar-24h"
UA = {"User-Agent": "valar-systems-uk-radar-measurement/1"}
LAT0, LAT1, LON0, LON1, M = 49.8, 60.9, -8.7, 1.8, 2.02


def get(url, rng=None, tries=3):
    last = None
    for a in range(tries):
        try:
            h = dict(UA)
            if rng:
                h["Range"] = f"bytes={rng[0]}-{rng[1]}"
            with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=60) as r:
                return r.status, r.read()
        except Exception as e:  # recorded by the caller, never hidden
            last = e
            time.sleep(2 * (a + 1))
    raise last


def listing(prefix):
    out, token = {}, None
    while True:
        u = f"{BASE}/?list-type=2&prefix={prefix}" + (f"&continuation-token={urllib.request.quote(token)}" if token else "")
        _, body = get(u)
        x = body.decode()
        for m in re.finditer(r"<Key>([^<]+)</Key><LastModified>([^<]+)</LastModified>", x):
            if "@DBZH." in m.group(1):
                out[m.group(1)] = m.group(2)
        if "<IsTruncated>true</IsTruncated>" not in x:
            return out
        token = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x).group(1)


def window(ref_h5):
    with h5py.File(ref_h5, "r") as f:
        w = f["where"].attrs
        proj = w["projdef"].decode() if isinstance(w["projdef"], bytes) else str(w["projdef"])
        tr = Transformer.from_crs("EPSG:4326", proj, always_xy=True)
        ulx, uly = tr.transform(float(w["UL_lon"]), float(w["UL_lat"]))
        xs, ys = float(w["xscale"]), float(w["yscale"])
    la0, la1, lo0, lo1 = LAT0 - M, LAT1 + M, LON0 - M, LON1 + M
    rows, cols = [], []
    for i in range(201):
        for lon, lat in ((lo0 + (lo1 - lo0) * i / 200, la0), (lo0 + (lo1 - lo0) * i / 200, la1),
                         (lo0, la0 + (la1 - la0) * i / 200), (lo1, la0 + (la1 - la0) * i / 200)):
            x, y = tr.transform(lon, lat)
            cols.append((x - ulx) / xs)
            rows.append((uly - y) / ys)
    return math.floor(min(rows)), math.floor(max(rows)), math.floor(min(cols)), math.floor(max(cols))


def ifd0(head):
    """Classic little-endian TIFF, IFD 0: the tags this needs. Refuses anything else."""
    if head[:4] != b"II*\x00":
        raise ValueError("not a classic little-endian TIFF")
    off = struct.unpack_from("<I", head, 4)[0]
    n = struct.unpack_from("<H", head, off)[0]
    sizes = {3: 2, 4: 4, 16: 8}
    tags = {}
    for i in range(n):
        tag, typ, cnt, val = struct.unpack_from("<HHII", head, off + 2 + 12 * i)
        if tag in (256, 257, 259, 277, 284, 322, 323, 324, 325):
            sz = sizes.get(typ)
            if sz is None:
                raise ValueError(f"tag {tag} has type {typ}")
            fmt = {2: "H", 4: "I", 8: "Q"}[sz]
            if cnt * sz <= 4:
                vals = struct.unpack_from("<" + fmt * cnt, head, off + 2 + 12 * i + 8)
            else:
                if val + cnt * sz > len(head):
                    raise ValueError(f"tag {tag} array past the first {len(head)} bytes")
                vals = struct.unpack_from("<" + fmt * cnt, head, val)
            tags[tag] = vals
    return tags


def main():
    out, ref = sys.argv[1], sys.argv[2]
    os.makedirs(os.path.join(out, "frames"), exist_ok=True)
    os.makedirs(os.path.join(out, "h5"), exist_ok=True)
    run = {"start": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "errors": []}
    r0, r1, c0, c1 = window(ref)
    now = datetime.now(timezone.utc)
    lst = {}
    for d in (now - timedelta(days=1), now):
        try:
            lst.update(listing(d.strftime("%Y/%m/%d") + "/OPERA/COMP/"))
        except Exception as e:
            run["errors"].append(f"list {d:%Y%m%d}: {e}")
    with open(os.path.join(out, "listings.jsonl"), "a", encoding="utf-8") as f:
        f.write(json.dumps({"at": run["start"], "keys": lst}) + "\n")
    tiffs = sorted(k for k in lst if k.endswith("@DBZH.tiff"))
    new = 0
    for k in tiffs:
        stamp = re.search(r"OPERA@(\d{8}T\d{4})@0@DBZH", k).group(1)
        meta_p = os.path.join(out, "frames", stamp + ".json")
        if os.path.exists(meta_p):
            continue
        try:
            _, head = get(f"{BASE}/{k}", (0, 8191))
            t = ifd0(head)
            w, hgt, tw, tl = t[256][0], t[257][0], t[322][0], t[323][0]
            offs, bcs = t[324], t[325]
            tx = math.ceil(w / tw)
            idx, chunks = [], []
            for ty in range(r0 // tl, r1 // tl + 1):
                row = [ty * tx + c for c in range(c0 // tw, c1 // tw + 1)]
                lo, hi = offs[row[0]], offs[row[-1]] + bcs[row[-1]] - 1
                _, b = get(f"{BASE}/{k}", (lo, hi))
                if len(b) != hi - lo + 1:
                    raise ValueError(f"short range {len(b)} of {hi - lo + 1}")
                for i in row:
                    chunks.append(b[offs[i] - lo: offs[i] - lo + bcs[i]])
                    idx.append(i)
            with open(os.path.join(out, "frames", stamp + ".tiles"), "wb") as f:
                for c in chunks:
                    f.write(c)
            h5k = k[:-5] + ".h5"
            meta = {"key": k, "V": stamp, "lm_tiff": lst[k], "lm_h5": lst.get(h5k), "size": [w, hgt], "tile": [tw, tl],
                    "tiles_x": tx, "window": [r0, r1, c0, c1], "idx": idx, "offsets": [offs[i] for i in idx],
                    "bytecounts": [bcs[i] for i in idx], "samples": t.get(277), "compression": t.get(259)}
            with open(meta_p, "w", encoding="utf-8") as f:
                json.dump(meta, f)
            new += 1
        except Exception as e:
            run["errors"].append(f"{stamp}: {e}")
    day = now.strftime("%Y%m%d")
    if not any(n.startswith(day) for n in os.listdir(os.path.join(out, "h5"))):
        held = sorted(n[:-5] for n in os.listdir(os.path.join(out, "frames")) if n.startswith(day) and n.endswith(".json"))
        if held:
            stamp = held[len(held) // 2]
            key = f"{stamp[:4]}/{stamp[4:6]}/{stamp[6:8]}/OPERA/COMP/OPERA@{stamp}@0@DBZH.h5"
            try:
                _, b = get(f"{BASE}/{key}")
                with open(os.path.join(out, "h5", stamp + ".h5"), "wb") as f:
                    f.write(b)
            except Exception as e:
                run["errors"].append(f"h5 {stamp}: {e}")
    run.update({"end": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"), "listed_tiffs": len(tiffs),
                "new_frames": new, "held_frames": sum(1 for n in os.listdir(os.path.join(out, "frames")) if n.endswith(".json"))})
    with open(os.path.join(out, "runs.jsonl"), "a", encoding="utf-8") as f:
        f.write(json.dumps(run) + "\n")
    print(json.dumps(run))


main()
