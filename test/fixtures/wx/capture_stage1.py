"""Weatherscope Stage 1 on the board: read the harness's report once and grade it against B3-B9.

    python -I test/fixtures/wx/capture_stage1.py <COMx> <out.txt> [--expect-sha <hex>]

ONE open of the port, never repeated (CLAUDE.md, the capture rig: a reattach is an intervention in the
board). DTR and RTS are held low, which does not reset a running board. Sends one byte -- the harness
prints its report on any byte -- reads until "WX,end", saves every byte to <out.txt>, then grades:
  B3 psram_used 144,000-160,000 B      B4 internal_heap_delta <= 2,048 B
  B5 decode_us 200-2,000               B6 index_us <= 1,500
  B7 render_us 2,000-10,000            B8 push_us 23,000-32,000
  B9 the WXFB dump's sha256 == kdfw-60mi.screen.bin's (render_ref.py; 344383b4...) and == the board's own
     fb_sha256 line. If they differ: how many pixels, and is every difference one cell away (the
     pre-registered escape hatch -- report, do not tune)?
--expect-sha replaces the reference sha (used for the S-B1 sabotage run, whose dump must NOT match).
Prints values and verdicts; exit 0 when every check reads as predicted, 1 otherwise, 2 if the capture
itself failed (no "WX,end" before the timeout: the instrument, not the board).
"""
import base64
import hashlib
import os
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
BANDS = {   # frozen in docs/weatherscope-stage1.md, Predictions B
    "psram_used": ("B3", 144000, 160000),
    "internal_heap_delta": ("B4", None, 2048),
    "decode_us": ("B5", 200, 2000),
    "index_us": ("B6", None, 1500),
    "render_us": ("B7", 2000, 10000),
    "push_us": ("B8", 23000, 32000),
}


def capture(port, out, timeout=90):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = False
    s.rts = False
    s.open()                                    # the one and only open
    try:
        time.sleep(1.0)
        s.reset_input_buffer()
        s.write(b"r")
        buf = bytearray()
        t0 = time.time()
        while time.time() - t0 < timeout:
            chunk = s.read(65536)
            if chunk:
                buf += chunk
                if b"WX,end" in buf:
                    time.sleep(0.2)
                    buf += s.read(65536)
                    break
    finally:
        s.close()
    open(out, "wb").write(bytes(buf))
    return bytes(buf)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    port, out = argv[1], argv[2]
    raw = capture(port, out)
    text = raw.decode("ascii", errors="replace")
    if "WX,end" not in text:
        print(f"CAPTURE FAILED: no WX,end within the timeout ({len(raw)} bytes saved to {out}). The instrument, not the board.")
        return 2
    lines = [l.strip() for l in text.replace("\r", "").split("\n")]
    wx = {}
    fb = None
    for l in lines:
        if l.startswith("WXFB,"):
            fb = l[5:]
        elif l.startswith("WX,"):
            parts = l.split(",", 2)
            if len(parts) == 3:
                wx[parts[1]] = parts[2]
    print(f"captured {len(raw)} bytes -> {os.path.basename(out)}")
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
        ok &= good
        band = f"{'' if lo is None else lo}..{'' if hi is None else hi}"
        print(f"  {bid} {key} = {n:,} (predicted {band}) -> {'as predicted' if good else 'OFF-PREDICTION'}")
    ref = open(os.path.join(HERE, "kdfw-60mi.screen.bin"), "rb").read()
    want = argv[argv.index("--expect-sha") + 1] if "--expect-sha" in argv else hashlib.sha256(ref).hexdigest()
    if fb is None:
        print("  B9: no WXFB line -> FAIL")
        return 1
    try:
        dump = base64.b64decode(fb, validate=True)
    except Exception as e:
        print(f"  B9: WXFB is not valid base64 ({e}) -> FAIL")
        return 1
    got = hashlib.sha256(dump).hexdigest()
    print(f"  B9 dump: {len(dump)} bytes, sha256 {got[:16]}; board's own fb_sha256 {wx.get('fb_sha256', '?')[:16]}; "
          f"{'consistent' if got == wx.get('fb_sha256') else 'INCONSISTENT (transport?)'}")
    differ = sum(1 for a, b in zip(dump, ref) if a != b) if len(dump) == len(ref) else -1
    match = got == want
    print(f"  B9 vs {'the expected sha' if '--expect-sha' in argv else 'render_ref.py'} ({want[:16]}): "
          f"{'IDENTICAL' if match else 'DIFFERENT'}; {differ} of {len(ref)} pixels differ from the reference screen")
    ok &= match and got == wx.get("fb_sha256") and len(dump) == 57600
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
