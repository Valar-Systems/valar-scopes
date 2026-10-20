"""Weatherscope Stage 1 on the board: read the harness's report once and grade it.

    python -I test/fixtures/wx/capture_stage1.py <COMx> <out.txt> [--budget] [--expect-sha <hex>] [--control [--stall <s>]]

ONE open of the port, never repeated (CLAUDE.md, the capture rig: a reattach is an intervention in the
board). DTR and RTS are held low, which does not reset a running board. Sends one byte -- 'r' for the
blocking dump, 'D' (with --control) for the old drop-on-stall dump -- reads until "WX,end", saves every
byte to <out.txt>, then grades.

THE DUMP IS CHECKED ROW BY ROW (docs/weatherscope-stage1.md, addendum 3). Every screen row arrives as
`WXR,<row>,<crc32>,<base64>`. A row that is missing, duplicated, not valid base64, not 240 bytes, or whose
zlib CRC-32 disagrees is NAMED, and a capture with any such row is REFUSED: nothing is assembled or graded
from it (exit 2 -- the instrument, not the board). Only a complete, checked dump is graded:
  B9  its sha256 == kdfw-60mi.screen.bin's (render_ref.py; 344383b4...) and == the board's own fb_sha256
--expect-sha <hex>  grade the dump against that sha instead (S-B1, the north/south flip): B9 against the
                    reference must FAIL, with the disc and echo agreement reported (predicted 52.1 % / 0.6 %).
--budget            also grade the budget bars (addendum 2, frozen LATER than B5-B7 and kept apart from them):
                    decode + indices + render + push <= 100 ms per frame, and a projected 15-frame past-hour
                    loop at >= 8 fps (per loop frame: decode + render + push; the indices are worked out once
                    per view). A separate block: it does not stand in for B5-B7.
--control           send 'D' and stop reading for --stall seconds (default 1.5) after 20 rows, so the board's
                    drop-on-stall writes can lose data. Graded by addendum 6 (ruled 2026-10-20, option 1):
                      PASS (exit 0)          refused, with at least one bad row named (missing/corrupt/duplicated)
                      INCONCLUSIVE (exit 3)  every row intact and the dump equals the board's fb_sha256: nothing
                                             was lost, so the tags were not tested. Next stall on the frozen ladder
                                             (1.5, 3, 6, 12 s); four INCONCLUSIVE attempts: stop and decide
                      FAIL (exit 1)          every row passed its checks but the dump disagrees with fb_sha256: a
                                             loss the tags did not see
B3-B8 are graded against their original frozen bands every time. B5-B7 are STANDING MISSES (Daniel,
2026-10-20: misses on their frozen bands, unchanged): each prints MISS against its frozen band and does not
gate a run. What does gate is that they repeat the reference points within +/-10 % (addendum 5); a timing
that moved is not one of the measurements those misses describe.
"""
import base64
import hashlib
import os
import re
import sys
import time
import zlib

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
ROWS, ROW_BYTES = 240, 240
BANDS = {   # frozen in docs/weatherscope-stage1.md, Predictions B
    "psram_used": ("B3", 144000, 160000),
    "internal_heap_delta": ("B4", None, 2048),
    "decode_us": ("B5", 200, 2000),
    "index_us": ("B6", None, 1500),
    "render_us": ("B7", 2000, 10000),
    "push_us": ("B8", 23000, 32000),
}
STANDING_MISSES = {"B5", "B6", "B7"}   # ruled 2026-10-20: misses on their frozen bands; reported, never re-graded
REF_POINTS = {"decode_us": 2867, "index_us": 3584, "render_us": 22012}   # addendum 2: the B5-B7 measurements
ROW_RE = re.compile(r"^WXR,(\d+),([0-9a-f]{8}),([A-Za-z0-9+/=]*)$")


def capture(port, out, cmd, stall_after_rows=None, stall_s=1.5, timeout=120):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()                                    # the one and only open
    try:
        time.sleep(1.0)
        s.reset_input_buffer()
        s.write(cmd)
        buf = bytearray()
        stalled = False
        t0 = time.time()
        while time.time() - t0 < timeout:
            chunk = s.read(65536)
            if chunk:
                buf += chunk
                if stall_after_rows and not stalled and buf.count(b"\nWXR,") >= stall_after_rows:
                    time.sleep(stall_s)         # the CONTROL: stop reading, so the board's writes must stall
                    stalled = True
                if b"WX,end" in buf[-64:] or buf.rstrip().endswith(b"WX,end"):
                    time.sleep(0.2)
                    buf += s.read(65536)
                    break
    finally:
        s.close()
    open(out, "wb").write(bytes(buf))
    return bytes(buf)


def check_rows(lines):
    """Returns (rows dict row->bytes, missing, duplicated, corrupt, unparseable line count)."""
    rows, dup, bad, junk = {}, [], [], 0
    for l in lines:
        if not l.startswith("WXR,"):
            continue
        m = ROW_RE.match(l)
        if not m:
            junk += 1
            continue
        y, crc, b64 = int(m.group(1)), int(m.group(2), 16), m.group(3)
        if not 0 <= y < ROWS:
            junk += 1
            continue
        try:
            data = base64.b64decode(b64, validate=True)
        except Exception:
            bad.append(y)
            continue
        if len(data) != ROW_BYTES or zlib.crc32(data) != crc:
            bad.append(y)
            continue
        if y in rows:
            dup.append(y)
            continue
        rows[y] = data
    missing = [y for y in range(ROWS) if y not in rows and y not in bad]
    return rows, missing, sorted(set(dup)), sorted(set(bad)), junk


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    port, out = argv[1], argv[2]
    control = "--control" in argv
    stall = float(argv[argv.index("--stall") + 1]) if "--stall" in argv else 1.5
    raw = capture(port, out, b"D" if control else b"r", stall_after_rows=20 if control else None, stall_s=stall)
    text = raw.decode("ascii", errors="replace").replace("\r", "")
    lines = [l.strip() for l in text.split("\n")]
    if "WX,end" not in lines:
        print(f"CAPTURE FAILED: no WX,end within the timeout ({len(raw)} bytes saved to {out}). The instrument, not the board.")
        return 2
    wx = {}
    for l in lines:
        if l.startswith("WX,"):
            parts = l.split(",", 2)
            if len(parts) == 3:
                wx[parts[1]] = parts[2]
    print(f"captured {len(raw)} bytes -> {os.path.basename(out)}  (dump mode: {wx.get('dump', '?')})")
    rows, missing, dup, bad, junk = check_rows(lines)
    refused = bool(missing or dup or bad)
    print(f"  rows: {len(rows)}/{ROWS} good; missing {missing}; corrupt {bad}; duplicated {dup}; unparseable lines {junk}")
    if control:
        head = f"  CONTROL (drop-on-stall + a {stall:g} s host stall):"
        if refused:   # addendum 6: refused AND >= 1 bad row named; refused implies one of the three lists is non-empty
            print(f"{head} REFUSED, bad rows named: missing {missing}, corrupt {bad}, duplicated {dup} "
                  f"-> PASS (the tags see a loss)")
            return 0
        got = hashlib.sha256(b"".join(rows[y] for y in range(ROWS))).hexdigest()
        if got == wx.get("fb_sha256"):
            print(f"{head} NOT refused: every row intact and the dump equals the board's fb_sha256 "
                  f"-> INCONCLUSIVE (nothing was lost; next stall on the ladder)")
            return 3
        print(f"{head} NOT refused, but the dump ({got[:16]}) disagrees with the board's fb_sha256 "
              f"({wx.get('fb_sha256', '?')[:16]}) -> FAIL: a loss the tags did not see")
        return 1
    if refused:
        print(f"  CAPTURE REFUSED: rows missing {missing}, corrupt {bad}, duplicated {dup}. Nothing assembled or graded.")
        return 2

    for k in ("build", "crop", "sprite"):
        print(f"  {k}: {wx.get(k)}")
    ok = True
    for key, (bid, lo, hi) in BANDS.items():
        v = wx.get(key)
        try:
            n = int(v)
        except (TypeError, ValueError):
            print(f"  {bid} {key}: UNREADABLE ({v!r}) -> FAIL")
            ok = False
            continue
        good = (lo is None or n >= lo) and (hi is None or n <= hi)
        band = f"{'' if lo is None else lo}..{'' if hi is None else hi}"
        if bid in STANDING_MISSES:
            ref = REF_POINTS[key]
            repeats = abs(n - ref) <= 0.10 * ref
            ok &= repeats
            print(f"  {bid} {key} = {n:,} (frozen band {band}) -> {'inside the band' if good else 'MISS'}, a standing "
                  f"miss; vs the measured {ref:,}: {100 * (n - ref) / ref:+.1f} % -> "
                  f"{'repeats (+/-10 %)' if repeats else 'MOVED: not the measurement the miss describes, STOP'}")
            continue
        ok &= good
        print(f"  {bid} {key} = {n:,} (predicted {band}) -> {'as predicted' if good else 'OFF-PREDICTION'}")

    dump = b"".join(rows[y] for y in range(ROWS))
    ref = open(os.path.join(HERE, "kdfw-60mi.screen.bin"), "rb").read()
    got = hashlib.sha256(dump).hexdigest()
    want = argv[argv.index("--expect-sha") + 1] if "--expect-sha" in argv else hashlib.sha256(ref).hexdigest()
    consistent = got == wx.get("fb_sha256")
    differ = sum(1 for a, b in zip(dump, ref) if a != b)
    print(f"  dump: {len(dump)} bytes from 240 checked rows, sha256 {got[:16]}; board's own fb_sha256 "
          f"{wx.get('fb_sha256', '?')[:16]} -> {'consistent' if consistent else 'INCONSISTENT'}")
    print(f"  B9 vs {'the expected sha' if '--expect-sha' in argv else 'render_ref.py'} ({want[:16]}): "
          f"{'IDENTICAL' if got == want else 'DIFFERENT'}; {differ} of {len(ref)} pixels differ from the reference screen")
    ok &= got == want and consistent
    if "--expect-sha" in argv:
        disc = [i for i in range(len(ref)) if ref[i] != 255]
        echo = [i for i in disc if 1 <= ref[i] <= 13]
        same_disc = 100 * sum(1 for i in disc if dump[i] == ref[i]) / len(disc)
        same_echo = 100 * sum(1 for i in echo if dump[i] == ref[i]) / len(echo)
        b9_fails = got != hashlib.sha256(ref).hexdigest()
        print(f"  S-B1: B9 against the reference {'FAILS' if b9_fails else 'PASSES (the sabotage did not bite)'}; "
              f"{same_disc:.1f}% of disc pixels and {same_echo:.1f}% of echo pixels equal the reference "
              f"(predicted 52.1% and 0.6%)")
        ok &= b9_fails
    if "--budget" in argv:
        try:
            d, ix, r, p = (int(wx[k]) for k in ("decode_us", "index_us", "render_us", "push_us"))
        except (KeyError, ValueError):
            print("  BUDGET: a timing line is missing or unreadable -> FAIL")
            return 1
        total, loop_frame = d + ix + r + p, d + r + p
        fps, fps_strict = 1e6 / loop_frame, 1e6 / total
        print("  BUDGET BARS (addendum 2, frozen later; a separate check, not a re-registration of B5-B7):")
        print(f"  BUDGET per frame, decode + indices + render + push = {total / 1000:.1f} ms (bar <= 100 ms) -> "
              f"{'as predicted' if total <= 100000 else 'OVER BUDGET'}")
        print(f"  BUDGET loop, 15 frames: {loop_frame / 1000:.1f} ms per frame -> {fps:.1f} fps (bar >= 8; "
              f"{fps_strict:.1f} counting the indices every frame) -> {'as predicted' if fps >= 8 else 'TOO SLOW'}")
        ok &= total <= 100000 and fps >= 8
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
