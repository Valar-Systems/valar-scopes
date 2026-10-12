#!/usr/bin/env python3
"""d6-capture.py -- record one D6 station's serial output, byte for byte, from a
Missileer bench board named by USB serial.

WHY NOT `pio device monitor | tee` (the runbook's line):
  * `pio device monitor` with no -p picks a port itself, and the Blipscope soak
    board is usually attached too. Ending a monitor resets the board it was on
    (bench-capture.ps1's header), and a reset ends the soak.
  * The probe's provenance block prints ONCE, at boot, and the probes wait at most
    3 s for a host (gametest waits 0.3 s). A monitor opened by hand after a flash
    usually misses it, and stations 1-2 are BAD runs without it.
  * In Windows PowerShell 5.1, `Tee-Object` writes UTF-16, so a "CSV" fixture
    comes out UTF-16.

WHAT THIS DOES INSTEAD
  * Resolves the board's port from its USB serial (scripts/board-port.py), refuses
    the Blipscope soak board and any non-Missileer board, and REFUSES A BOARD ON A
    TinyUSB BUILD: every D6 image is ARDUINO_USB_MODE=1 (USB-Serial-JTAG, the
    colon-spelled serial), so a TinyUSB port means a Missileer image is running
    and the D6 flash did not take.
  * Opens with DTR=0 and RTS=0, set BEFORE open. On this board's USB-Serial-JTAG
    that open RESETS it (measured: bench-capture.ps1 header, 2026-08-05: "DTR=false
    RTS=false -> RESET (ESP-ROM:esp32s3-... every time)"), so the boot and its
    provenance block land at the head of the capture. This is the OPPOSITE of the
    TinyUSB rule in platformio.ini [env:missileer-s3-128] (`--rts 0 --dtr 1`), and
    it is only right because the TinyUSB case is refused above.
  * If no boot shows within 8 s it says so; re-run with --reset, which drives the
    reset esptool itself uses on USB-Serial-JTAG (RTS high 100 ms, DTR low).
  * Writes the bytes exactly as received to bench-logs/<name>-<UTC yyyymmdd-HHMM>.csv
    (gesture) or .log (the rest), and a sidecar <file>.meta.txt with date_utc,
    board, port and -- with --image -- the image's MANIFEST (env, commit, sha256).
    The sidecar is what ties stations 3 and 4 to a build: neither prints a
    provenance block.
  * Never reopens after losing the port: a reopen into a boot window resets the
    board again (bench-capture.ps1, 2026-09-10), so a lost port ends the capture,
    loudly.
  * At the end, prints FACTS from the capture for the checklist (provenance lines,
    marker counts). It judges nothing; the runbook's criteria are the operator's.

USAGE (from the repo root)
    python scripts/d6-capture.py --board missileer --name gesture \
        --image C:\\Github\\missileer-overnight\\d6\\images\\station1-gesture
    names: gesture | screenstate | blit | armA | armB | armC | arm1 | arm2 | arm3
    stop with Ctrl+C.          --selftest: no board (pyserial's loop:// port)

EXIT  0 captured   2 usage   3 port lost mid-capture   4 board refused   5 not connected
"""
from __future__ import annotations

import argparse
import datetime
import importlib.util
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
NAMES = ("gesture", "screenstate", "blit", "armA", "armB", "armC", "arm1", "arm2", "arm3")
BOOT_MARKS = (b"ESP-ROM:", b"PROVENANCE", b"[build] env=", b"=== BlitProbe")


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def utc():
    return datetime.datetime.now(datetime.timezone.utc)


def facts(data: bytes, name: str, manifest: dict | None):
    """Plain counts and provenance lines from a capture -- no verdicts."""
    t = data.decode("utf-8", "replace")
    out = []
    prov = {k: re.search(rf"^# {k}\s+(.*)$", t, re.M) for k in
            ("label", "elf_sha256", "variant", "partition", "psram_total", "board_mac")}
    if prov["label"]:
        for k, m in prov.items():
            out.append(f"provenance {k:11} {m.group(1).strip() if m else '(line absent)'}")
        if manifest and prov["elf_sha256"]:
            want = manifest.get("provenance_elf_sha256", "")
            got = prov["elf_sha256"].group(1).strip()
            out.append(f"elf_sha256 vs MANIFEST  {'MATCH' if got == want else 'DIFFERENT'} "
                       f"(capture {got} / manifest {want})")
    else:
        out.append("provenance block: NOT in this capture" +
                   (" (this probe prints none)" if name in ("blit",) or name.startswith("arm") else ""))
    out.append(f"ROM boot banner seen    {'yes' if 'ESP-ROM:' in t else 'no'}")
    out.append(f"'waiting for download'  {'YES -- the board is in ROM download mode' if 'waiting for download' in t else 'no'}")
    if name == "gesture":
        out.append(f"state=1 rows            {len(re.findall(r'^[0-9]+,[0-9]+,[0-9]+,1,', t, re.M))}")
        out.append(f"'stroke N ended' marks  {len(re.findall(r'stroke [0-9]+ ended', t))}")
        dts = [int(m) for m in re.findall(r"^[0-9]+,[0-9]+,([0-9]+),1,", t, re.M)]
        if dts:
            out.append(f"state=1 rows dt_us>20000 {sum(d > 20000 for d in dts)} of {len(dts)}")
        # Per stroke, as the probe segments them: the state=1 rows before each
        # "stroke N ended" marker. Arithmetic only -- which strokes were the holds
        # is the operator's stroke table, not this.
        rows = []
        for line in t.splitlines():
            m = re.match(r"^([0-9]+),([0-9]+),([0-9]+),1,", line)
            if m:
                rows.append((int(m.group(2)), int(m.group(3))))
                continue
            m = re.search(r"stroke ([0-9]+) ended", line)
            if m:
                if rows:
                    out.append(f"stroke {int(m.group(1)):2}: {len(rows):5} rows, first->last "
                               f"{(rows[-1][0] - rows[0][0]) / 1000:9.1f} ms, max dt {max(d for _, d in rows) / 1000:7.1f} ms")
                else:
                    out.append(f"stroke {int(m.group(1)):2}: no state=1 rows")
                rows = []
    elif name == "screenstate":
        out.append(f"'# ---- script:' blocks {len(re.findall(r'^# ---- script:', t, re.M))}")
        out.append(f"'four scripts captured' {'yes' if 'four scripts captured' in t else 'no'}")
    elif name == "blit":
        for m in re.finditer(r"^\[blit\] (6\. backbuffer -> panel \(SPI\)|2\. blit 240x240 \(full-bleed\)).*$", t, re.M):
            out.append(m.group(0).strip())
        for m in re.finditer(r"^\[blit\] heap .*$", t, re.M):
            out.append(m.group(0).strip())
        out.append(f"'=== done.' line        {'yes' if '[blit] === done.' in t else 'no'}")
    else:
        for m in re.finditer(r"^REG,(arm|FATAL|WARNING),.*$", t, re.M):
            out.append(m.group(0).strip())
        out.append(f"HOLD,run_end lines      {len(re.findall(r'^HOLD,run_end,', t, re.M))}")
        last = re.findall(r"^HOLD,summary,.*$", t, re.M)
        out.extend(f"(last) {l}" for l in last[-3:])
    return out


def capture(port_obj, path, meta_path, name, manifest, reset, max_s=None, echo=True):
    """Read until Ctrl+C (or max_s, for the selftest). Returns (exit_code, bytes)."""
    buf = bytearray()
    boot_seen = False
    t0 = time.time()
    warned = False
    rc = 0
    with open(path, "wb") as f:
        if reset:
            # esptool's HardReset on USB-Serial-JTAG: RTS high (EN low) with DTR low,
            # then release. DTR low at release = a normal boot, never download mode.
            port_obj.dtr = False
            port_obj.rts = True
            time.sleep(0.1)
            port_obj.rts = False
        try:
            while True:
                if max_s is not None and time.time() - t0 > max_s:
                    break
                data = port_obj.read(4096)
                if data:
                    f.write(data)
                    f.flush()
                    buf += data
                    if echo:
                        sys.stdout.buffer.write(data)
                        sys.stdout.flush()
                    if not boot_seen and any(m in buf for m in BOOT_MARKS):
                        boot_seen = True
                        print("\n[d6-capture] boot captured -- the provenance (if this probe prints one) is in the file",
                              file=sys.stderr, flush=True)
                if not boot_seen and not warned and time.time() - t0 > 8:
                    warned = True
                    print("\n[d6-capture] WARNING: no boot seen in 8 s. The board was not reset by the open, so "
                          "this capture has no provenance block. Ctrl+C, then re-run with --reset.",
                          file=sys.stderr, flush=True)
        except KeyboardInterrupt:
            pass
        except Exception as e:  # serial.SerialException and friends: the port went away
            print(f"\n[d6-capture] PORT LOST: {type(e).__name__}: {e}. NOT reopening (a reopen into a boot "
                  f"window resets the board). The capture so far is kept.", file=sys.stderr, flush=True)
            rc = 3
    with open(meta_path, "a", encoding="utf-8", newline="\n") as m:
        m.write(f"date_utc_end   {utc():%Y-%m-%dT%H:%M:%SZ}\n")
        m.write(f"bytes          {len(buf)}\n")
        m.write(f"ended          {'PORT LOST' if rc == 3 else 'operator stop'}\n")
        m.write("---- facts from the capture (counts only; the runbook's criteria are the operator's) ----\n")
        for line in facts(bytes(buf), name, manifest):
            m.write(line + "\n")
    return rc, bytes(buf)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--board")
    ap.add_argument("--name", choices=NAMES)
    ap.add_argument("--image", help="the image dir that was flashed (its MANIFEST.json goes in the sidecar)")
    ap.add_argument("--out", default=os.path.join(REPO, "bench-logs"))
    ap.add_argument("--reset", action="store_true", help="pulse the USB-Serial-JTAG reset after opening")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.board or not a.name:
        ap.print_usage()
        return 2
    flash = _load(os.path.join(HERE, "d6-flash-image.py"), "d6flash")
    try:
        boards = flash.load_boards()
        flash.check_board(a.board, "", boards)
    except flash.Refused as e:
        print(f"[d6-capture] REFUSED: {e}")
        return 4
    manifest = None
    if a.image:
        with open(os.path.join(a.image, "MANIFEST.json"), encoding="utf-8") as f:
            manifest = json.load(f)
    bp = _load(os.path.join(HERE, "board-port.py"), "boardport")
    import serial  # pyserial, as board-port.py
    port, jtag = bp.find(a.board)
    if port is None:
        print(f"[d6-capture] {a.board} is not connected (no port carries its USB serial).")
        return 5
    if not jtag:
        print(f"[d6-capture] REFUSED: {a.board} is on {port} as a TinyUSB device -- a Missileer (TinyUSB) image is "
              f"running, not a D6 image (all of those are USB-Serial-JTAG). Did the flash take?")
        print("  (To watch a TinyUSB Missileer build instead: pio device monitor -p <port> --rts 0 --dtr 1.)")
        return 4
    os.makedirs(a.out, exist_ok=True)
    ext = "csv" if a.name == "gesture" else "log"
    start = utc()
    path = os.path.join(a.out, f"{a.name}-{start:%Y%m%d-%H%M}.{ext}")
    meta = path + ".meta.txt"
    with open(meta, "w", encoding="utf-8", newline="\n") as m:
        m.write("# D6 capture sidecar -- written by scripts/d6-capture.py, not by the device\n")
        m.write(f"capture        {os.path.basename(path)}\n")
        m.write(f"date_utc       {start:%Y-%m-%dT%H:%M:%SZ}\n")
        m.write(f"board          {a.board} {boards[a.board][0]} on {port} (USB-Serial-JTAG)\n")
        m.write(f"lines          DTR=0 RTS=0 set before open{'; then the USB-JTAG reset pulse (--reset)' if a.reset else ''}\n")
        if manifest:
            m.write(f"image          {manifest.get('station')}  env {manifest.get('env')}  "
                    f"extra flags '{manifest.get('extra_build_flags', '')}'\n")
            m.write(f"image commit   {manifest.get('commit')}\n")
            m.write(f"image sha256   {manifest['firmware']['sha256']}\n")
            m.write(f"image elf      {manifest.get('provenance_elf_sha256')} (first 8 bytes, as the provenance prints it)\n")
        else:
            m.write("image          (not given -- pass --image so this capture can be tied to a build)\n")
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr, s.rts = False, False          # BEFORE open: see the header
    print(f"[d6-capture] {a.board} on {port} -> {path}   (Ctrl+C to stop)", file=sys.stderr, flush=True)
    try:
        s.open()
    except serial.SerialException as e:
        print(f"[d6-capture] could not open {port}: {e}. Is another capture or monitor still holding it?")
        with open(meta, "a", encoding="utf-8", newline="\n") as m:
            m.write(f"ended          NEVER OPENED: {e}\n")
        return 5
    try:
        rc, data = capture(s, path, meta, a.name, manifest, a.reset)
    finally:
        try:
            s.close()
        except Exception:
            pass
    print(f"\n[d6-capture] wrote {path} ({len(data)} bytes) and {os.path.basename(meta)}", file=sys.stderr)
    for line in facts(data, a.name, manifest):
        print(f"  {line}", file=sys.stderr)
    return rc


def selftest():
    import tempfile
    try:
        import serial
    except ImportError:
        print("SELFTEST SKIPPED: pyserial is not installed")
        return 0
    bad = 0

    def check(n, ok, got=""):
        nonlocal bad
        bad += not ok
        print(f"  {'PASS' if ok else 'FAIL'}  {n}{(': ' + got) if got and not ok else ''}")

    boot = (b"ESP-ROM:esp32s3-20210327\r\nrst:0x15 (USB_UART_CHIP_RESET)\r\n\r\n"
            b"# ==== PROVENANCE ====\n# label        gesture-capture\n# elf_sha256   0123456789abcdef\n"
            b"# partition    app0 @ 0x020000\n# variant      S3_128\n# psram_total  8388608\n"
            b"seq,t_us,dt_us,state,x,y,r,theta_deci,band\n0,10,10,0,,,,,\n1,20,10,1,120,10,110,0,1\n"
            b"2,30,25000,1,121,10,110,4,1\n\n# --- stroke 1 ended ---\n")
    with tempfile.TemporaryDirectory() as d:
        p = serial.serial_for_url("loop://", timeout=0.1)
        p.write(boot)
        path, meta = os.path.join(d, "gesture-x.csv"), os.path.join(d, "gesture-x.csv.meta.txt")
        open(meta, "w").write("seed\n")
        rc, data = capture(p, path, meta, "gesture", {"provenance_elf_sha256": "0123456789abcdef"}, False,
                           max_s=1.0, echo=False)
        check("capture exits 0", rc == 0, str(rc))
        check("the file holds the device's bytes exactly", open(path, "rb").read() == boot)
        m = open(meta, encoding="utf-8").read()
        check("sidecar: elf_sha256 MATCHes the manifest", "MATCH" in m and "DIFFERENT" not in m, m)
        check("sidecar: stroke markers counted", "'stroke N ended' marks  1" in m, m)
        check("sidecar: slow-row count", "dt_us>20000 1 of 2" in m, m)
        f = facts(boot, "gesture", {"provenance_elf_sha256": "ffffffffffffffff"})
        check("a different image's hash reads DIFFERENT", any("DIFFERENT" in x for x in f), str(f))
        f = facts(b"[blit] heap baseline psram_free=0\n", "blit", None)
        check("blit: says the probe prints no provenance", any("prints none" in x for x in f), str(f))
        f = facts(b"waiting for download\r\n", "armA", None)
        check("download mode is called out", any("YES" in x for x in f), str(f))

        class Dies:
            dtr = rts = False

            def read(self, n):
                raise serial.SerialException("ClearCommError failed")
        rc, _ = capture(Dies(), os.path.join(d, "a.log"), os.path.join(d, "a.meta"), "armA", None, False,
                        max_s=1.0, echo=False)
        check("a lost port ends the capture with exit 3 (no reopen)", rc == 3, str(rc))
        check("  and the sidecar says so", "PORT LOST" in open(os.path.join(d, "a.meta")).read())

        class Rec:
            def __init__(self):
                self.log = []
                self._dtr = self._rts = None

            @property
            def dtr(self): return self._dtr

            @dtr.setter
            def dtr(self, v): self.log.append(("dtr", v)); self._dtr = v

            @property
            def rts(self): return self._rts

            @rts.setter
            def rts(self, v): self.log.append(("rts", v)); self._rts = v

            def read(self, n):
                time.sleep(0.05)
                return b""
        r = Rec()
        capture(r, os.path.join(d, "b.log"), os.path.join(d, "b.meta"), "blit", None, True, max_s=0.3, echo=False)
        check("--reset drives DTR low, then RTS high -> low (esptool's USB-JTAG hard reset)",
              r.log == [("dtr", False), ("rts", True), ("rts", False)], str(r.log))
    print("SELFTEST " + ("PASSED" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
