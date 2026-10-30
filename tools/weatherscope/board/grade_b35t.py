"""Grades 3.5t step t5's board run (docs/weatherscope-stage3.md A12.66 section 4, A12.67) from run_b35t.py's output.

    <penv python> -I tools/weatherscope/board/grade_b35t.py <run dir> [<boot.txt>] [--swb]

Every expected value comes from the OTHER side of the contract:
  - B1's frames and lines from the four committed references (k35.ref.txt, k35l.ref.txt, k35b.ref.txt, k35bl.ref.txt),
    each first checked by its own generator's --check: the frame's sha256 and every ROW, TIME, BAND and LABEL line;
  - B3 by legend_check.py --fb --theme <the capture's theme>;
  - BSW and BSW-L by A12.66's counts.
THE BELIEF RULE (grade_b35b's, with a theme clause): the last "[wx] theme=" line before a capture's first fetch gives its
theme, the last "[wx] terrain on|off" its terrain (none = off), and neither line arrives during its fetches.
--swb grades S-SWB's pass (A12.66 section 5): BSW with the base rebuilt ("package") at the two switches.
Q62 (A12.67): prints verdicts and counts only -- never a line's time, never which press switched. Prints no coordinate.
"""
import hashlib, json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
WX = os.path.join(ROOT, "test", "fixtures", "wx")
SCREENS = os.path.join(ROOT, "tools", "weatherscope", "screens")
LEGEND = os.path.join(WX, "legend_check.py")
DALLAS = ["dry", "approaching", "herenow", "warning", "stale", "clear"]
OFFSET = re.compile(r"^\[wx\] auto offset=\+(\d+) min phase=(\w+)$")
THEME = re.compile(r"^\[wx\] theme=(dark|light) was=(dark|light) ")
PSRAM_USED, PSRAM_BAND = 227144, 2000                       # A12.57's baseline, Q55
PSRAM_BLOCKS = dict(image="58356", state="9972", scratch="4596", ok="1")   # Q56


def refs():
    """{(theme, row): [lines]} from the four references, after each generator's --check."""
    checks = [(["ref35.py", "--k", "--check"], "k35.ref.txt", "dark"),
              (["ref35.py", "--k", "--theme", "light", "--check"], "k35l.ref.txt", "light"),
              (["k35b.py", "--check"], "k35b.ref.txt", "dark"),
              (["k35b.py", "--theme", "light", "--check"], "k35bl.ref.txt", "light")]
    out = {}
    for args, name, theme in checks:
        r = subprocess.run([sys.executable, "-I", "-B", os.path.join(SCREENS, args[0]), *args[1:]], capture_output=True,
                           text=True)
        if r.returncode != 0:
            print(f"  FAIL {name}: its generator's --check exits {r.returncode}: nothing is graded")
            sys.exit(2)
        for line in open(os.path.join(WX, name), encoding="utf-8"):
            line = line.rstrip("\r\n")
            if line and not line.startswith("#") and not line.startswith("AR1 "):
                out.setdefault((theme, line.split()[1]), []).append(line)
    print(f"  ok   the four references regenerate identically ({len(out)} rows)")
    return out


def last_before(lines, rx, t):
    hit = None
    for tt, l in lines:
        if tt >= t:
            break
        m = rx.match(l)
        if m:
            hit = m
    return hit


def between(lines, rx, t0, t1):
    return [(tt, l) for tt, l in lines if t0 <= tt <= t1 and rx.match(l)]


def board_k_lines(lines, row, t):
    """The last block of '[wx] k ... <row> ...' lines printed before t, without the prefix."""
    blocks, cur = [], None
    for tt, l in lines:
        if tt >= t:
            break
        if l.startswith(f"[wx] k ROW {row} "):
            cur = [l[len("[wx] k "):]]
            blocks.append(cur)
        elif cur is not None and l.startswith("[wx] k ") and l.split()[3] == row:
            cur.append(l[len("[wx] k "):])
        elif cur is not None and not l.startswith("[wx] k "):
            cur = None
    return blocks[-1] if blocks else None


def grade_b1(run, lines, caps, REF):
    ok_all, n = True, 0
    for c in caps:
        if c.get("step") != "B1":
            continue
        n += 1
        label = c["label"]
        if c["verdict"] != "graded":
            print(f"  FAIL B1 {label}: capture {c['verdict']}")
            ok_all = False
            continue
        f = c["fetches"]
        t0, t1 = f[0]["start"], f[-1]["end"]
        th = last_before(lines, re.compile(r"^\[wx\] theme=(dark|light) "), t0)
        te = last_before(lines, re.compile(r"^\[wx\] terrain (on|off)$"), t0)
        moved = between(lines, re.compile(r"^\[wx\] (theme=|terrain (on|off)$)"), t0, t1)
        theme = th.group(1) if th else None
        terrain = bool(te and te.group(1) == "on")
        row = f"{c['scenario']}-60" if c["scenario"] in DALLAS else f"{c['scenario']}-60-{'C' if terrain else 'B'}"
        want = REF.get((c["theme"], row))
        snap = open(os.path.join(run, c["graded"]), "rb").read()
        sha = hashlib.sha256(snap).hexdigest()
        k = board_k_lines(lines, row, t0)
        why = []
        if theme != c["theme"]:
            why.append(f"theme believed {theme}, the run's {c['theme']}")
        if moved:
            why.append("a theme or terrain line during its fetches")
        if terrain != c["terrain"]:
            why.append(f"terrain believed {terrain}, the run's {c['terrain']}")
        if not want:
            why.append(f"no reference row {row}")
        else:
            if sha != want[0].split()[2]:
                why.append("the frame's sha256 differs from the reference row")
            if k != want:
                why.append("the board's k lines differ from the reference's" if k else "no k lines for the row")
        ok = not why
        ok_all &= ok
        print(f"  {'ok  ' if ok else 'FAIL'} B1-{c['theme']} {row}: " + ("frame and lines exact" if ok else "; ".join(why)))
    print(f"  B1: {n} captures graded")
    return ok_all


def grade_b3(run, lines, caps):
    ok_all = True
    for c in caps:
        if c.get("step") != "B3":
            continue
        if c["verdict"] != "graded":
            print(f"  FAIL B3 {c['label']}: capture {c['verdict']}")
            ok_all = False
            continue
        t0 = c["fetches"][0]["start"]
        opened = [tt for tt, l in lines if tt < t0 and l == "[wx] screen=legend"]
        wx = [l for tt, l in lines if opened and opened[-1] <= tt < t0 and re.match(r"^WX[LT],", l)]
        with tempfile.TemporaryDirectory() as td:
            p = os.path.join(td, "lines.txt")
            open(p, "w", encoding="ascii", newline="\n").write("\n".join(wx) + "\n")
            r = subprocess.run([sys.executable, "-I", "-B", LEGEND, "--fb", os.path.join(run, c["graded"]), p, "--theme",
                                c["theme"]], capture_output=True, text=True)
        ok = r.returncode == 0
        ok_all &= ok
        print(f"  {'ok  ' if ok else 'FAIL'} B3-{c['theme']}: legend_check --theme {c['theme']} exit {r.returncode}")
        if not ok:
            print("      " + r.stdout.strip().replace("\n", "\n      "))
    return ok_all


def grade_bsw(lines, swb):
    t_s = next((t for t, l in lines if l == "#HOST BSW starts"), None)
    t_e = next((t for t, l in lines if l == "#HOST BSW ends"), None)
    if t_s is None or t_e is None:
        print("  FAIL BSW: did not run")
        return False
    win = [(t, l) for t, l in lines if t_s <= t <= t_e]
    offs = [OFFSET.match(l) for _, l in win]
    offs = [m for m in offs if m]
    got = [int(m.group(1)) for m in offs]
    want = [15 * i for i in range(1, 96)] + [0]
    phases = [m.group(2) for m in offs]
    # cyclic: the walk starts and ends at offset 0, so the phase before press 1 is press 96's
    changes = sum(1 for a, b in zip(phases[-1:] + phases, phases) if a != b)
    themes = [THEME.match(l) for _, l in win]
    themes = [m for m in themes if m]
    dims = [l for _, l in win if l.startswith("[wx] dim=")]
    init = [l for t, l in lines if t < t_s and l.startswith("[wx] init;")]
    autodim = bool(init) and " autodim=1 " in init[-1] + " "
    located = bool(init) and " location=set " in init[-1] + " "
    rows = []
    rows.append(("BSW offsets", got == want, f"{len(got)} offset lines, {'+15 ... +1425 then 0' if got == want else 'NOT +15 ... +1425, 0'}"))
    rows.append(("BSW location", located, "the last init line before BSW: location=" + ("set" if located else "UNSET")))
    rows.append(("BSW phase changes", changes == 2, f"{changes} phase changes across the 96 presses"))
    dirs = sorted(f"{m.group(2)}->{m.group(1)}" for m in themes)
    rows.append(("BSW switches", dirs == ["dark->light", "light->dark"], f"{len(themes)} [wx] theme= lines: {dirs}"))
    want_dims = 2 if autodim else 0
    rows.append(("BSW dims", len(dims) == want_dims, f"{len(dims)} [wx] dim= lines (autodim={int(autodim)}: want {want_dims})"))
    reasons, recomposed = [], 0
    for t, l in win:
        if not THEME.match(l):
            continue
        after = [(tt, ll) for tt, ll in lines if t < tt <= t + 3.0]
        scr = next((ll for _, ll in after if re.match(r"^\[wx\] screen=scenario \w+ R=60$", ll)), None)
        recomposed += scr is not None
        if scr and scr.split()[2] in ("bdry", "brain"):   # "[wx] screen=scenario <scene> R=60"
            base = next((re.search(r" rebuilt=(\w+) at=scenario ", ll) for _, ll in after if ll.startswith("[wx] base ")), None)
            reasons.append(base.group(1) if base else "none")
    rows.append(("BSW recompose", recomposed == len(themes) == 2, f"{recomposed} of {len(themes)} switches followed by their recompose"))
    want_reason = "package" if swb else "no"
    rows.append(("SW base row" + (" (S-SWB)" if swb else ""), reasons == [want_reason] * 2,
                 f"the base line at each switch on Bend: {reasons} (want {[want_reason] * 2})"))
    t_r = next((t for t, l in lines if t > t_e and l.startswith("[wx] builds n=")), None)
    whys = [l for t, l in lines if t_r and t >= t_r and re.match(r"^\[wx\] build \d+ why=theme ", l)]
    rows.append(("BSW build reasons", len(whys) == 2, f"{len(whys)} builds with why=theme in the R dump"))
    for name, ok, what in rows:
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {what}")
    return all(ok for _, ok, _ in rows)


def grade_bswl(lines):
    t_s = next((t for t, l in lines if l == "#HOST BSW-L starts"), None)
    t_e = next((t for t, l in lines if l == "#HOST BSW-L ends"), None)
    if t_s is None or t_e is None:
        print("  FAIL BSW-L: did not run")
        return False
    win = [(t, l) for t, l in lines if t_s <= t <= t_e]
    t_loop = next((t for t, l in win if l.startswith("[wx] screen=loop ")), None)
    rec = next(((t, l) for t, l in win if l.startswith("[wx] loop records=")), (None, None))
    t_theme = next((t for t, l in win if THEME.match(l)), None)
    off_in = [l for t, l in win if t_loop and rec[0] and t_loop < t < rec[0] and OFFSET.match(l)]
    rebuilds = re.search(r" layer_rebuilds=(\d+)", rec[1] or "")
    rows = [("BSW-L the press inside the loop", len(off_in) == 1, f"{len(off_in)} offset line(s) between the loop's start and its records"),
            ("BSW-L layer", bool(rebuilds) and rebuilds.group(1) == "0", f"layer_rebuilds={rebuilds.group(1) if rebuilds else '?'}"),
            ("BSW-L deferred", bool(t_theme and rec[0] and t_theme > rec[0]),
             "the switch printed " + ("after the loop's records" if t_theme and rec[0] and t_theme > rec[0] else "NOT after the loop's records"))]
    for name, ok, what in rows:
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {what}")
    return all(ok for _, ok, _ in rows)


def grade_boot(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    used = [int(m.group(1)) for m in re.finditer(r"\[wx\] psram_used=(\d+) ", text)]
    base = [dict(re.findall(r"(\w+)=(\d+)", m.group(0))) for m in re.finditer(r"\[wx\] psram_base=.*", text)]
    init = re.search(r"\[wx\] init; .*", text)
    rows = [("boot psram_used", bool(used) and abs(used[0] - PSRAM_USED) <= PSRAM_BAND,
             f"{used[0] if used else '?'} (want {PSRAM_USED} +/- {PSRAM_BAND})"),
            ("boot psram_base", bool(base) and all(base[0].get(k) == v for k, v in PSRAM_BLOCKS.items()),
             f"{ {k: base[0].get(k) for k in PSRAM_BLOCKS} if base else '?'}"),
            ("boot theme", bool(init) and " theme=auto read=unset" in init.group(0),
             "the init line reads " + (re.search(r"theme=\S+ read=\S+", init.group(0)).group(0) if init and "theme=" in init.group(0) else "NO theme"))]
    for name, ok, what in rows:
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {what}")
    return all(ok for _, ok, _ in rows)


def synthetic(switch_at=(40, 88), base="no", drop_theme=False, autodim=True):
    """A BSW and a BSW-L trace as the firmware would print them (A12.66), for --selftest. Synthetic, never a board's."""
    t, out = 0.0, []

    def add(l, dt=0.05):
        nonlocal t
        t += dt
        out.append((t, l))

    add(f"[wx] init; location=set autodim={int(autodim)} brightness=255 stored=1 theme=auto read=known")
    add("[wx] screen=scenario bdry R=60")
    add("#HOST BSW starts")
    phase = "Day"
    for i in range(1, 97):
        add("#HOST send b'O'")
        add("[wx] gesture O")
        flip = i in switch_at
        if flip:
            phase = "Night" if phase == "Day" else "Day"
            if autodim:
                add(f"[wx] dim={int(phase == 'Night')} brightness={51 if phase == 'Night' else 255}")
        add(f"[wx] auto offset=+{15 * i % 1440} min phase={phase}")
        if flip and not (drop_theme and i == switch_at[0]):
            new, old = ("dark", "light") if phase == "Night" else ("light", "dark")
            add(f"[wx] theme={new} was={old} setting=auto phase={phase}")
            add("[wx] screen=scenario bdry R=60")
            add(f"[wx] base build_us=1 rebuilt={base} at=scenario R=60 terrain=0 level=0 rings=30 rivers=135 roads=354 "
                f"places=9 peaks=16 fnv=c9a61df9")
        add("", 0.15)
    add("#HOST BSW ends")
    add("#HOST send b'R'")
    add("[wx] builds n=8 of 8")
    for j in range(8):
        add(f"[wx] build {j} why={'theme' if j in (2, 5) else 'cycle'} scene=bdry mi=60->60 decode=1")
    add("#HOST BSW-L starts")
    add("[wx] screen=loop R=60")
    add("[wx] auto offset=+600 min phase=Night")
    add("[wx] loop records=15 period_us=70000 R=60 layer_rebuilds=0")
    add("[wx] screen=scenario bdry R=60")
    add("[wx] theme=dark was=light setting=auto phase=Night")
    add("[wx] screen=scenario bdry R=60")
    add("#HOST BSW-L ends")
    return out


def selftest():
    """The grader's BSW and BSW-L rows on synthetic traces: the correct walk passes; each control must fail."""
    import contextlib, io
    cases = [("a correct walk", dict(), False, True),
             ("a switch at the first press (the cyclic count)", dict(switch_at=(1, 49)), False, True),
             ("S-SWB's walk graded as S-SWB", dict(base="package"), True, True),
             ("CONTROL: S-SWB's walk graded as t5's", dict(base="package"), False, False),
             ("CONTROL: t5's walk graded as S-SWB", dict(), True, False),
             ("CONTROL: a switch with no theme line", dict(drop_theme=True), False, False),
             ("CONTROL: autodim off but dims printed", dict(), False, False)]
    bad = 0
    for name, kw, swb, want in cases:
        lines = synthetic(**kw)
        if name.startswith("CONTROL: autodim off"):
            lines = [(t, l.replace("autodim=1", "autodim=0")) for t, l in lines]
        with contextlib.redirect_stdout(io.StringIO()):
            got = grade_bsw(lines, swb) and (swb or grade_bswl(lines))
        bad += got != want
        print(f"  {'ok  ' if got == want else 'FAIL'} {name}: {'passes' if got else 'fails'} (want {'pass' if want else 'fail'})")
    print(f"{len(cases)} checks, {bad} failures")
    return 1 if bad else 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        return selftest()
    swb = "--swb" in argv
    argv = [a for a in argv if a != "--swb"]
    if len(argv) not in (2, 3):
        print(__doc__)
        return 2
    run = argv[1]
    lines = [(t, l) for t, l in json.load(open(os.path.join(run, "lines.json")))]
    caps = json.load(open(os.path.join(run, "captures.json")))
    watch = json.load(open(os.path.join(run, "watch.json")))
    ok = True
    if len(argv) == 3:
        ok &= grade_boot(argv[2])
    if not swb:
        REF = refs()
        ok &= grade_b1(run, lines, caps, REF)
        ok &= grade_b3(run, lines, caps)
    ok &= grade_bsw(lines, swb)
    if not swb:
        ok &= grade_bswl(lines)
    stopped = watch.get("stopped")
    print(f"  {'ok  ' if not stopped else 'FAIL'} watcher: " + ("0 unexplained" if not stopped else "STOPPED"))
    ok &= not stopped
    print("ALL AS PREDICTED" if ok else "NOT ALL AS PREDICTED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
