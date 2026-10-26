"""Build M's run (docs/weatherscope-stage3.md A9.30): what a zoom at the fire costs, under three USB conditions.

    <penv python> -I tools/weatherscope/board/hold_probe.py <COMx> <empty out dir> <flash end, unix seconds>

  M2  nothing opens the port after the flash: the board's own auto-fires land at boot + 60, 90 and 120 s. The port
      is first opened at flash end + 140 s, and 'R' prints those records.
  M1  the port is read throughout: 'H' three times, 6 s apart, then 'R'.
  M3  'A' schedules three auto-fires at +20, +40 and +60 s; the port is read for 5 s, then NOT READ for 75 s while
      it stays open; then read again, and 'R'.

One open, never repeated (CLAUDE.md, the capture rig): a read error stops the run and says so. The baseline span is
the same as the gap's: one push's end to the next push's start (build M's commit). Prints no coordinate.
Grading is a separate step against A9.30's table; this only captures.
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
            self.lines.append(line)
            self.f.write(f"{ts} {line}\n")
            self.f.flush()

    def note(self, text):
        self.add(text)
        print(text, flush=True)

    def send(self, b):
        self.note(f"#HOST send {b!r}")
        self.ser.write(b)
        self.ser.flush()

    def close(self):
        self.note("#HOST closing the port")
        self.stop = True
        self.t.join(timeout=2)
        self.ser.close()
        self.f.close()


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 2
    port, out, flash_end = argv[1], argv[2], float(argv[3])
    if os.path.exists(out) and os.listdir(out):
        sys.exit(f"REFUSED: {out} is not empty")
    os.makedirs(out, exist_ok=True)
    open_at = flash_end + 140
    wait = open_at - time.time()
    print(f"M2: the port stays closed for {max(0, wait):.0f} s more (until flash end + 140 s)", flush=True)
    if wait > 0:
        time.sleep(wait)
    rig = Rig(port, out)
    try:
        rig.note(f"#HOST opened {port} at flash end + {time.time() - flash_end:.1f} s (M2: never opened before)")
        time.sleep(3)
        rig.note("#HOST M2 records:")
        rig.send(b"R")
        time.sleep(4)
        rig.note("#HOST M1: 'H' x3, the port read throughout")
        for _ in range(3):
            rig.send(b"H")
            time.sleep(6)
        rig.send(b"R")
        time.sleep(4)
        rig.note("#HOST M3: 'A', read 5 s, then NOT READ for 75 s with the port open")
        rig.send(b"A")
        time.sleep(5)
        rig.paused = True
        rig.note("#HOST M3: reader paused")
        time.sleep(75)
        rig.paused = False
        rig.note("#HOST M3: reader resumed")
        time.sleep(3)
        rig.send(b"R")
        time.sleep(5)
    finally:
        rig.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
