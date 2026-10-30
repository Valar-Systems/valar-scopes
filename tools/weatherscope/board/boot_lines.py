"""3.5b step 8's boot capture (docs/weatherscope-stage3.md A12.55): ONE serial open, never reopened, after a reset;
every line for a few seconds goes to a file, and the boot's identity lines and A12.53's boot lines are printed.
boot_read.py's rule (A12.33) otherwise: a missed boot is said, never inferred.

    <penv python> -I tools/weatherscope/board/boot_lines.py <COMx> <out file> [seconds]

Prints [build], [ota-slot], [wx] bend, [wx] psram_used/psram_base and [wx] init lines only: never an address or a
network name. The file holds every line as read; redact it (LAN IP, BSSID, SSID) before anything is committed.
Exit 0 if an [ota-slot] line was read, 1 if not.
"""
import re, sys, time

import serial

SHOW = re.compile(r"^\[(build|ota-slot)\] |^\[wx\] (bend |psram_used=|psram_base=|init;)")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    port, out, secs = argv[1], argv[2], float(argv[3]) if len(argv) > 3 else 15.0
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False   # both false BEFORE the open: an open must not reset the board
    s.rts = False
    s.open()
    t0, buf, slot = time.time(), b"", False
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        while time.time() - t0 < secs:
            buf += s.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                f.write(f"{time.time() - t0:6.2f}s {line}\n")
                if SHOW.search(line):
                    print(f"{time.time() - t0:6.2f}s {line}")
                    slot |= line.startswith("[ota-slot] ")
    s.close()
    print("ota-slot read: " + ("yes" if slot else "NO -- the boot was missed; the slot is not inferred"))
    return 0 if slot else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
