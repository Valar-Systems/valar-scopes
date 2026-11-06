"""Weatherscope 3.5c's host renders (docs/weatherscope-stage3.md A12.130, A12.133): the forecast, conditions and alerts
screens drawn from the trimmed NWS records alone, for Daniel's look -- no device code exists yet, so nothing here is read
from one. The font is ref35.py's pinned glcdfont (FT1). Every text row is checked against the disc's chord, written
here: a row (top y, height h) fits when its visible width <= int(2 * sqrt(120^2 - dy^2)) - 8, dy the larger of
|y - 120| and |y + h - 120|. A row that does not fit is REFUSED: no PNG is written and the row is named.

    python -I tools/weatherscope/screens/ref35c.py <out dir outside the repo> [--night <night record>]

Writes each screen at 1x and 3x as PNG (zlib, no Pillow) and prints its RGB-pixel sha256 (12 hex) -- the identity of
a render, which survives any PNG encoder. Prints no coordinate.
Exit 0 every render written; 1 a refusal (nothing written for that render); 2 the rig.
"""
import datetime, hashlib, importlib.util, json, math, os, struct, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DAY = os.path.join(ROOT, "test", "fixtures", "wx", "nws", "dallas-day.json")
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
        for i, ch in enumerate(s):
            g = FONT[ord(ch) & 0xFF]
            for cx in range(5):
                for cy in range(7):
                    if g[cx] & (1 << cy):
                        self.rect(x + (i * 6 + cx) * size, y + cy * size, x + (i * 6 + cx) * size + size - 1,
                                  y + cy * size + size - 1, col)

    def centred(self, s, y, size, role):
        self.text(C - (len(s) * 6 * size) // 2, y, s, size, role)

    def rgb(self):
        return b"".join(bytes(c) for row in self.px for c in row)


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


def headline(rec):
    for h in rec["hourly"][:24]:
        if h["hasPop"] and h["pop"] >= 30:
            return "NEXT RAIN", hour_text(h["startUtc"])
    for d in days(rec):
        for p, night in ((d["day"], False), (d["night"], True)):
            if p and p["hasPop"] and p["pop"] >= 30:
                if d["k"] == 0:
                    return "NEXT RAIN", "TONIGHT" if night else "TODAY"
                return "NEXT RAIN", weekday(rec, d["k"]) + (" NIGHT" if night else "")
    return "NO RAIN", "IN 7 DAYS"


def frame(s, title, page, dots):
    s.centred(title, 9, 1, "uiDim")
    if dots:
        for i, cx in enumerate((100, 110, 120, 130, 140)):
            s.rect(cx - 1, 223, cx + 1, 225, s.t["uiFg"] if i == page else s.t["uiDim"])


def forecast(rec, theme, dots):
    s = Screen(theme)
    frame(s, "FORECAST", 1, dots)
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
    s.rect(36, 137, 203, 137, s.t["uiDim"])
    pts = []
    for i, h in enumerate(hrs):
        x0 = 36 + 7 * i
        if h["hasPop"] and h["pop"] > 0:
            ht = int(math.floor(h["pop"] * 40 / 100 + 0.5))
            if ht > 0:
                s.rect(x0 + 1, 137 - ht, x0 + 5, 136, s.t["uiAccent"])
        if h["hasTemp"]:
            pts.append((x0 + 3, 132 - int(math.floor((h["tempF"] - tmin) * 52 / max(1, tmax - tmin) + 0.5))))
        if local(h["startUtc"]).hour % 6 == 0:
            lab = hour_text(h["startUtc"], spaced=False).replace("AM", "A").replace("PM", "P")
            s.text(x0 + 3 - (len(lab) * 6) // 2, 141, lab, 1, "uiDim")
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        s.line(x0, y0, x1, y1, s.t["orangeText"])
    for v, y in ((tmax, 76), (tmin, 128)):
        lab = f"{v}{DEG}"
        s.text(32 - (len(lab) * 6 - 1), y, lab, 1, "uiFg")
    for j, d in enumerate(days(rec)[:7]):
        name = weekday(rec, d["k"]) if d["day"] else "TON"
        hi = f"{d['day']['tempF']}" if d["day"] else "--"
        lo = f"{d['night']['tempF']}" if d["night"] else "--"
        pop = max([p["pop"] for p in (d["day"], d["night"]) if p and p["hasPop"]] or [0])
        s.centred(f"{name}  {hi}/{lo}  {pop:>2}%", 148 + 10 * j, 1, "uiFg")
    return s


def conditions(rec, theme, dots):
    s = Screen(theme)
    frame(s, "NOW", 2, dots)
    o = rec["observation"]
    s.centred(f"{o['tempF']}{DEG}" if o["hasTemp"] else "--", 40, 4, "uiFg")
    if not o["hasWind"]:
        wind = "WIND --"
    elif o["windMph"] == 0:
        wind = "WIND CALM"
    else:
        wind = f"WIND {DIRS[int(math.floor((o['windDirDeg'] + 22.5) / 45)) % 8]} {o['windMph']}" if o["hasWindDir"] else f"WIND {o['windMph']}"
        if o["hasGust"] and o["gustMph"] > o["windMph"]:
            wind += f" G{o['gustMph']}"
    s.centred(wind, 88, 2, "uiFg")
    s.centred(f"HUMIDITY {o['humidityPct']}%" if o["hasHumidity"] else "HUMIDITY --", 112, 2, "uiFg")
    hi = next((p["tempF"] for p in rec["periods"] if p["isDaytime"]), None)
    lo = next((p["tempF"] for p in rec["periods"] if not p["isDaytime"]), None)
    s.centred(f"HI {hi if hi is not None else '--'}  LO {lo if lo is not None else '--'}", 136, 2, "uiFg")
    stale = rec["capturedUtc"] - o["timeUtc"] > 90 * 60
    s.centred(f"OBSERVED {clock(o['timeUtc'])}", 198, 1, "orangeText" if stale else "uiDim")
    return s, hi, lo, stale


def alerts(items, theme, dots, now):
    s = Screen(theme)
    frame(s, "ALERTS", 3, dots)
    shown = sorted([a for a in items if a["event"] not in WARNING_SCREEN], key=lambda a: a["startUtc"])
    if not shown:
        s.centred("NO WATCHES OR ADVISORIES", 116, 1, "uiDim")
        return s, 0, 0
    for i, a in enumerate(shown[:4]):
        y = 36 + 26 * i
        s.centred(a["event"].upper(), y, 1, "uiWarn")
        span = (f"{WEEKDAYS[local(a['startUtc']).weekday()]} {hour_text(a['startUtc'])} - "
                f"{WEEKDAYS[local(a['endUtc']).weekday()]} {hour_text(a['endUtc'])}")
        s.centred(span, y + 10, 1, "uiDim")
    if len(shown) > 4:
        s.centred(f"+{len(shown) - 4} MORE", 140, 1, "uiDim")
    return s, min(4, len(shown)), max(0, len(shown) - 4)


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


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out = os.path.abspath(argv[1])
    os.makedirs(out, exist_ok=True)
    rec = json.load(open(DAY, encoding="utf-8"))
    night = None
    if "--night" in argv:
        night = json.load(open(argv[argv.index("--night") + 1], encoding="utf-8"))
    jobs = []
    for theme in ("dark", "light"):
        for dots in (False, True):
            tag = f"{theme}{'-dots' if dots else ''}"
            if night is None:
                jobs.append((f"forecast-capture-{tag}", lambda t=theme, d=dots: (forecast(rec, t, d), headline(rec))))
                syn = synthetic_rain(rec)
                jobs.append((f"forecast-SYNTHETIC-RAIN-{tag}", lambda t=theme, d=dots, r=syn: (forecast(r, t, d), headline(r))))
                jobs.append((f"conditions-day-{tag}", lambda t=theme, d=dots: conditions(rec, t, d)))
                jobs.append((f"alerts-SYNTHETIC-{tag}", lambda t=theme, d=dots: alerts(rec["syntheticAlerts"]["items"], t, d, rec["capturedUtc"])))
                jobs.append((f"alerts-capture-{tag}", lambda t=theme, d=dots: alerts(rec["alerts"], t, d, rec["capturedUtc"])))
            else:
                jobs.append((f"conditions-night-{tag}", lambda t=theme, d=dots: conditions(night, t, d)))
    refused = 0
    for name, job in jobs:
        res = job()
        scr, extra = res[0], res[1:]
        if scr.refusals:
            refused += 1
            print(f"REFUSED {name}: " + "; ".join(scr.refusals))
            continue
        rgb = scr.rgb()
        write_png(rgb, os.path.join(out, f"{name}.png"), 1)
        write_png(rgb, os.path.join(out, f"{name}@3x.png"), 3)
        print(f"{name}: rgb {hashlib.sha256(rgb).hexdigest()[:12]}  {extra if extra else ''}")
    print(f"{len(jobs) - refused} render(s) written ({2 * (len(jobs) - refused)} files), {refused} refused")
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
