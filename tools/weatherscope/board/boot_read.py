"""A12.33's post-write check: ONE serial open, never reopened, read for a few seconds after a write's reset, and print
only the boot's identity lines -- [build] env=, [ota-slot] boot running=, and the edition's ready/init lines. Prints
no address and no network name (those lines are never printed).

    <penv python> -I tools/weatherscope/board/boot_read.py <COMx> [seconds]

Exit 0 if an [ota-slot] line was read, 1 if not (the boot was missed: say so, never infer the slot).
"""
import re, sys, time

import serial

KEEP = re.compile(r"^\[(build|ota-slot)\] |^\[look\] ready=|^\[wx\] init;|^\[wx\] qbuf ")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    port, secs = argv[1], float(argv[2]) if len(argv) > 2 else 12.0
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False   # both false BEFORE the open: an open must not reset the board
    s.rts = False
    s.open()
    t0, buf, slot = time.time(), b"", False
    try:
        while time.time() - t0 < secs:
            buf += s.read(4096)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").strip()
                if KEEP.search(text):
                    print(f"{time.time() - t0:6.2f}s {text}")
                    slot |= text.startswith("[ota-slot] ")
    finally:
        s.close()
    print("ota-slot read: " + ("yes" if slot else "NO -- the boot was missed; the slot is not inferred"))
    return 0 if slot else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
