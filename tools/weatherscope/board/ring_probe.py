"""Item 1's run (docs/weatherscope-stage3.md A9.34): bench presses through HandleTouch's own path, under M1 and M3.

    <penv python> -I tools/weatherscope/board/ring_probe.py <COMx> <empty out dir> <M1 presses> <M3 presses>

Each press starts 2 s after a cycle step's "[wx] screen=" line (not one that follows a "[wx] zoom" line), so no 30 s
cycle step falls inside it: 'P', 'L' 1.5 s later, 'R' 2 s after the lift. Under M3 the reader is paused from just
before the 'P' until 1 s after the 'R'. One open, never repeated: a read error stops the run. Prints no coordinate.
Grade with grade_ring.py.
"""
import os, sys, threading, time

import serial


class Rig:
    def __init__(self, port, out):
        self.f = open(os.path.join(out, "trace.txt"), "w", encoding="utf-8", newline="\n")
        self.lock, self.stop, self.paused, self.lines = threading.Lock(), False, False, []
        self.ser = serial.Serial()
        self.ser.port, self.ser.baudrate, self.ser.timeout = port, 115200, 0.2
        self.ser.dtr, self.ser.rts = False, False
        self.ser.open()
        self.t = threading.Thread(target=self.read, daemon=True)
        self.t.start()

    def read(self):
        buf = b""
        while not self.stop:
            if self.paused:
                time.sleep(0.05)
                continue
            try:
                chunk = self.ser.read(4096)
            except Exception as e:
                self.note(f"#RIG read error: {type(e).__name__}: {e} -- the run stops here")
                self.stop = True
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.add(line.decode("utf-8", errors="replace").rstrip("\r"))

    def add(self, line):
        ts = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime()) + f".{int(time.time() * 1000) % 1000:03d}Z"
        with self.lock:
            self.lines.append((time.time(), line))
            self.f.write(f"{ts} {line}\n")
            self.f.flush()

    def note(self, text):
        self.add(text)
        print(text, flush=True)

    def send(self, b):
        self.note(f"#HOST send {b!r}")
        self.ser.write(b)
        self.ser.flush()

    def wait_cycle_step(self, timeout=40):
        """The next '[wx] screen=' line that is a cycle step: its previous board line is not a '[wx] zoom' line."""
        with self.lock:
            start = len(self.lines)
        t_end = time.time() + timeout
        while time.time() < t_end and not self.stop:
            with self.lock:
                new = self.lines[start:]
            for i, (_, line) in enumerate(new):
                if line.startswith("[wx] screen=scenario"):
                    board = [l for _, l in self.lines[:start + i] if not l.startswith("#")]
                    zoomed = any(l.startswith("[wx] zoom") for l in board[-24:])   # a rebuild prints up to ~16 lines
                    if not zoomed:
                        return True
            time.sleep(0.1)
        return False

    def close(self):
        self.note("#HOST closing the port")
        self.stop = True
        self.t.join(timeout=2)
        self.ser.close()
        self.f.close()


def press(rig, cond, k):
    if not rig.wait_cycle_step():
        rig.note(f"#HOST {cond} press {k}: no cycle step within 40 s -- BLIND, recorded, no retry")
        return
    time.sleep(2.0)
    rig.note(f"#HOST {cond} press {k}: begin")
    if cond == "M3":
        rig.paused = True
        rig.note("#HOST M3: reader paused")
    rig.send(b"P")
    time.sleep(1.5)
    rig.send(b"L")
    time.sleep(2.0)
    rig.send(b"R")
    if cond == "M3":
        time.sleep(1.0)
        rig.paused = False
        rig.note("#HOST M3: reader resumed")
    time.sleep(3.0)
    rig.note(f"#HOST {cond} press {k}: end")


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    port, out, n1, n3 = argv[1], argv[2], int(argv[3]), int(argv[4])
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    rig = Rig(port, out)
    try:
        rig.note(f"#HOST opened {port}; {n1} M1 presses, then {n3} M3 presses")
        time.sleep(1.0)
        rig.send(b"R")   # clears the records the board kept before this run
        for k in range(n1):
            press(rig, "M1", k)
        for k in range(n3):
            press(rig, "M3", k)
    finally:
        rig.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
