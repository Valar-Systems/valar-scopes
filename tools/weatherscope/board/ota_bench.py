"""O9 (docs/weatherscope-stage3.md A1.3, A12.104): the REAL flash adapter, on the board, behind ota::Install.

The board runs weatherscope-otabench-s3-128, whose serial key 'B' reads "OTA <total> <n>" and a newline, then n bytes,
feeds them to ota::Install through the real Update adapter as if they were the start of a download of <total> bytes, and
prints one line:

    [ota-bench] result=<token> boot=<before>/<after> slot=<sha256 of the inactive slot's first 64 KB, 12 hex, before>/<after>

    <penv python> -I tools/weatherscope/board/ota_bench.py <COMx> <blipscope-s3-128.bin> <blipscope-s3-146.bin> \
        <weatherscope-s3-128.bin>

The board asks for its own scope, weatherscope- on s3-128. Predicted (A12.104), each with the boot label unchanged:
  refused-s3-128   the Blipscope s3-128 head (352 bytes)        refused-foreign, the slot hash unchanged
  refused-s3-146   the blipscope-s3-146 head (352 bytes)        refused-foreign, the slot hash unchanged
  CONTROL          its own head with the stream cut at 8 KB     fail-6, the slot hash CHANGED -- the instrument can see
                                                                a write, so "unchanged" above means something
ONE serial open for all three, never reopened (CLAUDE.md, the capture rig): DTR and RTS false before it, so it resets
nothing. Prints no address and no network name. Exit 0 AS PREDICTED, 1 OFF-PREDICTION, 2 the rig.
"""
import os, re, sys, time

import serial

RESULT = re.compile(r"^\[ota-bench\] result=(\S+) boot=(\S+)/(\S+) slot=([0-9a-f]{12})/([0-9a-f]{12})$")
HEAD = 352
CUT = 8192


class Board:
    def __init__(self, port):
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.2
        self.s.dtr = False   # both false BEFORE the open: an open must not reset the board
        self.s.rts = False
        self.s.open()
        self.buf = b""
        self.t0 = time.time()

    def lines(self, secs):
        end = time.time() + secs
        while time.time() < end:
            self.buf += self.s.read(4096)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                yield raw.decode("utf-8", errors="replace").rstrip("\r")

    def wait_for(self, prefix, secs, seen):
        for line in self.lines(secs):
            seen.append(line)
            if line.startswith(prefix):
                return line
        return None

    def send(self, data, chunk=64, pause=0.004):
        for i in range(0, len(data), chunk):
            self.s.write(data[i:i + chunk])
            self.s.flush()
            time.sleep(pause)


def run_case(board, name, image, n, want_token, want_slot_changed):
    total = len(image)
    seen = []
    for _ in board.lines(1.0):  # let anything in flight drain first
        pass
    board.s.write(b"B" + ("OTA %d %d\n" % (total, n)).encode())
    board.s.flush()
    if board.wait_for("[ota-bench] go", 10, seen) is None:
        print(f"{name}: the board never said go -- the rig")
        for l in seen[-5:]:
            print(f"    {l}")
        return None
    board.send(image[:n])
    line = board.wait_for("[ota-bench] result=", 60, seen)
    if line is None:
        print(f"{name}: no result line within 60 s -- the rig")
        return None
    m = RESULT.match(line)
    if not m:
        print(f"{name}: unreadable result line: {line}")
        return None
    token, b0, b1, s0, s1 = m.groups()
    screen = [l for l in seen if l.startswith("[ota] screen:")]
    good = token == want_token and b0 == b1 and (s0 != s1) == want_slot_changed
    print(f"{name}: sent {n} of {total} bytes -> {token}; boot {b0} -> {b1} ({'unchanged' if b0 == b1 else 'CHANGED'}); "
          f"slot {s0} -> {s1} ({'CHANGED' if s0 != s1 else 'unchanged'}); screen lines {len(screen)}")
    print(f"    want {want_token}, boot unchanged, slot {'changed' if want_slot_changed else 'unchanged'} "
          f"-> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    for l in screen:
        print(f"    {l}")
    return good


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    port = argv[1]
    imgs = {}
    for key, path in (("s3-128", argv[2]), ("s3-146", argv[3]), ("own", argv[4])):
        data = open(path, "rb").read()
        if len(data) < CUT or data[0] != 0xE9:
            print(f"the rig: {os.path.basename(path)} is not an ESP image of at least {CUT} bytes")
            return 2
        imgs[key] = data
    board = Board(port)
    results = [
        run_case(board, "refused-s3-128", imgs["s3-128"], HEAD, "refused-foreign", False),
        run_case(board, "refused-s3-146", imgs["s3-146"], HEAD, "refused-foreign", False),
        run_case(board, "CONTROL", imgs["own"], CUT, "fail-6", True),
    ]
    board.s.close()
    if any(r is None for r in results):
        print("O9: the RIG -- not graded")
        return 2
    ok = all(results)
    print("O9: " + ("AS PREDICTED" if ok else "OFF-PREDICTION"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
