"""M1's sources (docs/uk-data.md A2.4): fetch each once, keep only the members M1 reads, record a manifest.

Usage: python -I m1_fetch.py <dest_dir>
Writes <dest>/manifest.json: per file, its URL, sha256, bytes and Last-Modified, and which members were kept.
Zips are deleted after their members are extracted, so the disk holds only what M1 reads.
"""
import hashlib
import json
import os
import re
import sys
import time
import urllib.request
import zipfile

UA = {"User-Agent": "BlipscopeBasemapM1/1 (+https://github.com/Valar-Systems/valar-scopes)"}
dest = sys.argv[1]
os.makedirs(dest, exist_ok=True)
man_p = os.path.join(dest, "manifest.json")
manifest = json.load(open(man_p)) if os.path.exists(man_p) else {"files": []}
done = {f["url"] for f in manifest["files"]}


def fetch(url, path, tries=3):
    for a in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=300) as r, open(path, "wb") as f:
                h = hashlib.sha256()
                n = 0
                while True:
                    b = r.read(1 << 20)
                    if not b:
                        break
                    h.update(b)
                    f.write(b)
                    n += len(b)
                return {"sha256": h.hexdigest(), "bytes": n, "last_modified": r.headers.get("Last-Modified")}
        except Exception as e:  # recorded, then retried; the last failure is raised
            last = e
            time.sleep(5 * (a + 1))
    raise last


def take(layer, url, keep=None, name=None):
    """Fetch url into <dest>/<layer>/; if a zip, extract members matching `keep` and delete the zip."""
    if url in done:
        return
    d = os.path.join(dest, layer)
    os.makedirs(d, exist_ok=True)
    name = name or url.split("?")[0].rstrip("/").split("/")[-1] or "download"
    path = os.path.join(d, name)
    rec = fetch(url, path)
    rec.update({"layer": layer, "url": url})
    if keep is not None and zipfile.is_zipfile(path):
        kept = []
        with zipfile.ZipFile(path) as z:
            for m in z.namelist():
                if re.search(keep, m):
                    z.extract(m, d)
                    kept.append(m)
        os.remove(path)
        rec["kept"] = kept
    manifest["files"].append(rec)
    json.dump(manifest, open(man_p, "w"), indent=1, sort_keys=True)
    print(layer, rec["bytes"], rec.get("kept", [name])[:3], flush=True)


def os_downloads(product, fmt):
    with urllib.request.urlopen(urllib.request.Request(
            f"https://api.os.uk/downloads/v1/products/{product}/downloads", headers=UA), timeout=60) as r:
        return [x for x in json.load(r) if x.get("format") == fmt]


# GB: Ordnance Survey OpenData (OGL v3)
for x in os_downloads("OpenRivers", "ESRI® Shapefile"):
    take("os-rivers", x["url"], r"WatercourseLink\.(shp|shx|dbf|prj)$", x["fileName"])
for x in os_downloads("OpenRoads", "ESRI® Shapefile"):
    take("os-roads", x["url"], r"_RoadLink\.(shp|shx|dbf|prj)$", x["fileName"])
for x in os_downloads("OpenNames", "CSV"):
    take("os-names", x["url"], r"(\.csv$|Header)", x["fileName"])
for x in sorted(os_downloads("OpenMapLocal", "ESRI® Shapefile"), key=lambda x: x["area"]):
    if x["area"] != "GB":
        take("os-water", x["url"], r"SurfaceWater_Area\.(shp|shx|dbf|prj)$", x["fileName"])
# Populations (OGL v3): ONS (England and Wales), NRS (Scotland), NISRA (NI)
take("ons", "https://www.ons.gov.uk/file?uri=/peoplepopulationandcommunity/housing/datasets/"
     "townsandcitiescharacteristicsofbuiltupareasenglandandwalescensus2021/2021/"
     "townsandcitiescharacteristicsofbuiltupareasenglandandwalescensus2021.xlsx", name="ons-bua-census2021.xlsx")
BUA = ("https://services1.arcgis.com/ESMARspQHYMw9BZ9/arcgis/rest/services/BUA_2022_GB/FeatureServer/0/query"
       "?where=1%3D1&outFields=BUA22CD,BUA22NM&outSR=27700&f=geojson&orderByFields=BUA22CD&resultRecordCount=1000"
       "&resultOffset=")
for off in range(0, 9000, 1000):  # the service caps a page at 1,000 (its exceededTransferLimit), so step by 1,000
    take("ons-bua", BUA + str(off), name=f"bua2022-{off:05d}.geojson")
take("nrs", "https://www.nrscotland.gov.uk/media/c3xbhayl/census-settlement-2022-mhw.zip", r"\.(shp|shx|dbf|prj|cpg)$")
take("nisra", "https://www.nisra.gov.uk/system/files/statistics/census-2021-commissioned-table-ct0046.xlsx",
     name="ct0046.xlsx")
take("nisra", "https://www.nisra.gov.uk/files/nisra/publications/SDL15-ESRI_format.zip", r"\.(shp|shx|dbf|prj|cpg)$")
# Northern Ireland layers (OpenDataNI, labelled OGL; served only once LPS answers, A1.1b)
take("osni", "https://admin.opendatani.gov.uk/dataset/bdcc2953-ed72-4f21-b012-43cbd5bbc395/resource/"
     "c576a4c4-67d1-45cc-89bf-0f6025ad133e/download/osni_open_data_50k_transport_transport_lines.zip",
     r"\.(shp|shx|dbf|prj|cpg)$")
take("osni", "https://admin.opendatani.gov.uk/dataset/d27903f1-15e6-4c07-8564-ddc655e9c549/resource/"
     "411c6ea5-c079-43aa-bd62-438423d070cc/download/osni_open_data_-_gazetteer_-_place_names.csv",
     name="osni-gazetteer-place-names.csv")
take("daera", "https://opendata-daerani.hub.arcgis.com/api/download/v1/items/5acc99bfd09e432a9db6d72272b96174/"
     "shapefile?layers=0", r"\.(shp|shx|dbf|prj|cpg)$", name="lakes-of-northern-ireland.zip")
print("done", len(manifest["files"]), "files", flush=True)
