"""O9 (docs/weatherscope-stage3.md A1.3, A12.104, A12.107): the REAL flash adapter, on the board, behind ota::Install.

The board runs weatherscope-otabench-s3-128, whose serial key 'B' reads "OTA <total> <n>" and a newline, then n bytes,
feeds them to ota::Install through the real Update adapter as if they were the start of a download of <total> bytes, and
prints one line:

    [ota-bench] result=<token> boot=<before>/<after> slot=<sha256 of the inactive slot's first 64 KB, 12 hex, before>/<after>

THE SEND IS ACKNOWLEDGED (A12.107). The device asks for every chunk of at most 64 bytes with "[ota-bench] more", and this
sends exactly one chunk per request, never ahead of one -- so no more than 64 bytes are ever in flight and the device's
receive buffer cannot overrun. (The unpaced first run, 2026-11-05 17:52Z, lost bytes while the Updater erased a sector;
its byte budget never drained and the CONTROL read the RIG.) It counts requests against chunks: the device must ask for
exactly as many as were sent, and nothing after its budget.

    <penv python> -I tools/weatherscope/board/ota_bench.py <COMx> <blipscope-s3-128.bin> <blipscope-pro-s3-21.bin> \
        <weatherscope-s3-128.bin>

The board asks for its own scope, weatherscope- on s3-128. Predicted (A12.107), each with the boot label unchanged:
  refused-s3-128   the Blipscope s3-128 head (352 bytes, 6 chunks)     refused-foreign, the slot hash unchanged, 0 screen
  refused-s3-21    the blipscope-pro-s3-21 head (352 bytes, 6 chunks)  refused-foreign, the slot hash unchanged, 0 screen
  CONTROL          its own head, the stream cut at 8 KB (128 chunks)   fail-6, the slot hash CHANGED, a progress 0 line --
                                                                       the instrument sees a write, so "unchanged" above
                                                                       means something
ONE serial open for all three, never reopened (CLAUDE.md, the capture rig): DTR and RTS false before it, so it resets
nothing. Prints no address and no network name. Exit 0 AS PREDICTED, 1 OFF-PREDICTION, 2 the rig (and the lines seen).
"""
import os, re, sys, time

import serial

RESULT = re.compile(r"^\[ota-bench\] result=(\S+) boot=(\S+)/(\S+) slot=([0-9a-f]{12})/([0-9a-f]{12})$")
MORE = "[ota-bench] more"
HEAD = 352
CUT = 8192
CHUNK = 64


class Board:
    def __init__(self, port):
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.05
        self.s.dtr = False   # both false BEFORE the open: an open must not reset the board
        self.s.rts = False
        self.s.open()
        self.buf = b""

    def lines(self, secs):
        end = time.time() + secs
        while time.time() < end:
            self.buf += self.s.read(4096)
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                yield raw.decode("utf-8", errors="replace").rstrip("\r")

    def next_line(self, secs):
        for line in self.lines(secs):
            return line
        return None


def run_case(board, name, image, n, want_token, want_slot_changed, want_screen):
    total = len(image)
    seen = []
    for _ in board.lines(1.0):   # let anything in flight drain first
        pass
    board.s.write(b"B" + ("OTA %d %d\n" % (total, n)).encode())
    board.s.flush()
    sent = requests = extra = 0
    result = None
    deadline = time.time() + 10
    while result is None and time.time() < deadline:
        line = board.next_line(0.5)
        if line is None:
            continue
        seen.append(line)
        if line == MORE:
            requests += 1
            if sent < n:
                k = min(CHUNK, n - sent)
                board.s.write(image[sent:sent + k])
                board.s.flush()
                sent += k
            else:
                extra += 1
            # after the last chunk a cut stream takes ~30 s to end (writeRest's 300 empty reads)
            deadline = time.time() + (60 if sent == n else 10)
        elif line.startswith("[ota-bench] result="):
            result = line
    if result is None:
        print(f"{name}: no result line -- the RIG; sent {sent} of {n} in {requests} request(s); the last lines seen:")
        for l in seen[-8:]:
            print(f"    {l}")
        return None
    m = RESULT.match(result)
    if not m:
        print(f"{name}: unreadable result line: {result}")
        return None
    token, b0, b1, s0, s1 = m.groups()
    screen = [l for l in seen if l.startswith("[ota] screen:")]
    chunks = (n + CHUNK - 1) // CHUNK
    good = (token == want_token and b0 == b1 and (s0 != s1) == want_slot_changed and sent == n
            and requests == chunks and extra == 0 and [l.split(": ", 1)[1] for l in screen] == want_screen)
    print(f"{name}: sent {sent} of {total} bytes in {requests} request(s) ({chunks} chunks, {extra} after the budget) "
          f"-> {token}; boot {b0} -> {b1} ({'unchanged' if b0 == b1 else 'CHANGED'}); slot {s0} -> {s1} "
          f"({'CHANGED' if s0 != s1 else 'unchanged'}); screen {[l.split(': ', 1)[1] for l in screen] or '-'}")
    print(f"    want {want_token}, boot unchanged, slot {'changed' if want_slot_changed else 'unchanged'}, "
          f"screen {want_screen or '-'}, every chunk asked for once -> {'AS PREDICTED' if good else 'OFF-PREDICTION'}")
    return good


def main(argv):
    if len(argv) != 5:
        print(__doc__)
        return 2
    port = argv[1]
    imgs = {}
    for key, path in (("s3-128", argv[2]), ("s3-21", argv[3]), ("own", argv[4])):
        data = open(path, "rb").read()
        if len(data) < CUT or data[0] != 0xE9:
            print(f"the rig: {os.path.basename(path)} is not an ESP image of at least {CUT} bytes")
            return 2
        imgs[key] = data
    board = Board(port)
    results = [
        run_case(board, "refused-s3-128", imgs["s3-128"], HEAD, "refused-foreign", False, []),
        run_case(board, "refused-s3-21", imgs["s3-21"], HEAD, "refused-foreign", False, []),
        run_case(board, "CONTROL", imgs["own"], CUT, "fail-6", True, ["progress 0"]),
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
