"""Weatherscope Stage 1 on the board: read the harness's report once and grade it.

    python -I test/fixtures/wx/capture_stage1.py <COMx> <out.txt> [--budget] [--expect-sha <hex>] [--control]

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
--budget            grade the ruled budget bars (2026-10-20): decode + indices + render + push <= 100 ms per
                    frame, and a projected 15-frame past-hour loop at >= 8 fps (per loop frame: decode +
                    render + push; the indices are worked out once per view). B5-B7's retired bands print
                    as reference points.
--control           send 'D' and stop reading for 1.5 s after 20 rows, so the board's 4 KB TX buffer must
                    overflow and its 2 ms writes must drop rows. PASS = the capture is refused with at least
                    one missing row named; anything else means the tags cannot see a loss.
--diag              grade the diagnostic run (env weatherscope-s3-128-diag; addendum 4): the cold run must
                    reproduce Stage 1's boot numbers within +/-10 %, every variant must give the identical
                    buffer, and each candidate cause is called holds / ruled out / partial by the frozen
                    thresholds, then compared with the bets.
B3, B4 and B8 are graded against their original bands every time.
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
    raw = capture(port, out, b"D" if control else b"r", stall_after_rows=20 if control else None)
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
        ok = refused and len(missing) >= 1
        print(f"  CONTROL (drop-on-stall + a 1.5 s host stall): "
              f"{'REFUSED, missing rows named -> the tags see a loss' if ok else 'NOT refused -> the tags cannot see a loss: STOP'}")
        return 0 if ok else 1
    if refused:
        print(f"  CAPTURE REFUSED: rows missing {missing}, corrupt {bad}, duplicated {dup}. Nothing assembled or graded.")
        return 2

    for k in ("build", "crop", "sprite"):
        print(f"  {k}: {wx.get(k)}")
    ok = True
    retired = {"B5", "B6", "B7"} if ("--budget" in argv or "--expect-sha" in argv or "--diag" in argv) else set()
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
        print(f"  BUDGET B5'-B8' = {total / 1000:.1f} ms per frame (bar <= 100 ms) -> {'as predicted' if total <= 100000 else 'OVER BUDGET'}")
        print(f"  BUDGET loop, 15 frames: {loop_frame / 1000:.1f} ms per frame -> {fps:.1f} fps (bar >= 8; "
              f"{fps_strict:.1f} counting the indices every frame) -> {'as predicted' if fps >= 8 else 'TOO SLOW'}")
        ok &= total <= 100000 and fps >= 8
    if "--diag" in argv:
        ok &= grade_diag(lines)
    return 0 if ok else 1


# ---- the diagnostic run (docs/weatherscope-stage1.md, "Predictions B, addendum 4"; thresholds frozen there) ----
REF_POINTS = {"decode_us": 2867, "index_us": 3584, "render_us": 22012}   # Stage 1's boot numbers, image ed40af2
# The reference screen's sha256 prefix, derived from the file rather than typed in (a 16-hex literal is
# also the device-id shape the pre-commit hook refuses).
REF_SHA16 = hashlib.sha256(open(os.path.join(HERE, "kdfw-60mi.screen.bin"), "rb").read()).hexdigest()[:16]


def grade_diag(lines):
    d = {}
    for l in lines:
        if l.startswith("WXD,"):
            parts = l.split(",")
            d[parts[1]] = {kv.split("=")[0]: kv.split("=")[1] for kv in parts[2:] if "=" in kv}
            d[parts[1]]["_raw"] = parts[2:]
    if "error" in d or "cold" not in d:
        print(f"  DIAG: the run did not happen ({d.get('error', {}).get('_raw')}) -> FAIL")
        return False
    med = lambda k: int(d[k]["median"])
    cold = {k: int(d["cold"][k]) for k in REF_POINTS}
    ok = True
    # (a) the instrument: the cold run must reproduce the boot numbers within +/-10 % each
    inst = all(abs(cold[k] - REF_POINTS[k]) <= 0.10 * REF_POINTS[k] for k in REF_POINTS)
    print("  DIAG (a) cold run vs Stage 1's boot numbers (+/-10 %): " + ", ".join(
        f"{k} {cold[k]:,} vs {REF_POINTS[k]:,}" for k in REF_POINTS) + f" -> {'reproduced' if inst else 'NOT REPRODUCED: the instrument is unreliable, STOP'}")
    if not inst:
        return False
    for k in ("d_flash", "d_ram", "d_ram_int", "i_double", "i_float", "r_hline", "r_direct"):
        print(f"  DIAG {k}: median {med(k):,} us, max {int(d[k]['max']):,} us (n={d[k]['n']})")
    # identity
    cs, fid = d["code_sha"], d["float_index_diff"]
    n_idx, n_pix = int(fid["_raw"][0]), int(fid["pixels"])
    same = all(cs[k] == REF_SHA16 for k in ("d_flash", "d_ram", "d_ram_int", "i_double"))
    float_ok = (cs["i_float"] == REF_SHA16) if n_idx == 0 else True
    sprite_ok = d["sprite_sha"]["r_hline"] == d["sprite_sha"]["r_direct"]
    print(f"  DIAG identity: d_flash/d_ram/d_ram_int/i_double code sha == {REF_SHA16}: {same}; "
          f"r_hline sprite == r_direct sprite: {sprite_ok}; float indices differing {n_idx} of 480 "
          f"(predicted 0-3), {n_pix} pixels affected, float buffer {'identical' if cs['i_float'] == REF_SHA16 else 'differs'}")
    if not (same and float_ok and sprite_ok):
        print("  DIAG identity FAILED -> STOP")
        return False
    ok &= 0 <= n_idx <= 3

    def verdict(name, holds, out):
        v = "holds" if holds else ("ruled out" if out else "partial, not established")
        print(f"  DIAG cause {name}: {v}")
        return v
    v = {}
    v["C1"] = verdict("C1 software doubles (B6)",
                      abs(med("i_double") - cold["index_us"]) <= 0.10 * cold["index_us"] and med("i_float") <= 1000,
                      med("i_float") >= 0.5 * med("i_double") or med("i_double") <= 0.5 * cold["index_us"])
    v["C2"] = verdict("C2 first-touch misses on the flash crop (B5)",
                      med("d_flash") <= 0.60 * cold["decode_us"] and abs(med("d_ram") - med("d_flash")) <= 0.15 * med("d_flash"),
                      med("d_flash") >= 0.85 * cold["decode_us"])
    v["C2'"] = verdict("C2' the PSRAM fill (B5)", med("d_ram_int") <= 0.50 * med("d_ram"), med("d_ram_int") >= 0.80 * med("d_ram"))
    v["C3"] = verdict("C3 writeFastHLine per-call overhead (B7)", med("r_direct") <= 0.50 * med("r_hline"), med("r_direct") >= 0.80 * med("r_hline"))
    v["C3'"] = verdict("C3' first-touch PSRAM misses (B7)", med("r_hline") <= 0.70 * cold["render_us"], med("r_hline") >= 0.90 * cold["render_us"])
    bets = {"C1": "holds", "C2": "ruled out", "C2'": "holds", "C3": "holds", "C3'": "ruled out"}
    for miss, causes in (("B5", ("C2", "C2'")), ("B6", ("C1",)), ("B7", ("C3", "C3'"))):
        held = [c for c in causes if v[c] == "holds"]
        allout = all(v[c] == "ruled out" for c in causes)
        print(f"  DIAG {miss} is explained by: {', '.join(held) if held else ('NOTHING: cause unknown, STOP' if allout else 'not established')}")
        ok &= bool(held)
    match = all(v[c] == bets[c] for c in bets)
    print(f"  DIAG against my bets {bets}: {'AS PREDICTED' if match else 'OFF-PREDICTION'}")
    return ok and match


if __name__ == "__main__":
    sys.exit(main(sys.argv))
