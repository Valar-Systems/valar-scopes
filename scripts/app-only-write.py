#!/usr/bin/env python3
"""Write ONLY the app image of a build to the board's ota_0 slot. Called by
bench-flash.ps1 -AppOnly, after its board rules, reservation and port checks.

"App image only; never erase" (Daniel, 2026-10-12). `pio run -t upload` also
rewrites the bootloader, the partition table and otadata -- the same layout, NVS
untouched, but not "the app only". This writes one region, and first proves it is
the right one:

  1. the board's partition table (read back from 0x8000) must equal the build's
     partitions.bin entry for entry -- otherwise ota_0 is not where the build thinks;
  2. the board's otadata must select ota_0 (or be blank, which boots ota_0) --
     otherwise the bootloader would ignore what we write;
  3. the image must be an ESP app image (magic 0xE9) that fits ota_0.
Then: write-flash ota_0 with --flash-mode/freq/size keep and NO reset; success
only if esptool says "Hash of data verified."; one retry while the chip is still
in the ROM; reset only after a verified write. Nothing is erased; NVS, otadata,
the bootloader and the partition table are never written.

    app-only-write.py --port COMx --build-dir .pio/build/<env> [--before default-reset|no-reset]
    app-only-write.py --selftest
Exit: 0 written and verified, 7 refused (nothing written), 8 write failed, 2 usage.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

PIO = os.path.join(os.path.expanduser('~'), '.platformio')
PY = os.path.join(PIO, 'penv', 'Scripts', 'python.exe')
ESPTOOL = os.path.join(PIO, 'packages', 'tool-esptoolpy', 'esptool.py')
PT_OFFSET, PT_SIZE = 0x8000, 0xC00
T_APP, T_DATA, ST_OTA0, ST_OTADATA = 0x00, 0x01, 0x10, 0x00


def parse_table(blob):
    """[(label, type, subtype, offset, size)] from a binary partition table."""
    out = []
    for i in range(0, len(blob) - 31, 32):
        e = blob[i:i + 32]
        if e[:2] == b'\xaa\x50':
            typ, sub, off, size = struct.unpack_from('<BBII', e, 2)
            out.append((e[12:28].split(b'\0')[0].decode('ascii', 'replace'), typ, sub, off, size))
        elif e[:2] in (b'\xeb\xeb', b'\xff\xff'):
            break
        else:
            return []
    return out


def selected_slot(otadata, n_ota=2):
    """ota slot the bootloader will boot: highest valid sequence; blank -> 0."""
    best = None
    for base in (0, 0x1000):
        seq = struct.unpack_from('<I', otadata, base)[0]
        crc = struct.unpack_from('<I', otadata, base + 28)[0]
        if seq == 0xFFFFFFFF:
            continue
        if zlib.crc32(struct.pack('<I', seq), 0xFFFFFFFF) != crc:
            continue
        if best is None or seq > best:
            best = seq
    return 0 if best is None else (best - 1) % n_ota


def esptool(port, before, after, *args):
    cmd = [PY, ESPTOOL, '--chip', 'esp32s3', '--port', port, '--before', before, '--after', after, *args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, (r.stdout or '') + (r.stderr or '')


def read_flash(port, before, off, size, path):
    rc, out = esptool(port, before, 'no-reset', 'read-flash', hex(off), hex(size), path)
    if rc != 0 or not os.path.exists(path):
        raise SystemExit(f'REFUSED: could not read {hex(off)}+{hex(size)} from the board (rc {rc}):\n{out}')
    with open(path, 'rb') as f:
        return f.read()


def run(port, build, before):
    img_path = os.path.join(build, 'firmware.bin')
    with open(os.path.join(build, 'partitions.bin'), 'rb') as f:
        want = parse_table(f.read())
    with open(img_path, 'rb') as f:
        img = f.read()
    ota0 = [p for p in want if p[1] == T_APP and p[2] == ST_OTA0]
    otad = [p for p in want if p[1] == T_DATA and p[2] == ST_OTADATA]
    n_ota = len([p for p in want if p[1] == T_APP and 0x10 <= p[2] <= 0x1F]) or 1
    if not want or len(ota0) != 1 or len(otad) != 1:
        print('REFUSED: the build has no single ota_0 + otadata in partitions.bin')
        return 7
    _, _, _, app_off, app_size = ota0[0]
    if not img or img[0] != 0xE9 or len(img) > app_size:
        print(f'REFUSED: {img_path} is not an app image that fits ota_0 ({len(img)} B, slot {app_size} B)')
        return 7
    with tempfile.TemporaryDirectory() as d:
        board_pt = read_flash(port, before, PT_OFFSET, PT_SIZE, os.path.join(d, 'pt.bin'))
        have = parse_table(board_pt)
        if have != want:
            print('REFUSED: the board\'s partition table differs from the build\'s -- ota_0 would land wrong.')
            print('  board:', have)
            print('  build:', want)
            return 7
        od = read_flash(port, 'no-reset', otad[0][3], otad[0][4], os.path.join(d, 'otadata.bin'))
        slot = selected_slot(od, n_ota)
        if slot != 0:
            print(f'REFUSED: the board boots ota_{slot}; writing ota_0 would be ignored.')
            return 7
    print(f'checks ok: table matches ({len(want)} entries), board boots ota_0, image {len(img)} B '
          f'fits ota_0 @ {hex(app_off)} ({app_size} B). Writing ota_0 only.')
    for attempt in (1, 2):
        rc, out = esptool(port, 'no-reset', 'no-reset', '--baud', '921600', 'write-flash',
                          '--flash-mode', 'keep', '--flash-freq', 'keep', '--flash-size', 'keep',
                          hex(app_off), img_path)
        ok = rc == 0 and 'Hash of data verified' in out
        written = [l for l in out.splitlines() if l.startswith('Wrote ')]
        print(f'write attempt {attempt}: rc {rc}, {"verified" if ok else "NOT verified"}; ' +
              ('; '.join(written) or 'no Wrote line'))
        if ok:
            rc2, out2 = esptool(port, 'no-reset', 'hard-reset', 'read-mac')
            print('reset:', 'ok' if rc2 == 0 else f'rc {rc2} -- reset the board by hand if it does not come back')
            return 0
    print('WRITE FAILED twice; the chip is still in the ROM (not reset). Re-run, or flash with BOOT held.')
    print(out)
    return 8


def selftest():
    bad = 0

    def ok(cond, what):
        nonlocal bad
        print(f"  {'ok  ' if cond else 'FAIL'}  {what}")
        bad += not cond

    def table(rows):
        return b''.join(b'\xaa\x50' + struct.pack('<BBII', t, s, o, z) + l.encode().ljust(16, b'\0') + b'\0' * 4
                        for l, t, s, o, z in rows).ljust(PT_SIZE, b'\xff')

    def entry(seq):
        e = struct.pack('<I', seq) + b'\xff' * 24
        return e + struct.pack('<I', zlib.crc32(struct.pack('<I', seq), 0xFFFFFFFF))

    rows = [('nvs', 1, 2, 0x9000, 0x15000), ('otadata', 1, 0, 0x1E000, 0x2000),
            ('app0', 0, 0x10, 0x20000, 0x640000), ('app1', 0, 0x11, 0x660000, 0x640000)]
    ok(parse_table(table(rows)) == rows, 'the bignvs table parses back to its rows')
    ok(parse_table(b'\x00' * 64) == [], 'CONTROL: garbage is not a table')
    blank = b'\xff' * 0x2000
    ok(selected_slot(blank) == 0, 'blank otadata boots ota_0')
    ok(selected_slot(entry(1).ljust(0x1000, b'\xff') + b'\xff' * 0x1000) == 0, 'seq 1 boots ota_0')
    ok(selected_slot(entry(1).ljust(0x1000, b'\xff') + entry(2).ljust(0x1000, b'\xff')) == 1, 'seq 2 boots ota_1')
    bad_crc = (struct.pack('<I', 2) + b'\xff' * 28).ljust(0x1000, b'\xff') + b'\xff' * 0x1000
    ok(selected_slot(bad_crc) == 0, 'CONTROL: a seq with a bad crc is ignored')
    print('SELFTEST PASSED' if not bad else f'SELFTEST FAILED ({bad})')
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    try:
        port = argv[argv.index('--port') + 1]
        build = argv[argv.index('--build-dir') + 1]
    except (ValueError, IndexError):
        print(__doc__)
        return 2
    before = argv[argv.index('--before') + 1] if '--before' in argv else 'default-reset'
    return run(port, build, before)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
