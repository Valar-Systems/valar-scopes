"""WXC1 v2, measurement 4 (docs/wxc1-v2.md section 4 and Addenda 1-4, all frozen before this ran).

    python -I tools/weatherscope/m4/m4.py <scratchpad> run          the measurement
    python -I tools/weatherscope/m4/m4.py <scratchpad> --selftest   the grading rules against synthetic cases

<scratchpad> holds what Stage 2 left there: spike-a5db678/ (the decoder and cropBox Stage 2 pinned) and
wxdata2/ (results2.json, selection.json, and change 1's SeamlessHSR and composite files under grib/). PrecipFlag
files are downloaded to wxdata2/grib/pf/ and per-hour counts written to wxdata2/m4/. Nothing here goes into git,
and no coordinate is printed.

THE ORDER IS THE FROZEN ONE (A1.7, with Addenda 2-4), AND A STOP LEAVES EVERY LATER ROW UNREAD:
  1. the instrument's checks: the listing holds Stage 2's files; C-M4e (the any-width decoder against mrms.ts);
     C-M4a (packing, the width and scale sabotages, the census);
     C-M4b (the anchor); the run counter against the spike encoder (an instrument check, not a frozen row)
  2. M4a (cadence), M4b (pairing; its next-hour sabotage first), M4i (C-M4d first), M4c, M4d (its two sabotages first)
  3. C-M4c's three sabotages, then C-M4c
  4. only after C-M4c's outcome (i): M4e, M4f, M4g, M4h
Exit 0: every row read. Exit 1: a pre-registered stop, or a sabotage that did not bite. Exit 2: the instrument failed.

Decided here before the run, as the frozen text implies and no more:
  - The decode pairs each hour with its NEAREST PrecipFlag file. A hour whose paired file (by M4b's rule) is not
    the decoded one is excluded as missing under (a); under (b) it is an instrument stop (re-run with the paired
    files), never a silent substitution.
  - Under A1.3 a non-missing hour is by definition at the modal offset, so M4b's outcome (c) cannot occur; varying
    offsets show up as missing hours, and more than 2 stops.
  - M4a: a window with fewer than 2 files cannot show a cadence: "anything else", stop.
  - M4a and M4c have no frozen sabotage, and none is invented here.
"""
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from collections import Counter
from datetime import datetime, timedelta
from fractions import Fraction

BASE = "https://noaa-mrms-pds.s3.amazonaws.com"
PRODUCTS = {"hsr": "SeamlessHSR_00.00", "comp": "MergedReflectivityQCComposite_00.50", "pf": "PrecipFlag_00.00"}
HERE = os.path.dirname(os.path.abspath(__file__))
PF_TABLE = {-3, 0, 1, 3, 6, 7, 10, 91, 96}
K_BOUNDARY = 8                       # A2.1
STAGE2_HIST = {"hsr": [7, 12, 1, 3, 1], "comp": [4, 12, 4, 3, 1]}   # docs/weatherscope-stage2.md, change 1
FIELDS = ["ni", "nj", "la1", "lo1", "la2", "lo2", "di", "dj", "w2e", "n2s"]


class Stop(Exception):
    pass


class InstrumentFailure(Exception):
    pass


def pct(f):
    return f"{100 * float(f):.1f} %"


# ---------------------------------------------------------------------------------------------- listings, files
def list_day(product, day):
    prefix = f"CONUS/{PRODUCTS[product]}/{day:%Y%m%d}/"
    out, token = [], None
    while True:
        url = f"{BASE}/?list-type=2&prefix={prefix}"
        if token:
            url += "&continuation-token=" + urllib.parse.quote(token)
        x = urllib.request.urlopen(url, timeout=60).read().decode()
        for blk in re.findall(r"<Contents>(.*?)</Contents>", x, re.S):
            key = re.search(r"<Key>([^<]+)</Key>", blk).group(1)
            lm = re.search(r"<LastModified>([^<]+)</LastModified>", blk).group(1)
            m = re.search(r"_(\d{8}-\d{6})\.grib2\.gz$", key)
            if m:
                out.append((datetime.strptime(m.group(1), "%Y%m%d-%H%M%S"), key,
                            datetime.strptime(lm[:19], "%Y-%m-%dT%H:%M:%S")))
        m = re.search(r"<NextContinuationToken>([^<]+)</NextContinuationToken>", x)
        if "<IsTruncated>true</IsTruncated>" in x and m:
            token = m.group(1)
        else:
            return out


def listing(product, times):
    days = sorted({(t + timedelta(seconds=s)).date() for t in times for s in (-900, 0, 900)})
    out = []
    for d in days:
        out += list_day(product, datetime(d.year, d.month, d.day))
    return sorted(out)


def download(key, dest_dir):
    os.makedirs(dest_dir, exist_ok=True)
    path = os.path.join(dest_dir, key.rsplit("/", 1)[1])
    if not os.path.exists(path):
        tmp = path + ".part"
        urllib.request.urlretrieve(f"{BASE}/{key}", tmp)
        os.replace(tmp, path)
    return path


# ------------------------------------------------------------------------------------------- grading rules (pure)
def classify_m4b(offsets, target=0):
    """offsets: per hour, the offsets (s) from the SeamlessHSR time of the PrecipFlag files within +-15 min of
    the pairing target (0 for the real run; +3600 for the next-hour sabotage). Returns (outcome, modal,
    missing_idx). Outcome: 'a', 'b', or a Stop. A1.3 + A4.2."""
    nearest = [min(o, key=lambda v: abs(v - target)) if o else None for o in offsets]
    if any(n is None for n in nearest):
        raise Stop("M4b: an hour has no PrecipFlag file within 15 min: anything else")
    counts = Counter(nearest).most_common()
    if len(counts) > 1 and counts[0][1] == counts[1][1]:
        raise Stop(f"M4b: no single modal offset ({counts[:3]}): anything else")
    modal = counts[0][0]
    if abs(modal) >= 120:
        raise Stop(f"M4b: modal offset {modal:+d} s pairs a different frame (|offset| >= 120 s, A4.2)")
    missing = [i for i, o in enumerate(offsets) if modal not in o]
    if len(missing) > 2:
        raise Stop(f"M4b: {len(missing)} missing hours (> 2): untyped crops would be common. Stop and decide")
    return ("a" if modal == 0 else "b"), modal, missing


def classify_iou(iou_by_offset, boundary_hours, a00):
    """C-M4c (A2.1). iou_by_offset: name -> Fraction, '0,0' included. Returns ('i'|'ii'|'iii'|'iv', detail)."""
    if boundary_hours < K_BOUNDARY:
        return "iv", f"{boundary_hours} boundary hours < K = {K_BOUNDARY}"
    i00 = iou_by_offset["0,0"]
    others = {n: x for n, x in iou_by_offset.items() if n != "0,0"}
    top = max(others.values())
    tops = sorted(n for n, x in others.items() if x == top)
    if top > i00:
        return "ii", f"strict excess at {tops}"
    if top == i00:
        return "iv", f"tie at the top with {tops}"
    if a00 >= Fraction(95, 100):
        return "i", "strict peak at (0,0) and A(0,0) >= 95 %"
    return "iii", f"strict peak at (0,0) but A(0,0) = {pct(a00)}"


def selftest():
    F = Fraction
    base = {"0,0": F(9, 10), "1,0": F(8, 10), "0,-10": F(5, 10), "flipNS": F(1, 10)}
    cases = [
        ("aligned", classify_iou(base, 20, F(97, 100))[0], "i"),
        ("aligned, low A", classify_iou(base, 20, F(90, 100))[0], "iii"),
        ("strict excess", classify_iou({**base, "0,-1": F(95, 100)}, 20, F(99, 100))[0], "ii"),
        ("tie at top", classify_iou({**base, "0,-1": F(9, 10)}, 20, F(99, 100))[0], "iv"),
        ("few boundaries", classify_iou(base, 7, F(99, 100))[0], "iv"),
    ]
    def m4b(o):
        try:
            return classify_m4b(o)[0]
        except Stop:
            return "STOP"
    cases += [
        ("m4b exact", m4b([[0, 120]] * 24), "a"),
        ("m4b 2 missing", m4b([[0]] * 22 + [[-120, 120]] * 2), "a"),
        ("m4b 3 missing", m4b([[0]] * 21 + [[-120, 120]] * 3), "STOP"),
        ("m4b constant 30", m4b([[30, -90]] * 24), "b"),
        ("m4b next hour (A4.2)", m4b([[3480, 3600, 3720]] * 24), "STOP"),
        ("m4b +120", m4b([[120]] * 24), "STOP"),
    ]
    bad = [c for c in cases if c[1] != c[2]]
    for name, got, want in cases:
        print(f"  {'ok  ' if got == want else 'FAIL'} {name}: {got} (want {want})")
    print("SELFTEST", "PASSED" if not bad else "FAILED")
    return 0 if not bad else 1


# ------------------------------------------------------------------------------------------------------- the run
def run(sp):
    spike = os.path.join(sp, "spike-a5db678")
    airports = os.path.join(spike, "tools", "wx", "airports-a2.txt")
    data = os.path.join(sp, "wxdata2")
    out_dir = os.path.join(data, "m4")
    os.makedirs(os.path.join(out_dir, "hours"), exist_ok=True)
    icao = json.load(open(os.path.join(data, "selection.json")))["F4"]["icao"]
    T = lambda s: datetime.strptime(s, "%Y-%m-%dT%H:%M:%S")
    hours = [{"t": T(e["t"]), "hsr_t": T(e["hsr_file"]), "comp_t": T(e["comp_file"]),
              "hsr_raw": e["hsr"]["raw"], "comp_raw": e["comp"]["raw"]}
             for e in json.load(open(os.path.join(data, "results2.json")))["change1"]]
    print(f"M4: {len(hours)} change-1 hours at {icao}, {hours[0]['t']:%Y-%m-%d %H:%M}Z to {hours[-1]['t']:%Y-%m-%d %H:%M}Z")
    if len(hours) != 24:
        raise InstrumentFailure(f"{len(hours)} hours in results2.json, want 24")

    # ---- listings; the listing control: Stage 2's own files must be in it ----
    L = {p: listing(p, [h["hsr_t"] for h in hours] + [h["hsr_t"] + timedelta(hours=1) for h in hours])
         for p in PRODUCTS}
    by_t = {p: {t: (k, lm) for t, k, lm in L[p]} for p in L}
    for h in hours:
        if h["hsr_t"] not in by_t["hsr"] or h["comp_t"] not in by_t["comp"]:
            raise InstrumentFailure(f"listing: Stage 2's file for {h['t']} is not in the listing")
    print("listing: all 48 of Stage 2's files present (object totals are not printed: they bear on M4a)")
    pf_times = sorted(by_t["pf"])

    def offsets_near(t, shift=0):
        """Offsets from t of the PrecipFlag files within 15 min of t + shift."""
        c = t + timedelta(seconds=shift)
        return [int((x - t).total_seconds()) for x in pf_times if abs((x - c).total_seconds()) <= 900]

    # ---- download + decode: each hour with its NEAREST PrecipFlag file ----
    for h in hours:
        near = min(pf_times, key=lambda x: abs((x - h["hsr_t"]).total_seconds()))
        h["pf_decoded_t"] = near
        files = {}
        for p, t in (("hsr", h["hsr_t"]), ("comp", h["comp_t"]), ("pf", near)):
            files[p] = download(by_t[p][t][0], os.path.join(data, "grib", p))
        out = os.path.join(out_dir, "hours", f"{h['t']:%Y%m%d-%H%M}.json")
        r = subprocess.run(["node", "--experimental-strip-types", os.path.join(HERE, "m4frame.ts"), spike, airports,
                            icao, files["hsr"], files["comp"], files["pf"], out], capture_output=True, text=True)
        if r.returncode != 0:
            raise InstrumentFailure(f"m4frame.ts failed for {h['t']}, its whole stderr follows:{os.linesep}{r.stderr.strip()}")
        h["m"] = json.load(open(out))
    print(f"decoded: {len(hours)} hours x 3 files")

    # ================================ 1. the instrument's checks =================================================
    am = [(f"{h['t']:%m-%d %H:%M}", p, h["m"]["products"][p]["anchorMismatch"]) for h in hours for p in ("hsr", "comp")
          if h["m"]["products"][p]["anchorMismatch"]]
    print(f"C-M4e: the any-width decoder reproduces mrms.ts on {48 - len(am)}/48 SeamlessHSR and composite files")
    if am:
        raise InstrumentFailure(f"C-M4e: the any-width decoder disagrees with mrms.ts: {am[:4]}")
    packs = Counter((h["m"]["meta"]["pf"]["bits"], h["m"]["meta"]["pf"]["R"], h["m"]["meta"]["pf"]["E"],
                     h["m"]["meta"]["pf"]["D"]) for h in hours)
    print(f"C-M4a packing (bits, R, E, D): {dict(packs)}")
    if set(packs) != {(8, -3.0, 0, 0)}:
        raise Stop("C-M4a: the packing is not 8 bits at (-3, 0, 0), which Addendum 5 predicted. Stop and decide")
    w_hours = sum(1 for h in hours if h["m"]["census"]["sabWidthOutOfTable"])
    w_total = sum(h["m"]["census"]["sabWidthOutOfTable"] for h in hours)
    print(f"C-M4a sabotage (8-bit pairs read as 16-bit): out-of-table values in {w_hours}/24 hours ({w_total:,} cells) -> "
          f"{'REFUSED, as predicted' if w_hours else 'NOT refused: the sabotage did not bite'}")
    if not w_hours:
        raise Stop("C-M4a's width sabotage did not bite")
    sab_hours = sum(1 for h in hours if h["m"]["census"]["sabScaleOutOfTable"] > 0)
    sab_total = sum(h["m"]["census"]["sabScaleOutOfTable"] for h in hours)
    print(f"C-M4a sabotage (R + 2X): out-of-table values in {sab_hours}/24 hours ({sab_total:,} cells) -> "
          f"{'REFUSED, as predicted' if sab_hours else 'NOT refused: the sabotage did not bite'}")
    if not sab_hours:
        raise Stop("C-M4a's sabotage did not bite")
    bad = [(f"{h['t']:%m-%d %H:%M}", h["m"]["census"]["outOfTable"], h["m"]["census"]["nonInteger"], h["m"]["census"]["odd"])
           for h in hours if h["m"]["census"]["outOfTable"] or h["m"]["census"]["nonInteger"]]
    if bad:
        raise InstrumentFailure(f"C-M4a census: values outside the table: {bad[:5]}")
    print("C-M4a census: every PrecipFlag value in all 24 files is in NSSL's table -> pass")

    def bins(vals):
        b = [0] * 5
        for v in vals:
            if v == -999:
                raise InstrumentFailure("C-M4b: a -999 at the centre")
            b[0 if v == -99 else 1 if v < 5 else 2 if v < 10 else 3 if v < 15 else 4] += 1
        return b
    miss = [f"{h['t']:%m-%d %H:%M}" for h in hours
            if h["m"]["centre"]["hsr"] != h["hsr_raw"] or h["m"]["centre"]["comp"] != h["comp_raw"]]
    hist = {"hsr": bins(h["m"]["centre"]["hsr"] for h in hours), "comp": bins(h["m"]["centre"]["comp"] for h in hours)}
    print(f"C-M4b anchor: centre values equal Stage 2's in {24 - len(miss)}/24 hours; histograms {hist} "
          f"vs committed {STAGE2_HIST}")
    if miss or hist != STAGE2_HIST:
        raise InstrumentFailure(f"C-M4b: the anchor does not reproduce Stage 2 (hours {miss[:5]})")
    cb = [(f"{h['t']:%m-%d %H:%M}", p) for h in hours for p in ("hsr", "comp")
          if h["m"]["products"][p]["codeBytes"] != h["m"]["products"][p]["encoderCodeBytes"]]
    if cb:
        raise InstrumentFailure(f"run counter disagrees with the spike encoder at coarsen 1: {cb[:4]}")
    print("instrument: the run counter equals the spike encoder's coarsen-1 body in all 48 crops")

    # ================================ 2. timing and grid rows =====================================================
    ivs, short = [], 0
    for h in hours:
        w = [x for x in pf_times if abs((x - h["hsr_t"]).total_seconds()) <= 600]
        if len(w) < 2:
            short += 1
        ivs += [int((b - a).total_seconds()) for a, b in zip(w, w[1:])]
    if short:
        raise Stop(f"M4a: {short} windows hold fewer than 2 PrecipFlag files: anything else")
    if any(iv % 120 for iv in ivs):
        raise Stop(f"M4a: intervals not multiples of 120 s: {sorted(set(iv for iv in ivs if iv % 120))[:5]}")
    mode = Counter(ivs).most_common(1)[0][0]
    gaps = sum(iv // 120 - 1 for iv in ivs)
    slots = sum(iv // 120 for iv in ivs)
    print(f"M4a cadence: {len(ivs)} intervals, mode {mode} s, {gaps} gaps in {slots} slots ({pct(Fraction(gaps, slots))}); "
          f"predicted 120 s with gaps <= 5 % (no frozen sabotage)")
    if mode != 120:
        raise Stop(f"M4a: modal interval {mode} s")
    if Fraction(gaps, slots) > Fraction(5, 100):
        raise Stop("M4a: gaps above 5 %. Stop and decide")

    try:
        classify_m4b([offsets_near(h["hsr_t"], 3600) for h in hours], target=3600)
        raise Stop("M4b's sabotage (next hour's file) did not stop: the rule cannot see a wrong pairing")
    except Stop as e:
        if "did not stop" in str(e):
            raise
        print(f"M4b sabotage (next hour's file): stops, as predicted ({e})")
    offs = [offsets_near(h["hsr_t"]) for h in hours]
    outcome, modal, missing = classify_m4b(offs)
    nearest = Counter(min(o, key=abs) for o in offs)
    print(f"M4b pairing: outcome ({outcome}), modal offset {modal:+d} s, {len(missing)} missing hours "
          f"(nearest offsets {dict(nearest)}); predicted (a) with <= 2 missing")
    for i, h in enumerate(hours):
        h["pf_t"] = None if i in missing else h["hsr_t"] + timedelta(seconds=modal)
        if h["pf_t"] is not None and h["pf_t"] != h["pf_decoded_t"]:
            raise InstrumentFailure(f"{h['t']}: the paired file is not the decoded one; re-run with the paired files")
    used = [h for h in hours if h["pf_t"] is not None]

    lm_hsr = [int((by_t["hsr"][h["hsr_t"]][1] - h["hsr_t"]).total_seconds()) for h in hours]
    cm4d = all(0 <= x <= 300 for x in lm_hsr)
    print(f"C-M4d: SeamlessHSR LastModified - nominal: {min(lm_hsr)}..{max(lm_hsr)} s -> "
          f"{'real-time publication (0-300 s in 24/24)' if cm4d else 'NOT real-time: M4i is uninformative (c)'}")
    if cm4d:
        lag = [int((by_t["pf"][h["pf_t"]][1] - by_t["hsr"][h["hsr_t"]][1]).total_seconds()) for h in used]
        lagc = [int((by_t["pf"][h["pf_t"]][1] - by_t["comp"][h["comp_t"]][1]).total_seconds()) for h in used]
        print(f"M4i: PrecipFlag - SeamlessHSR LastModified {min(lag)}..{max(lag)} s over {len(used)} hours "
              f"(vs composite {min(lagc)}..{max(lagc)} s); predicted within +-60 s")
        if all(-60 <= x <= 60 for x in lag):
            print("  M4i outcome (a): 'untyped, then typed' is a rare edge")
        elif all(x >= -60 for x in lag):
            print(f"  M4i outcome (b): PrecipFlag > 60 s later in {sum(x > 60 for x in lag)} hours: P3(b) is load-bearing")
        else:
            raise Stop("M4i: a PrecipFlag file more than 60 s EARLY: anything else (d). Stop and decide")

    # The PrecipFlag time here is the file's OWN reference time (GRIB section 1, as decoded), not its name: under
    # (a) the name-derived time equals the SeamlessHSR time by construction, and the check would test nothing.
    for h in used:
        h["pf_valid"] = datetime.strptime(h["m"]["meta"]["pf"]["validTimeUtc"][:19], "%Y-%m-%dT%H:%M:%S")
    if outcome == "a":
        dif = [f"{h['t']:%m-%d %H:%M}" for h in used
               if (h["comp_t"] - h["pf_valid"]).total_seconds() != (h["comp_t"] - h["hsr_t"]).total_seconds()]
        print(f"M4c (sharpened, A1.4): composite - PrecipFlag (its own reference time) equals Stage 2's "
              f"composite - SeamlessHSR in {len(used) - len(dif)}/{len(used)} hours (no frozen sabotage)")
        if dif:
            raise Stop(f"M4c: differs in {dif[:5]}")
    else:
        d = [abs((h["comp_t"] - h["pf_valid"]).total_seconds()) for h in used]
        print(f"M4c (band form): |composite - PrecipFlag| {min(d):.0f}..{max(d):.0f} s; predicted 31-43 s")
        if not all(31 <= x <= 43 for x in d):
            raise Stop("M4c: outside 31-43 s")

    def diff(a, b):
        return [f for f in FIELDS if a[f] != b[f]]
    m0 = hours[0]["m"]["meta"]
    for name, field, change in (("an increment", "di", lambda v: v + 1e-6), ("the scanning mode", "n2s", lambda v: not v)):
        sab = dict(m0["pf"]); sab[field] = change(sab[field])
        d = diff(m0["pf"], sab)          # against itself, so a real difference can't confound the sabotage
        print(f"M4d sabotage ({name} perturbed): {'different: ' + ', '.join(d) if d else 'IDENTICAL'}")
        if d != [field]:
            raise Stop(f"M4d's sabotage ({name}) was not reported as exactly '{field}': M4d is not graded")
    dd = sorted({f for h in hours for a, b in (("hsr", "pf"), ("comp", "pf"), ("hsr", "comp"))
                 for f in diff(h["m"]["meta"][a], h["m"]["meta"][b])})
    same_box = all(h["m"]["box"]["sameBoxBothProducts"] for h in hours)
    print(f"M4d grids: {'identical for all three products in 24/24 hours' if not dd else 'DIFFERENT: ' + ', '.join(dd)}; "
          f"crop box the same for both products: {same_box}")
    if dd or not same_box:
        raise Stop("M4d: the grids differ: v2 defines no resampling")

    # ================================ 3. C-M4c: sabotages, then the control ======================================
    def pooled(variant):
        names = list(used[0]["m"]["iou"][variant]["counts"])
        io = {}
        for n in names:
            inter = sum(h["m"]["iou"][variant]["counts"][n][0] for h in used)
            uni = sum(h["m"]["iou"][variant]["counts"][n][1] for h in used)
            if uni == 0:
                raise InstrumentFailure(f"C-M4c: empty union at {variant} {n}")
            io[n] = Fraction(inter, uni)
        bh = sum(1 for h in used if h["m"]["iou"][variant]["boundary"])
        a = Fraction(sum(h["m"]["iou"][variant]["a00"][0] for h in used), max(1, sum(h["m"]["iou"][variant]["a00"][1] for h in used)))
        return io, bh, a
    real_io, real_bh, real_a = pooled("real")
    for variant, pred_offset in (("sab_a_flip_grid", None), ("sab_b_east10", "0,-10"), ("sab_c_east1", "0,-1")):
        io, bh, a = pooled(variant)
        oc, det = classify_iou(io, bh, a)
        if variant == "sab_a_flip_grid":
            ok = oc != "i" and io["0,0"] <= real_io["0,0"] / 2
            what = f"IoU(0,0) {float(io['0,0']):.3f} vs real {float(real_io['0,0']):.3f}; predicted not (i) and <= half"
        else:
            ok = oc == "ii" and det == f"strict excess at ['{pred_offset}']"
            what = f"predicted (ii) with a strict maximum at {pred_offset} alone"
        print(f"C-M4c sabotage {variant}: outcome ({oc}), {det}; {what} -> {'as predicted' if ok else 'DID NOT BITE'}")
        if not ok:
            raise Stop(f"C-M4c's sabotage {variant} missed its prediction: the control is not believed")
    oc, det = classify_iou(real_io, real_bh, real_a)
    peak = sorted(((float(x), n) for n, x in real_io.items()), reverse=True)[:3]
    print(f"C-M4c: {real_bh} boundary hours (K = {K_BOUNDARY}); IoU(0,0) {float(real_io['0,0']):.4f}; top three "
          f"{[(n, round(x, 4)) for x, n in peak]}; A(0,0) {pct(real_a)} -> outcome ({oc}): {det}")
    if oc == "ii":
        raise Stop("C-M4c (ii): misaligned. The instrument")
    if oc == "iii":
        raise Stop(f"C-M4c (iii): aligned, but A(0,0) = {pct(real_a)} < 95 %. Stop and decide; Q2's ruling gets the number")
    if oc == "iv":
        raise Stop("C-M4c (iv): no signal; the instrument is unreliable at this scale")

    # ================================ 4. M4e-M4h (outcome (i) only) ==============================================
    S = {p: {k: sum(h["m"]["products"][p][k] for h in used) for k in
             ("echo", "echoSnow", "echoNoType", "snow", "snowEcho")} for p in ("hsr", "comp")}
    e = {p: Fraction(S[p]["echoSnow"], S[p]["echo"]) for p in S}
    f = {p: Fraction(S[p]["echoNoType"], S[p]["echo"]) for p in S}
    g = {p: Fraction(S[p]["snowEcho"], S[p]["snow"]) for p in S}
    print(f"M4e: HSR {pct(e['hsr'])} (predicted 80-100 %); composite {pct(e['comp'])} (predicted 60-100 % and <= HSR)")
    if e["comp"] > e["hsr"]:
        raise Stop("M4e: direction reversed (composite > HSR): the model of the two products is wrong")
    for p, lo in (("hsr", 80), ("comp", 60)):
        if e[p] < Fraction(lo, 100):
            print(f"  M4e({p}) OFF-PREDICTION: reported with M4f; Daniel decides (not a stop)")
    print(f"M4f: HSR {pct(f['hsr'])} (predicted <= 5 %); composite {pct(f['comp'])} (predicted <= 25 % and >= HSR)")
    if f["comp"] < f["hsr"]:
        raise Stop("M4f: direction reversed (composite < HSR): the model of the two products is wrong")
    if f["comp"] > Fraction(25, 100):
        print("  M4f(comp) outside its band: reported beside Q2 as the Blipscope layer's virga share (not a stop)")
    print(f"M4g: HSR {pct(g['hsr'])}, composite {pct(g['comp'])} (predicted 5-40 % each, composite >= HSR)")
    if g["comp"] < g["hsr"]:
        raise Stop("M4g: direction reversed (composite < HSR)")
    for p in g:
        if not Fraction(5, 100) <= g[p] <= Fraction(40, 100):
            print(f"  M4g({p}) outside 5-40 %: for the record; changes nothing ruled (not a stop)")
    for p in ("hsr", "comp"):
        rows = [h["m"]["products"][p] for h in used]
        ok = sum(1 for r in rows if (r["typeBytes"] <= 4 if r["allOneType"] else 4 * r["typeBytes"] <= r["codeBytes"]))
        rungs = Counter(r["v1Rung"] for r in rows)
        worst = max(Fraction(r["typeBytes"], max(1, r["codeBytes"])) for r in rows)
        print(f"M4h {p}: {ok}/{len(rows)} hours within the prediction (<= 4 B when one type, else <= 25 % of the code "
              f"layer); worst type/code {pct(worst)}; v1 rungs {dict(rungs)}")
    print("M4: every row read.")
    return 0


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    if argv[2] == "--selftest":
        return selftest()
    if argv[2] != "run":
        print(__doc__)
        return 2
    try:
        return run(argv[1])
    except Stop as e:
        print(f"STOP: {e}\n(every later row stays unread)")
        return 1
    except InstrumentFailure as e:
        print(f"INSTRUMENT FAILURE: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
