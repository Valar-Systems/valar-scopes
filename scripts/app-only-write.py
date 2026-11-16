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

    app-only-write.py --port COMx --build-dir .pio/build/<env> [--before default-reset|no-reset] [--check-only]
    app-only-write.py --check-tool
    app-only-write.py --selftest
    app-only-write.py --sabotage
--check-only stops after the checks: the two reads, nothing written, the chip left in the ROM (an esptool read-mac
with a hard reset brings it back). --check-tool runs the tool guard on the configured esptool and asks it its version.
--selftest runs a full write and a --check-only run against a fake esptool and checks every argv they build and every
guard refusal; --sabotage plants faults into copies of this file and each must turn its own rows red.
Exit: 0 written and verified (with --check-only: checked), 7 refused (nothing written), 8 write failed, 6 refused
because the esptool is not the recorded one (nothing run), 2 usage (--sabotage: 2 when its anchor fails).
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

# The esptool this script runs is a PRIVATE copy, never the shared ~/.platformio one (the orchestrator's ruling,
# 2026-11-16: the shared folder moved twice in one day, each time because another project built). It is a venv on
# Python 3.13.7 at ~/.valar-bench/esptool-5.2.0 (override: env VALAR_ESPTOOL_DIR), holding the PyPI sdist's esptool/
# module copied verbatim (sdist sha256 9c355b7d6331cc92979cc710ae5c41f59830d1ea29ec24c467c6005a092c06d6) and its 17
# dependency wheels installed with --require-hashes --no-deps. It is run as `python -I -m esptool`.
TOOL_DIR = os.environ.get('VALAR_ESPTOOL_DIR') or os.path.join(os.path.expanduser('~'), '.valar-bench', 'esptool-5.2.0')
# What the install recorded, and what esptool() checks before EVERY call (any mismatch: exit 6, nothing run). Changing
# any of these is a ruling, and needs its own proof read on the bench.
ESPTOOL_VERSION = '5.2.0'
ESPTOOL_TREE_SHA = '84c878702a70f290937b06a7c1bd49dbc50d2350f52454061b90d12b7d2c667e'  # tree_sha(site-packages)
ESPTOOL_PY_VERSION = '3.13.7 (tags/v3.13.7:bcee1c3, Aug 14 2025, 14:15:11) [MSC v.1944 64 bit (AMD64)]'
PT_OFFSET, PT_SIZE = 0x8000, 0xC00
COMMANDS = ('read-flash', 'write-flash', 'read-mac')  # the esptool commands this script runs (the selftest reads them)
# The flasher stub, PINNED on every call (the orchestrator's ruling, 2026-11-16). esptool 5.3.0 runs the new stub
# (esp-flasher-stub) by default and 5.2.0 the legacy one (v1.11.1), and nothing in esptool's output says which ran. The
# shared ~/.platformio went from 5.3.0 to 5.2.0 on 2026-11-16, and the otadata read below then stalled on the bench board
# after its first 4 KiB block, twice; the same read completed under the new stub. THIS PINS THE WORKING PATH, NOT A
# PROVEN CAUSE: the new stub also stays resident between the two reads, where the legacy one exits to the ROM and is
# uploaded again, and which of those two differences matters is not known. The pin covers both. --stub-version is a
# hidden esptool option outside its versioning policy: an esptool without it fails every call loudly ("No such
# option"), never silently. Every call goes through esptool_argv, and the selftest checks every argv a run builds.
STUB_VERSION = '2'
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


def tool_python():
    return os.path.join(TOOL_DIR, 'venv', 'Scripts', 'python.exe')


def tool_site():
    return os.path.join(TOOL_DIR, 'venv', 'Lib', 'site-packages')


def tree_sha(root):
    """sha256 over a tree: files in os.walk order with sorted directories (__pycache__ excluded) and sorted names, each
    fed as its relative posix path + NUL + the sha256 of its bytes. The install recorded ESPTOOL_TREE_SHA this way."""
    h = hashlib.sha256()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d != '__pycache__')
        for fn in sorted(filenames):
            p = os.path.join(dirpath, fn)
            with open(p, 'rb') as f:
                data = f.read()
            h.update(os.path.relpath(p, root).replace(os.sep, '/').encode('utf-8') + b'\0' + hashlib.sha256(data).digest())
    return h.hexdigest()


def esptool_version():
    """__version__ of the private copy's esptool module; 'absent' when its python or the module is missing."""
    init = os.path.join(tool_site(), 'esptool', '__init__.py')
    if not (os.path.isfile(tool_python()) and os.path.isfile(init)):
        return 'absent'
    with open(init, encoding='utf-8', errors='replace') as f:
        m = re.search(r'''^__version__\s*=\s*["']([^"']+)["']''', f.read(), re.M)
    return m.group(1) if m else 'unreadable'


def interpreter_version():
    """sys.version as the private copy's own python reports it ('' when it cannot be run)."""
    try:
        r = subprocess.run([tool_python(), '-I', '-c', 'import sys; print(sys.version)'], capture_output=True, text=True,
                           timeout=60)
    except (OSError, subprocess.SubprocessError):
        return ''
    return r.stdout.strip() if r.returncode == 0 else ''


def esptool_argv(port, before, after, *args):
    """The ONE place an esptool argv is built: esptool() runs it, and the selftest checks every one a run builds."""
    return [tool_python(), '-I', '-m', 'esptool', '--chip', 'esp32s3', '--port', port, '--before', before, '--after', after, '--stub-version', STUB_VERSION, *args]


def refuse(why):
    print(f'REFUSED: {why} (tool {TOOL_DIR}); nothing run')
    raise SystemExit(6)


def esptool(port, before, after, *args):
    # THE GUARD, before EVERY call: the recorded private copy, or nothing runs.
    v = esptool_version()
    if v != ESPTOOL_VERSION:
        refuse(f'its esptool is {v}, not the recorded {ESPTOOL_VERSION}')
    s = tree_sha(tool_site())
    if s != ESPTOOL_TREE_SHA:
        refuse(f'its site-packages tree is {s}, not the recorded {ESPTOOL_TREE_SHA}')
    i = interpreter_version()
    if i != ESPTOOL_PY_VERSION:
        refuse(f'its interpreter reports {i!r}, not the recorded {ESPTOOL_PY_VERSION!r}')
    r = subprocess.run(esptool_argv(port, before, after, *args), capture_output=True, text=True)
    return r.returncode, (r.stdout or '') + (r.stderr or '')


def read_flash(port, before, off, size, path):
    rc, out = esptool(port, before, 'no-reset', 'read-flash', hex(off), hex(size), path)
    if rc != 0 or not os.path.exists(path):
        raise SystemExit(f'REFUSED: could not read {hex(off)}+{hex(size)} from the board (rc {rc}):\n{out}')
    with open(path, 'rb') as f:
        return f.read()


def run(port, build, before, check_only=False):
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
    if check_only:
        print(f'checks ok: table matches ({len(want)} entries), board boots ota_0. --check-only: nothing written; '
              'the chip is left in the ROM.')
        return 0
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

    # A1-A4: run() itself, a full write and a --check-only run, against a fake subprocess.run that records every argv.
    def word(cmd):
        return next((i for i, a in enumerate(cmd) if a in COMMANDS), None)

    def pinned(cmd):
        w = word(cmd)
        i = next((i for i, a in enumerate(cmd) if a == '--stub-version'), None)
        return w is not None and i is not None and i < w and i + 1 < len(cmd) and cmd[i + 1] == '2'

    def capture(check_only, version='5.2.0', with_python=True, tamper=False, interp=None):
        """run() against the fake, with TOOL_DIR pointed at a STAND-IN private copy in the venv layout: python.exe
        (unless with_python is False) and a site-packages holding esptool/__init__.py (the given __version__) and
        esptool/loader.py. ESPTOOL_TREE_SHA is set to the stand-in's own tree sha, and THEN, with tamper, one byte of
        loader.py changes. The fake answers the interpreter probe with ESPTOOL_PY_VERSION, or with interp; the probe is
        not an esptool call and is not recorded."""
        global TOOL_DIR, ESPTOOL_TREE_SHA
        calls = []

        def fake(cmd, **kw):
            if '-c' in cmd:  # the interpreter probe
                return subprocess.CompletedProcess(cmd, 0, (interp or ESPTOOL_PY_VERSION) + '\n', '')
            calls.append(list(cmd))
            out, w = '', word(cmd)
            if w is not None and cmd[w] == 'read-flash':
                off, size, path = int(cmd[-3], 16), int(cmd[-2], 16), cmd[-1]
                with open(path, 'wb') as f:
                    f.write(table(rows) if off == PT_OFFSET else b'\xff' * size)
            elif w is not None and cmd[w] == 'write-flash':
                out = 'Wrote 256 bytes at 0x00020000\nHash of data verified.\n'
            return subprocess.CompletedProcess(cmd, 0, out, '')

        real = (subprocess.run, TOOL_DIR, ESPTOOL_TREE_SHA)
        with tempfile.TemporaryDirectory() as b:
            with open(os.path.join(b, 'partitions.bin'), 'wb') as f:
                f.write(table(rows))
            with open(os.path.join(b, 'firmware.bin'), 'wb') as f:
                f.write(b'\xe9' + b'\0' * 255)
            tool = os.path.join(b, 'tool')
            site = os.path.join(tool, 'venv', 'Lib', 'site-packages')
            os.makedirs(os.path.join(site, 'esptool'))
            os.makedirs(os.path.join(tool, 'venv', 'Scripts'))
            with open(os.path.join(site, 'esptool', '__init__.py'), 'w', encoding='utf-8', newline='\n') as f:
                f.write(f'__version__ = "{version}"\n')
            with open(os.path.join(site, 'esptool', 'loader.py'), 'wb') as f:
                f.write(b'# stand-in\n')
            if with_python:
                open(os.path.join(tool, 'venv', 'Scripts', 'python.exe'), 'wb').close()
            subprocess.run, TOOL_DIR = fake, tool
            ESPTOOL_TREE_SHA = tree_sha(site)
            if tamper:
                with open(os.path.join(site, 'esptool', 'loader.py'), 'wb') as f:
                    f.write(b'# stand-iN\n')
            try:
                rc = run('COMX', b, 'default-reset', check_only)
            except SystemExit as e:  # a refusal is its exit code
                rc = ('exit', e.code)
            except BaseException as e:  # a run that raises is a failed row, not a crashed selftest
                rc = f'raised {type(e).__name__}'
            finally:
                subprocess.run, TOOL_DIR, ESPTOOL_TREE_SHA = real
        return rc, calls, [c[word(c)] if word(c) is not None else None for c in calls]

    rc, calls, words = capture(False)
    ok(rc == 0 and bool(calls) and all(pinned(c) for c in calls),
       "A1 every call of a full run carries '--stub-version' '2', ahead of the command word")
    ok(words == ['read-flash', 'read-flash', 'write-flash', 'read-mac'],
       'A2 CONTROL: a full run makes exactly read-flash, read-flash, write-flash, read-mac')
    rc, calls, words = capture(True)
    ok(rc == 0 and words == ['read-flash', 'read-flash'], 'A3 check-only: exactly read-flash, read-flash, and no write-flash')
    ok(rc == 0 and bool(calls) and all(pinned(c) for c in calls),
       "A4 every call of a check-only run carries '--stub-version' '2', ahead of the command word")
    # A5-A8: the tool guard. Anything but the recorded private copy is refused (exit 6) before a single esptool call.
    rc, calls, words = capture(False, version='5.3.0')
    ok(rc == ('exit', 6) and calls == [], 'A5 a stand-in whose esptool says 5.3.0: refused (exit 6) before any call')
    rc, calls, words = capture(False, with_python=False)
    ok(rc == ('exit', 6) and calls == [], 'A6 a stand-in with no python.exe (absent): refused (exit 6) before any call')
    rc, calls, words = capture(False)
    ok(rc == 0 and words == ['read-flash', 'read-flash', 'write-flash', 'read-mac'],
       'A7c CONTROL: the same stand-in unmodified passes the guard and makes the full run')
    rc, calls, words = capture(False, tamper=True)
    ok(rc == ('exit', 6) and calls == [], 'A7 one byte of the stand-in changed after its sha was recorded: refused (exit 6)')
    rc, calls, words = capture(False, interp='3.14.6 (another interpreter)')
    ok(rc == ('exit', 6) and calls == [], 'A8 the interpreter reports another sys.version: refused (exit 6) before any call')
    print('SELFTEST PASSED' if not bad else f'SELFTEST FAILED ({bad})')
    return 1 if bad else 0


# Each plant goes into a temp copy of this file, and the copy's --selftest must turn EXACTLY these rows red. The needles
# are split across a '+' so that this table never matches itself (each must appear exactly once in the source).
SABOTAGE = [
    ('S-NOPIN', "'--after', after, '--stub-" + "version', STUB_VERSION, *args]", "'--after', after, *args]", {'A1', 'A4'}),
    ('S-PIN1', "STUB_VERSION" + " = '2'", "STUB_VERSION = '1'", {'A1', 'A4'}),
    ('S-CHECKONLY', "    if check" + "_only:\n", "    if False:\n", {'A3'}),
    ('S-NOVERSION', "    if v != ESPTOOL" + "_VERSION:\n", "    if False:\n", {'A5', 'A6'}),
    ('S-NOSHA', "    if s != ESPTOOL" + "_TREE_SHA:\n", "    if False:\n", {'A7'}),
    ('S-NOINTERP', "    if i != ESPTOOL" + "_PY_VERSION:\n", "    if False:\n", {'A8'}),
]


def sabotage():
    with open(os.path.abspath(__file__), encoding='utf-8') as f:
        src = f.read()

    def red_rows(text):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'app-only-write.py')
            with open(p, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            r = subprocess.run([sys.executable, p, '--selftest'], capture_output=True, text=True)
        rows = {l.split()[1] for l in r.stdout.splitlines() if l.strip().startswith('FAIL') and len(l.split()) > 1}
        return r.returncode, rows

    rc, rows = red_rows(src)
    if rc != 0 or rows:
        print(f'SABOTAGE RIG: the unplanted copy does not pass its selftest (exit {rc}, red {sorted(rows)}); no plant run')
        return 2
    print('  ok    anchor: the unplanted copy passes its selftest')
    bad = 0
    for name, needle, repl, want in SABOTAGE:
        n = src.count(needle)
        planted = src.replace(needle, repl)
        if n != 1 or planted == src:
            print(f'  FAIL  {name}: the needle appears {n} times (must be once); not planted')
            bad += 1
            continue
        rc, rows = red_rows(planted)
        good = rc != 0 and rows == want
        print(f"  {'ok  ' if good else 'FAIL'}  {name}: red {sorted(rows)}, want {sorted(want)}, exit {rc}")
        bad += not good
    print(f'SABOTAGE PASSED ({len(SABOTAGE)} of {len(SABOTAGE)})' if not bad else f'SABOTAGE FAILED ({bad})')
    return 1 if bad else 0


def check_tool():
    """The guard on the configured private copy, then `version` through esptool() (the same path every real call takes).
    Exit 0 only when it prints ESPTOOL_VERSION; a guard refusal exits 6 from inside esptool()."""
    print(f'tool {TOOL_DIR}: esptool {esptool_version()}, tree {tree_sha(tool_site())} (recorded {ESPTOOL_TREE_SHA})')
    rc, out = esptool('NONE', 'no-reset', 'no-reset', 'version')
    print(out.strip())
    good = rc == 0 and ESPTOOL_VERSION in out
    print('CHECK-TOOL PASSED' if good else f'CHECK-TOOL FAILED (rc {rc})')
    return 0 if good else 1


def main(argv):
    if '--check-tool' in argv:
        return check_tool()
    if '--selftest' in argv:
        return selftest()
    if '--sabotage' in argv:
        return sabotage()
    try:
        port = argv[argv.index('--port') + 1]
        build = argv[argv.index('--build-dir') + 1]
    except (ValueError, IndexError):
        print(__doc__)
        return 2
    before = argv[argv.index('--before') + 1] if '--before' in argv else 'default-reset'
    return run(port, build, before, '--check-only' in argv)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
