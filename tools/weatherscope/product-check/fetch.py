"""List and download MRMS files from noaa-mrms-pds (public). Picks the file nearest a target time."""
import os, re, sys, urllib.request
from datetime import datetime, timedelta

BASE = "https://noaa-mrms-pds.s3.amazonaws.com"
PRODUCTS = {"comp": "MergedReflectivityQCComposite_00.50", "hsr": "SeamlessHSR_00.00"}
_listing = {}

def list_day(product, day):
    key = (product, day)
    if key in _listing:
        return _listing[key]
    prefix = f"CONUS/{PRODUCTS[product]}/{day:%Y%m%d}/"
    names, token = [], None
    while True:
        url = f"{BASE}/?list-type=2&prefix={prefix}" + (f"&continuation-token={urllib.request.quote(token)}" if token else "")
        x = urllib.request.urlopen(url, timeout=60).read().decode()
        names += re.findall(r"<Key>([^<]+)</Key>", x)
        m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x)
        if "<IsTruncated>true</IsTruncated>" not in x or not m:
            break
        token = m.group(1)
    out = []
    for k in names:
        m = re.search(r"_(\d{8}-\d{6})\.grib2\.gz$", k)
        if m:
            out.append((datetime.strptime(m.group(1), "%Y%m%d-%H%M%S"), k))
    _listing[key] = out
    return out

def nearest(product, t):
    cands = []
    for d in (t.date() - timedelta(days=1), t.date(), t.date() + timedelta(days=1)):
        if abs((datetime(d.year, d.month, d.day) + timedelta(hours=12) - t).total_seconds()) < 36 * 3600:
            cands += list_day(product, datetime(d.year, d.month, d.day))
    if not cands:
        raise SystemExit(f"no {product} files near {t}")
    return min(cands, key=lambda c: abs((c[0] - t).total_seconds()))

def download(key, dest_dir):
    os.makedirs(dest_dir, exist_ok=True)
    path = os.path.join(dest_dir, key.rsplit("/", 1)[1])
    if not os.path.exists(path):
        tmp = path + ".part"
        urllib.request.urlretrieve(f"{BASE}/{key}", tmp)
        os.replace(tmp, path)
    return path
