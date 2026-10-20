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
--budget grades the ruled budget bars (2026-10-20) instead of B5-B7's retired bands:
  decode + indices + render + push <= 100 ms per frame, and a projected 15-frame past-hour loop at
  >= 8 fps (per loop frame: decode + render + push; the indices are worked out once per view).
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
    # B5-B7's narrow bands were retired by the 2026-10-20 ruling (recorded MISSES): under --budget, and in
    # the sabotage run, they print as reference points and the budget bars grade instead.
    retired = {"B5", "B6", "B7"} if ("--budget" in argv or "--expect-sha" in argv) else set()
    for key, (bid, lo, hi) in BANDS.items():
        v = wx.get(key)
        try:
            n = int(v)
        except (TypeError, ValueError):
            print(f"  {bid} {key}: UNREADABLE ({v!r}) -> FAIL")
            ok = False
            continue
        if bid in retired:
            print(f"  {bid} {key} = {n:,} (reference point; the band is retired, the budget bars grade it)")
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
    if "--expect-sha" in argv and len(dump) == len(ref):
        # S-B1: the dump must be the predicted flipped screen, so B9 against the reference FAILS.
        disc = [i for i in range(len(ref)) if ref[i] != 255]
        echo = [i for i in disc if 1 <= ref[i] <= 13]
        same_disc = 100 * sum(1 for i in disc if dump[i] == ref[i]) / len(disc)
        same_echo = 100 * sum(1 for i in echo if dump[i] == ref[i]) / len(echo)
        b9_fails = hashlib.sha256(dump).hexdigest() != hashlib.sha256(ref).hexdigest()
        print(f"  S-B1: B9 against the reference {'FAILS' if b9_fails else 'PASSES (the sabotage did not bite)'}; "
              f"{same_disc:.1f}% of disc pixels and {same_echo:.1f}% of echo pixels equal the reference "
              f"(predicted 52.1% and 0.6%)")
        ok &= b9_fails
    if "--budget" in argv:
        # Ruled 2026-10-20 (docs/weatherscope-stage1.md, "Predictions B, addendum 2"): budget bars, not bands.
        try:
            d, ix, r, p = (int(wx[k]) for k in ("decode_us", "index_us", "render_us", "push_us"))
        except (KeyError, ValueError):
            print("  BUDGET: a timing line is missing or unreadable -> FAIL")
            return 1
        total = d + ix + r + p
        loop_frame = d + r + p                  # a loop keeps one view: indices once, then decode + render + push
        fps, fps_strict = 1e6 / loop_frame, 1e6 / total
        b_total = total <= 100000
        b_fps = fps >= 8.0
        print(f"  BUDGET B5'-B8' decode+indices+render+push = {total / 1000:.1f} ms per frame (bar <= 100 ms) -> "
              f"{'as predicted' if b_total else 'OVER BUDGET'}")
        print(f"  BUDGET past-hour loop, 15 frames: {loop_frame / 1000:.1f} ms per frame -> {fps:.1f} fps "
              f"(bar >= 8 fps; counting the indices every frame too: {fps_strict:.1f} fps) -> "
              f"{'as predicted' if b_fps else 'TOO SLOW'}")
        ok &= b_total and b_fps
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
