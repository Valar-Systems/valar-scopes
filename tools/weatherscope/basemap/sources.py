"""The BMPK host tool's sources: where each layer's files live, how one is fetched and recorded, and which files a
square reads (format doc §5, §7 and §13; docs/weatherscope-stage3.md A12.3-A12.4).

The URL forms are the map server's (`proxy/scripts/basemap/sources.ts` on #486), so the two builders' `sha256.<layer>`
lines can agree byte for byte. Nothing here prints a coordinate.

A fetched file lives at <dir>/<host>/<path>, with <that>.meta.json beside it: {url, sha256, bytes, last_modified}. A
file already present with a meta is not fetched again; its sha256 is re-checked against the meta on every read.
"""
import email.utils
import hashlib
import json
import math
import os
import urllib.request

TIGER = "https://www2.census.gov/geo/tiger/TIGER2024/"
GNIS = "https://prd-tnm.s3.amazonaws.com/StagedProducts/GeographicNames/DomesticNames/"
GAZETTEER = "https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip"
SUBEST = "https://www2.census.gov/programs-surveys/popest/datasets/2020-2024/cities/totals/sub-est2024.csv"
STATES = TIGER + "STATE/tl_2024_us_state.zip"
COUNTIES = TIGER + "COUNTY/tl_2024_us_county.zip"

# Postal codes by state FIPS, for GNIS's per-state file names (the 50 states and DC; v1 serves no territory).
POSTAL = {"01": "AL", "02": "AK", "04": "AZ", "05": "AR", "06": "CA", "08": "CO", "09": "CT", "10": "DE", "11": "DC",
          "12": "FL", "13": "GA", "15": "HI", "16": "ID", "17": "IL", "18": "IN", "19": "IA", "20": "KS", "21": "KY",
          "22": "LA", "23": "ME", "24": "MD", "25": "MA", "26": "MI", "27": "MN", "28": "MS", "29": "MO", "30": "MT",
          "31": "NE", "32": "NV", "33": "NH", "34": "NJ", "35": "NM", "36": "NY", "37": "NC", "38": "ND", "39": "OH",
          "40": "OK", "41": "OR", "42": "PA", "44": "RI", "45": "SC", "46": "SD", "47": "TN", "48": "TX", "49": "UT",
          "50": "VT", "51": "VA", "53": "WA", "54": "WV", "55": "WI", "56": "WY"}


def areawater(geoid):
    return f"{TIGER}AREAWATER/tl_2024_{geoid}_areawater.zip"


def linearwater(geoid):
    return f"{TIGER}LINEARWATER/tl_2024_{geoid}_linearwater.zip"


def prisecroads(state):
    return f"{TIGER}PRISECROADS/tl_2024_{state}_prisecroads.zip"


def gnis(state):
    return f"{GNIS}DomesticNames_{POSTAL[state]}_Text.zip"


def terrarium(x, y):
    return f"https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/{x}/{y}.png"


class Refused(Exception):
    pass


def magic_ok(url, head):
    """The body is the file it claims to be, by its own first bytes (census.gov answers a rejected request with an
    HTML page under status 200: the map server's finding)."""
    if url.endswith(".zip"):
        return head[:4] in (b"PK\x03\x04", b"PK\x05\x06")
    if url.endswith(".png"):
        return head[:8] == b"\x89PNG\r\n\x1a\n"
    if url.endswith(".csv"):
        return len(head) > 0 and head[:1] != b"<"
    return False


def local_path(root, url):
    rest = url.split("://", 1)[1]
    return os.path.join(root, *rest.split("/"))


def vintage(url, last_modified):
    """RD-P3: the year in the dataset's name where it has one, otherwise the year of the Last-Modified as fetched."""
    if "/TIGER2024/" in url:
        return "2024"
    if "/2025_Gazetteer/" in url:
        return "2025"
    if url.endswith("sub-est2024.csv"):
        return "2024"
    if not last_modified:
        raise Refused(f"no Last-Modified for {url}: refused, never defaulted")
    return str(email.utils.parsedate_to_datetime(last_modified).year)


def fetch(root, url):
    """The file's meta, fetching it when it is not already present. Refuses a body that fails its magic."""
    path = local_path(root, url)
    meta_path = path + ".meta.json"
    if os.path.exists(path) and os.path.exists(meta_path):
        return read_meta(root, url)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "valar-scopes basemap host tool (weatherscope)"})
    with urllib.request.urlopen(req, timeout=300) as r:
        status = r.status
        ctype = r.headers.get("Content-Type") or ""
        last_modified = r.headers.get("Last-Modified")
        body = r.read()
    if status != 200:
        raise Refused(f"{url}: HTTP {status}")
    if "text/html" in ctype.lower() or not magic_ok(url, body[:8]):
        raise Refused(f"{url}: not the file it claims to be ({ctype or 'no type'}, {len(body)} B)")
    meta = {"url": url, "sha256": hashlib.sha256(body).hexdigest(), "bytes": len(body), "last_modified": last_modified}
    tmp = path + ".part"
    with open(tmp, "wb") as fh:
        fh.write(body)
    os.replace(tmp, path)
    with open(meta_path, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(meta, fh, indent=1, sort_keys=True)
        fh.write("\n")
    return meta


def read_meta(root, url):
    path = local_path(root, url)
    with open(path + ".meta.json", encoding="utf-8") as fh:
        meta = json.load(fh)
    body = open(path, "rb").read()
    if hashlib.sha256(body).hexdigest() != meta["sha256"] or len(body) != meta["bytes"] or meta["url"] != url:
        raise Refused(f"{url}: the local copy no longer matches its meta")
    return meta


def read_bytes(root, url, want_sha=None):
    meta = read_meta(root, url)
    if want_sha is not None and meta["sha256"] != want_sha:
        raise Refused(f"{url}: sha256 {meta['sha256'][:12]} is not the manifest's {want_sha[:12]}")
    return open(local_path(root, url), "rb").read()


# ------------------------------------------------------------------------------------------------- the frame (§2)

K = 111.0


def frame(lat0_micro, lon0_micro):
    lat0, lon0 = lat0_micro / 1e6, lon0_micro / 1e6
    c0 = max(math.cos(lat0 * (math.pi / 180)), 0.01)
    return lat0, lon0, c0


def latlon_box(lat0_micro, lon0_micro, r_mm):
    """The square [-R, R]^2 as a latitude/longitude box (RD-P6), from §2's metric inverted exactly."""
    lat0, lon0, c0 = frame(lat0_micro, lon0_micro)
    r_km = r_mm / 1e6
    return (lat0 - r_km / K, lon0 - r_km / (K * c0), lat0 + r_km / K, lon0 + r_km / (K * c0))


def box_meets(rec_box, box):
    """RD-P6: a TIGER record's box (xmin, ymin, xmax, ymax) meets the square's (latmin, lonmin, latmax, lonmax),
    closed."""
    xmin, ymin, xmax, ymax = rec_box
    latmin, lonmin, latmax, lonmax = box
    return xmin <= lonmax and xmax >= lonmin and ymin <= latmax and ymax >= latmin


def z9_pixel(lat, lon):
    """RD21: the global z9 pixel (X, Y) containing a point."""
    X = math.floor((lon + 180) / 360 * 2 ** 17)
    phi = lat * math.pi / 180
    Y = math.floor((1 - math.log(math.tan(math.pi / 4 + phi / 2)) / math.pi) / 2 * 2 ** 17)
    return X, Y
