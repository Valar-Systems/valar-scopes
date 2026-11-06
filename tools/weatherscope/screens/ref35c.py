"""Weatherscope 3.5c's host renders (docs/weatherscope-stage3.md A12.130, A12.133, A12.138): the forecast, conditions and
alerts screens drawn from the trimmed NWS records alone, for Daniel's look -- no device code exists yet, so nothing here
is read from one. The font is ref35.py's pinned glcdfont (FT1). Every text row is checked against the disc's chord,
written here: a row (top y, height h) fits when its visible width <= int(2 * sqrt(120^2 - dy^2)) - 8, dy the larger of
|y - 120| and |y + h - 120|. A row that does not fit is REFUSED: no PNG is written and the row is named. So is a screen
with two text items within 2 px of each other (A12.138): each text() call is one item, its box the pixels it lit.

    python -I tools/weatherscope/screens/ref35c.py <out dir outside the repo> [--night <night record>]
    python -I tools/weatherscope/screens/ref35c.py --selftest     # S-L: the forecast before A12.138 must read 0 px
    python -I tools/weatherscope/screens/ref35c.py --k --write    # test/fixtures/wx/nws/k35c.ref.txt, the K rows
    python -I tools/weatherscope/screens/ref35c.py --k --check    # that file still matches what this draws

Writes each screen at 1x and 3x as PNG (zlib, no Pillow) and prints its RGB-pixel sha256 (12 hex) -- the identity of
a render, which survives any PNG encoder -- and its smallest text gap. Prints no coordinate.
Exit 0 every render written; 1 a refusal (nothing written for that render) or a failed selftest; 2 the rig.
"""
import datetime, hashlib, importlib.util, json, math, os, struct, sys, tempfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DAY = os.path.join(ROOT, "test", "fixtures", "wx", "nws", "dallas-day.json")
K_REF = os.path.join(ROOT, "test", "fixtures", "wx", "nws", "k35c.ref.txt")
OFFSET_MIN = -360   # CST, read from the capture's first hourly entry; fixed per stored case (A12.133)
SIZE, C = 240, 120
_spec = importlib.util.spec_from_file_location("ref35", os.path.join(os.path.dirname(os.path.abspath(__file__)), "ref35.py"))
_ref35 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ref35)
FONT = _ref35.FONT
DEG = chr(0xF8)
THEMES = {   # the roles A12.133 names, as WxTheme.h's tables hold them
    "dark": {"bg": (0, 0, 0), "uiFg": (230, 237, 234), "uiDim": (130, 150, 160), "uiAccent": (90, 200, 255),
             "uiWarn": (255, 176, 40), "orangeText": (255, 140, 0)},
    "light": {"bg": (234, 231, 223), "uiFg": (38, 38, 40), "uiDim": (96, 92, 86), "uiAccent": (0, 112, 151),
              "uiWarn": (143, 94, 0), "orangeText": (159, 85, 0)},
}
WARNING_SCREEN = {"Tornado Warning", "Snow Squall Warning", "Severe Thunderstorm Warning", "Flash Flood Warning"}
WEEKDAYS = ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"]
DIRS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
BAND_TOP, BAND_ROWS = 16, 206   # rows 16-221: under the title (last row 15), above the current dot (top row 222)
MIN_GAP = 3                     # two text items 2 px apart or closer are refused
# The forecast strip (A12.138): the hour labels sit under the axis and the week stays where it was, so the strip is shorter.
FORECAST = {"axis": 132, "plot_bottom": 127, "plot_scale": 47, "low_label": 123, "hours": 135}
FORECAST_BEFORE = {"axis": 137, "plot_bottom": 132, "plot_scale": 52, "low_label": 128, "hours": 141}   # S-L only


def chord(y, h):
    dy = max(abs(y - C), abs(y + h - C))
    if dy >= C:
        return 0
    return max(0, int(2 * math.sqrt(C * C - dy * dy)) - 8)


class Screen:
    def __init__(self, theme):
        self.t = THEMES[theme]
        self.px = [[self.t["bg"]] * SIZE for _ in range(SIZE)]
        self.refusals = []
        self.items = []   # one (text, x0, y0, x1, y1) per text() call: the box of the pixels it lit

    def put(self, x, y, col):
        if 0 <= x < SIZE and 0 <= y < SIZE:
            self.px[y][x] = col

    def rect(self, x0, y0, x1, y1, col):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, col)

    def line(self, x0, y0, x1, y1, col):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.put(x0, y0, col)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy; x0 += sx
            if e2 <= dx:
                err += dx; y0 += sy

    def text(self, x, y, s, size, role):
        col = self.t[role]
        w = len(s) * 6 * size - size
        if w > chord(y, 8 * size):
            self.refusals.append(f"'{s}' at y={y} size {size}: {w} px > chord {chord(y, 8 * size)}")
        lit = []
        for i, ch in enumerate(s):
            g = FONT[ord(ch) & 0xFF]
            for cx in range(5):
                for cy in range(7):
                    if g[cx] & (1 << cy):
                        px, py = x + (i * 6 + cx) * size, y + cy * size
                        self.rect(px, py, px + size - 1, py + size - 1, col)
                        lit += [(px, py), (px + size - 1, py + size - 1)]
        if lit:
            self.items.append((s, min(p[0] for p in lit), min(p[1] for p in lit), max(p[0] for p in lit), max(p[1] for p in lit)))

    def centred(self, s, y, size, role):
        self.text(C - (len(s) * 6 * size) // 2, y, s, size, role)

    def min_gap(self):
        """(gap, a, b): the closest two text items. A gap is the larger of the horizontal and vertical blank pixels
        between two boxes, negative when they overlap, and deliberately not clamped: an overlap reads as one."""
        best = None
        for i, a in enumerate(self.items):
            for b in self.items[i + 1:]:
                dx = max(a[1], b[1]) - min(a[3], b[3]) - 1
                dy = max(a[2], b[2]) - min(a[4], b[4]) - 1
                if best is None or max(dx, dy) < best[0]:
                    best = (max(dx, dy), a[0], b[0])
        return best

    def rgb(self):
        return b"".join(bytes(c) for row in self.px for c in row)

    def rgb565(self):
        """The frame the K rows hash: 57,600 big-endian RGB565 pixels, truncated as color565 does."""
        out = bytearray()
        for row in self.px:
            for r, g, b in row:
                v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
                out += bytes((v >> 8, v & 0xFF))
        return bytes(out)


def local(utc):
    return datetime.datetime.fromtimestamp(utc + OFFSET_MIN * 60, tz=datetime.timezone.utc)


def hour_text(utc, spaced=True):
    h = local(utc).hour
    return f"{12 if h % 12 == 0 else h % 12}{' ' if spaced else ''}{'AM' if h < 12 else 'PM'}"


def clock(utc):
    t = local(utc)
    return f"{12 if t.hour % 12 == 0 else t.hour % 12}:{t.minute:02d} {'AM' if t.hour < 12 else 'PM'}"


def days(rec):
    """The periods grouped into days by position: a daytime period opens a day; a night joins the current one; a
    night-only first period is day 0 alone. Each: {k, day: period|None, night: period|None}."""
    out = []
    for p in rec["periods"]:
        if p["isDaytime"] or not out:
            out.append({"k": len(out), "day": p if p["isDaytime"] else None, "night": None if p["isDaytime"] else p})
        else:
            out[-1]["night"] = p
    return out


def weekday(rec, k):
    return WEEKDAYS[(local(rec["capturedUtc"]).weekday() + k) % 7]


def next_word(period):
    """Q95: SNOW when the period's short forecast names snow and not rain; otherwise RAIN."""
    t = period["short"].lower() if period else ""
    return "NEXT SNOW" if "snow" in t and "rain" not in t else "NEXT RAIN"


def period_at(rec, utc):
    """Q105: the period holding an hour, the last one starting at or before it. An hour before them all has none."""
    hit = None
    for p in rec["periods"]:
        if p["startUtc"] <= utc:
            hit = p
    return hit


def headline(rec):
    for h in rec["hourly"][:24]:
        if h["hasPop"] and h["pop"] >= 30:
            return next_word(period_at(rec, h["startUtc"])), hour_text(h["startUtc"])
    for d in days(rec):
        for p, night in ((d["day"], False), (d["night"], True)):
            if p and p["hasPop"] and p["pop"] >= 30:
                if d["k"] == 0:
                    return next_word(p), "TONIGHT" if night else "TODAY"
                return next_word(p), weekday(rec, d["k"]) + (" NIGHT" if night else "")
    return "NO RAIN", "IN 7 DAYS"


def frame(s, title, page):
    s.centred(title, 9, 1, "uiDim")
    for i, cx in enumerate((100, 110, 120, 130, 140)):
        if i == page:
            s.rect(cx - 2, 222, cx + 2, 226, s.t["uiFg"])   # 5 px as well as brighter: a shade alone is faint in light
        else:
            s.rect(cx - 1, 223, cx + 1, 225, s.t["uiDim"])


def block_top(h):
    """The row that centres an h-row block between the title and the dots; an odd remainder goes below."""
    return BAND_TOP + (BAND_ROWS - h) // 2


def forecast(rec, theme, f=FORECAST):
    s = Screen(theme)
    frame(s, "FORECAST", 1)
    a, b = headline(rec)
    whole = f"{a} {b}"
    if len(whole) * 12 - 2 <= chord(34, 16):
        s.centred(whole, 34, 2, "uiFg")
    else:
        s.centred(a, 34, 2, "uiFg")
        s.centred(b, 52, 2, "uiFg")
    hrs = rec["hourly"][:24]
    temps = [h["tempF"] for h in hrs if h["hasTemp"]]
    tmin, tmax = min(temps), max(temps)
    s.rect(36, f["axis"], 203, f["axis"], s.t["uiDim"])
    pts = []
    for i, h in enumerate(hrs):
        x0 = 36 + 7 * i
        if h["hasPop"] and h["pop"] > 0:
            ht = int(math.floor(h["pop"] * 40 / 100 + 0.5))
            if ht > 0:
                s.rect(x0 + 1, f["axis"] - ht, x0 + 5, f["axis"] - 1, s.t["uiAccent"])
        if h["hasTemp"]:
            pts.append((x0 + 3, f["plot_bottom"] - int(math.floor((h["tempF"] - tmin) * f["plot_scale"] / max(1, tmax - tmin) + 0.5))))
        if local(h["startUtc"]).hour % 6 == 0:
            lab = hour_text(h["startUtc"], spaced=False).replace("AM", "A").replace("PM", "P")
            s.text(x0 + 3 - (len(lab) * 6) // 2, f["hours"], lab, 1, "uiDim")
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        s.line(x0, y0, x1, y1, s.t["orangeText"])
    for v, y in ((tmax, 76), (tmin, f["low_label"])):
        lab = f"{v}{DEG}"
        s.text(32 - (len(lab) * 6 - 1), y, lab, 1, "uiFg")
    for j, d in enumerate(days(rec)[:7]):
        name = weekday(rec, d["k"]) if d["day"] else "TON"
        hi = f"{d['day']['tempF']}" if d["day"] else "--"
        lo = f"{d['night']['tempF']}" if d["night"] else "--"
        pop = max([p["pop"] for p in (d["day"], d["night"]) if p and p["hasPop"]] or [0])
        s.centred(f"{name}  {hi}/{lo}  {pop:>2}%", 148 + 10 * j, 1, "uiFg")
    return s


def conditions(rec, theme):
    s = Screen(theme)
    frame(s, "NOW", 2)
    o = rec["observation"]
    top = block_top(127)   # 28 temperature, a 20 gap, then three 14-row lines and a 7-row one, 10 apart
    if o["hasTemp"]:
        # The degree sign is size 2 beside size-4 digits, at the natural advance. The glyph's first lit row and the
        # digits' are both row 0, so one y levels their tops. The two are centred as one.
        digits = f"{o['tempF']}"
        x = C - (len(digits) * 24 + 10) // 2
        s.text(x, top, digits, 4, "uiFg")
        s.text(x + len(digits) * 24, top, DEG, 2, "uiFg")
    else:
        s.centred("--", top, 4, "uiFg")
    if not o["hasWind"]:
        wind = "WIND --"
    elif o["windMph"] == 0:
        wind = "WIND CALM"
    else:
        wind = f"WIND {DIRS[int(math.floor((o['windDirDeg'] + 22.5) / 45)) % 8]} {o['windMph']}" if o["hasWindDir"] else f"WIND {o['windMph']}"
        if o["hasGust"] and o["gustMph"] > o["windMph"]:
            wind += f" G{o['gustMph']}"
    s.centred(wind, top + 48, 2, "uiFg")
    s.centred(f"HUMIDITY {o['humidityPct']}%" if o["hasHumidity"] else "HUMIDITY --", top + 72, 2, "uiFg")
    hi = next((p["tempF"] for p in rec["periods"] if p["isDaytime"]), None)
    lo = next((p["tempF"] for p in rec["periods"] if not p["isDaytime"]), None)
    s.centred(f"HI {hi if hi is not None else '--'}  LO {lo if lo is not None else '--'}", top + 96, 2, "uiFg")
    stale = rec["capturedUtc"] - o["timeUtc"] > 90 * 60
    s.centred(f"OBSERVED {clock(o['timeUtc'])}", top + 120, 1, "orangeText" if stale else "uiDim")
    return s, hi, lo, stale


def alerts(items, theme, now):
    s = Screen(theme)
    frame(s, "ALERTS", 3)
    shown = sorted([a for a in items if a["event"] not in WARNING_SCREEN], key=lambda a: a["startUtc"])
    if not shown:
        s.centred("NO WATCHES OR ADVISORIES", 116, 1, "uiDim")
        return s, 0, 0
    n, more = min(4, len(shown)), max(0, len(shown) - 4)
    top = block_top(26 * n + 7 if more else 26 * n - 9)
    for i, a in enumerate(shown[:4]):
        y = top + 26 * i
        s.centred(a["event"].upper(), y, 1, "uiWarn")
        span = (f"{WEEKDAYS[local(a['startUtc']).weekday()]} {hour_text(a['startUtc'])} - "
                f"{WEEKDAYS[local(a['endUtc']).weekday()]} {hour_text(a['endUtc'])}")
        s.centred(span, y + 10, 1, "uiDim")
    if more:
        s.centred(f"+{more} MORE", top + 26 * n, 1, "uiDim")
    return s, n, more


def synthetic_rain(rec):
    """A LABELLED copy (A12.133): the next 24 hours' chances set to a ramp under 30 %, and Tuesday Night to 60 %."""
    r = json.loads(json.dumps(rec))
    ramp = [0] * 12 + [5, 10, 15, 20, 25, 20, 15, 10, 5, 0, 0, 0]
    for h, v in zip(r["hourly"], ramp):
        h["pop"], h["hasPop"] = v, True
    for p in r["periods"]:
        if p["name"] == "Tuesday Night":
            p["pop"], p["hasPop"] = 60, True
    return r


def write_png(px_rgb, path, scale):
    w = SIZE * scale
    rows = []
    for y in range(SIZE):
        line = px_rgb[y * SIZE * 3:(y + 1) * SIZE * 3]
        if scale > 1:
            line = b"".join(line[x * 3:x * 3 + 3] * scale for x in range(SIZE))
        rows.extend([b"\x00" + line] * scale)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, w, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def job_list(rec, night):
    jobs = []
    for t in ("dark", "light"):
        if night is None:
            syn = synthetic_rain(rec)
            jobs.append((f"forecast-capture-{t}", lambda t=t: (forecast(rec, t), headline(rec))))
            jobs.append((f"forecast-SYNTHETIC-RAIN-{t}", lambda t=t, r=syn: (forecast(r, t), headline(r))))
            jobs.append((f"conditions-day-{t}", lambda t=t: conditions(rec, t)))
            jobs.append((f"alerts-SYNTHETIC-{t}", lambda t=t: alerts(rec["syntheticAlerts"]["items"], t, rec["capturedUtc"])))
            jobs.append((f"alerts-capture-{t}", lambda t=t: alerts(rec["alerts"], t, rec["capturedUtc"])))
        else:
            jobs.append((f"conditions-night-{t}", lambda t=t: conditions(night, t)))
    return jobs


def k_text():
    """The K reference: one ROW per screen, named as the PNGs are, plus the record's own hash (LF-normalised)."""
    rec = json.load(open(DAY, encoding="utf-8"))
    day = hashlib.sha256(open(DAY, "rb").read().replace(b"\r\n", b"\n")).hexdigest()
    lines = ["# Weatherscope 3.5c K rows (docs/weatherscope-stage3.md A12.144): written by tools/weatherscope/screens/ref35c.py",
             "# --k --write, checked by its --k --check; read by test/host/test_wx_swipe.cpp.",
             f"# dallas-day.json {day} (LF-normalised)"]
    for name, job in job_list(rec, None):
        scr = job()[0]
        lines.append(f"ROW {name} {hashlib.sha256(scr.rgb565()).hexdigest()}")
    return "\n".join(lines) + "\n"


def k_main(argv):
    want = k_text()
    if "--write" in argv:
        open(K_REF, "w", encoding="ascii", newline="\n").write(want)
        print(f"wrote {want.count('ROW ')} rows")
        return 0
    if "--check" in argv:
        try:
            have = open(K_REF, encoding="ascii").read().replace("\r\n", "\n")
        except OSError:
            print("k35c.ref.txt cannot be read")
            return 2
        ok = have == want
        print(f"k35c.ref.txt: {want.count('ROW ')} rows, {'as this renderer draws them' if ok else 'STALE: it differs from what this renderer draws'}")
        return 0 if ok else 1
    print(__doc__)
    return 2


def emit(name, scr, extra, out):
    """Writes one render, or refuses it and writes nothing. True when written."""
    gap = scr.min_gap()
    why = list(scr.refusals)
    if gap is not None and gap[0] < MIN_GAP:
        why.append(f"text gap {gap[0]} px: '{gap[1]}' and '{gap[2]}'")
    if why:
        print(f"REFUSED {name}: " + "; ".join(why))
        return False
    rgb = scr.rgb()
    write_png(rgb, os.path.join(out, f"{name}.png"), 1)
    write_png(rgb, os.path.join(out, f"{name}@3x.png"), 3)
    print(f"{name}: rgb {hashlib.sha256(rgb).hexdigest()[:12]}  gap {gap[0] if gap else '-'} px  {extra if extra else ''}")
    return True


def selftest():
    """S-L (A12.138): the forecast as it stood before the ruling must read 0 px and be refused, with no PNG written, in
    both themes. CONTROL: the frozen layout reads at least MIN_GAP and writes one, so the check is not refusing everything."""
    rec = json.load(open(DAY, encoding="utf-8"))
    bad = 0
    with tempfile.TemporaryDirectory() as out:
        for t in THEMES:
            old, new = forecast(rec, t, FORECAST_BEFORE), forecast(rec, t)
            old_written = emit(f"before-{t}", old, (), out)
            new_written = emit(f"control-{t}", new, (), out)
            ok = (old.min_gap()[0] == 0 and not old_written and not os.path.exists(os.path.join(out, f"before-{t}.png"))
                  and new.min_gap()[0] >= MIN_GAP and new_written and os.path.exists(os.path.join(out, f"control-{t}.png")))
            print(f"S-L {t}: before reads {old.min_gap()[0]} px, control reads {new.min_gap()[0]} px: {'ok' if ok else 'FAILED'}")
            bad += 0 if ok else 1
    return 1 if bad else 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[1] == "--selftest":
        return selftest()
    if argv[1] == "--k":
        return k_main(argv)
    out = os.path.abspath(argv[1])
    os.makedirs(out, exist_ok=True)
    rec = json.load(open(DAY, encoding="utf-8"))
    night = None
    if "--night" in argv:
        night = json.load(open(argv[argv.index("--night") + 1], encoding="utf-8"))
    jobs = job_list(rec, night)
    refused = 0
    for name, job in jobs:
        res = job()
        refused += 0 if emit(name, res[0], res[1:], out) else 1
    print(f"{len(jobs) - refused} render(s) written ({2 * (len(jobs) - refused)} files), {refused} refused")
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
