"""The watcher on "a screen change no gesture byte caused" (docs/weatherscope-stage3.md A12.29's standing rule, as
A12.55 writes it down). Every screen change in a board trace must be explained by the runner's own bytes or by the
device's own cycle; the first one that is not is reported with its time in Pacific time (PDT, UTC-7).

    python -I tools/weatherscope/board/screen_watch.py <trace.txt> --scenes 6|8
    python -I tools/weatherscope/board/screen_watch.py --selftest

Importable: run_b35b.py feeds a Watcher every line as it arrives and stops at the first unexplained change.

A change is EXPLAINED only by:
  [wx] screen=legend                 a sent S within 3 s
  [wx] screen=loop ...               a sent T within 3 s
  [wx] zoom in|out ...               a sent U or D within 3 s;  [wx] zoom hold ...  a sent H within 3 s
  [wx] screen=scenario <name> ...    the cycle's next scene after the last one shown; or within 3 s of a zoom line, a
                                     [wx] terrain on|off line, a [wx] loop records= line, or a T sent with the legend
                                     open; or the first scene of a trace or of a boot ([wx] init); or, repeating the
                                     scene shown, within 3 s of a [wx] theme= line (the recompose, A12.66)
  [wx] theme=...                     a sent O, or the runner's "#HOST post wx-theme=<v>" line, within 3 s (3.5t t5)
NOT screen changes (recorded by the trace, not judged here): [wx] dim=..., [wx] auto offset=...
UNEXPLAINED, always: [wx] zoom idle; [wx] hold ring on; a [wx] gesture line for a byte the runner did not send
(within 3 s); anything else above that does not match its rule. Prints screen names and times: no coordinate.
"""
import calendar
import re
import sys
import time

DALLAS = ["dry", "approaching", "herenow", "warning", "stale", "clear"]
BEND = ["bdry", "brain"]
WINDOW_S = 3.0


def pacific(t):
    """PDT (UTC-7), the clock A12.29 asks for: 2026's daylight time runs to 2026-11-01."""
    return time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(t - 7 * 3600)) + " PDT"


class Watcher:
    def __init__(self, scenes):
        self.scenes = scenes
        self.sent = []             # (t, byte)
        self.marks = []            # (t, kind): zoom, terrain, loopend
        self.last = None           # the last scene shown
        self.legend = False
        self.changes = []          # (t, line, explanation or None)

    def _sent_within(self, t, chars):
        return any(c in chars and 0 <= t - ts <= WINDOW_S for ts, c in self.sent)

    def _mark_within(self, t, kinds):
        return any(k in kinds and 0 <= t - ts <= WINDOW_S for ts, k in self.marks)

    def _record(self, t, line, why):
        self.changes.append((t, line, why))
        return None if why else f"UNEXPLAINED at {time.strftime('%H:%M:%S', time.gmtime(t))}Z ({pacific(t)}): {line}"

    def feed(self, t, line):
        """One trace line at receive time t. Returns a message for an unexplained change, else None."""
        m = re.match(r"^#HOST send b'(.)'", line)
        if m:
            self.sent.append((t, m.group(1)))
            return None
        if line.startswith("#HOST post wx-theme="):   # the runner's config POST (A12.66)
            self.marks.append((t, "post"))
            return None
        if line.startswith("[wx] theme="):
            self.marks.append((t, "theme"))
            why = "a sent O" if self._sent_within(t, "O") else "the runner's POST" if self._mark_within(t, ("post",)) else None
            return self._record(t, line, why)
        if line.startswith("[wx] init;"):
            self.last = None
            return None
        if line.startswith("[wx] gesture "):
            ch = line.split()[2]
            return self._record(t, line, "the runner's byte" if self._sent_within(t, ch) else None)
        if line.startswith("[wx] hold ring on"):
            return self._record(t, line, None)
        if line.startswith("[wx] zoom "):
            why = line.split()[2]
            self.marks.append((t, "zoom"))
            if why == "idle":
                return self._record(t, line, None)
            need = "H" if why == "hold" else "UD"
            return self._record(t, line, f"a sent {need}" if self._sent_within(t, need) else None)
        if line in ("[wx] terrain on", "[wx] terrain off"):
            self.marks.append((t, "terrain"))
            return None
        if line.startswith("[wx] loop records="):
            self.marks.append((t, "loopend"))
            return None
        if line == "[wx] screen=legend":
            self.legend = True
            return self._record(t, line, "a sent S" if self._sent_within(t, "S") else None)
        if line.startswith("[wx] screen=loop"):
            return self._record(t, line, "a sent T" if self._sent_within(t, "T") else None)
        m = re.match(r"^\[wx\] screen=scenario (\w+) R=\d+$", line)
        if m:
            name = m.group(1)
            why = None
            if self.last is None:
                why = "the first scene seen"
            elif self.last in self.scenes and name == self.scenes[(self.scenes.index(self.last) + 1) % len(self.scenes)]:
                why = "the cycle"
            elif self._mark_within(t, ("zoom", "terrain", "loopend")):
                why = "a zoom, terrain or loop-end line"
            elif self.legend and self._sent_within(t, "T"):
                why = "a T with the legend open"
            elif name == self.last and self._mark_within(t, ("theme",)):
                why = "a theme switch's recompose"
            self.last = name
            self.legend = False
            return self._record(t, line, why)
        return None


def stamp(s):
    base, frac = s.rstrip("Z").split(".")
    return calendar.timegm(time.strptime(base, "%Y-%m-%dT%H:%M:%S")) + int(frac) / 1000.0


def selftest():
    """W-T (A12.66): each case's whole list of explanations must equal its expected list. Cases 3 and 4 are the
    controls: a theme line with no O and no POST, and a repeated scene with no theme line, are both UNEXPLAINED."""
    cases = [
        ("an O, then the switch and its recompose",
         ["[wx] screen=scenario bdry R=60", "#HOST send b'O'", "[wx] gesture O", "[wx] dim=1 brightness=51",
          "[wx] auto offset=+15 min phase=Night", "[wx] theme=dark was=light setting=auto phase=Night",
          "[wx] screen=scenario bdry R=60"],
         ["the first scene seen", "the runner's byte", "a sent O", "a theme switch's recompose"]),
        ("a POST, then the switch and its recompose",
         ["[wx] screen=scenario dry R=60", "#HOST post wx-theme=light",
          "[wx] theme=light was=dark setting=light phase=Night", "[wx] screen=scenario dry R=60"],
         ["the first scene seen", "the runner's POST", "a theme switch's recompose"]),
        ("CONTROL: a switch nobody caused (the 20 s tick at a real dusk)",
         ["[wx] screen=scenario dry R=60", "[wx] theme=dark was=light setting=auto phase=Night"],
         ["the first scene seen", None]),
        ("CONTROL: a repeated scene with no switch",
         ["[wx] screen=scenario dry R=60", "[wx] screen=scenario dry R=60"],
         ["the first scene seen", None]),
    ]
    bad = 0
    for name, lines, want in cases:
        w = Watcher(DALLAS + BEND)
        for i, line in enumerate(lines):
            w.feed(1000.0 + 0.1 * i, line)
        got = [why for _, _, why in w.changes]
        ok = got == want
        bad += not ok
        print(f"  {'ok  ' if ok else 'FAIL'} W-T {name}: {got}" + ("" if ok else f" (want {want})"))
    print(f"{len(cases)} checks, {bad} failures")
    return 1 if bad else 0


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        return selftest()
    if len(argv) != 4 or argv[2] != "--scenes" or argv[3] not in ("6", "8"):
        print(__doc__)
        return 2
    w = Watcher(DALLAS + (BEND if argv[3] == "8" else []))
    bad = []
    for raw in open(argv[1], encoding="utf-8", errors="replace"):
        ts, _, line = raw.rstrip("\n").partition(" ")
        try:
            t = stamp(ts)
        except ValueError:
            continue
        msg = w.feed(t, line)
        if msg:
            bad.append(msg)
    print(f"screen changes: {len(w.changes)}; explained {len(w.changes) - len(bad)}; unexplained {len(bad)}")
    for msg in bad:
        print("  " + msg)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
