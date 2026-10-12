#!/usr/bin/env python3
"""Resolve a bench board's COM port from its USB serial number -- never from a COM number.

WHY (2026-09-24). The Missileer board was COM15, COM4 and COM17 in one day: COM15
running the TinyUSB CDC+HID build, COM4 in ROM download mode, COM17 running a build
with the HID interface compiled out. Windows numbers a port per USB *interface
layout*, so the same board gets a new COM number whenever its firmware changes
shape. A rule written against COM numbers ("COM15 = Missileer, flash freely") is
therefore a rule that eventually flashes the wrong board.

The ESP32-S3 reports its factory MAC as the USB serial number in every mode, but
spelled two ways: the chip's USB-Serial-JTAG peripheral (ROM download mode, and any
firmware built with ARDUINO_USB_MODE=1, e.g. the Blipscope soak build) gives
"90:70:69:31:E9:D8"; the TinyUSB stack (ARDUINO_USB_MODE=0 -- the Missileer builds until
2026-10-12, when they moved to the built-in USB-Serial/JTAG; no env uses it now)
gives "90706931E9D8". Both are matched after removing the colons.

FOUR BOARDS, TWO OF EACH PRODUCT (Daniel, 2026-10-12). Two Blipscope units and two
Missileer units sit on the bench, so a product name is not an identity either: COM16
was taken for the bench Missileer that day and was 90706931E794, the photo unit. The
name below is what a command says; the serial is what it is checked against.

Usage:
    python scripts/board-port.py missileer            # print its current port
    python scripts/board-port.py missileer --upload   # the port esptool can flash:
                                                      # a TinyUSB board is first
                                                      # 1200-baud-touched into ROM
                                                      # download mode
    python scripts/board-port.py missileer --serial   # the registered serial (no USB)
    python scripts/board-port.py --list               # every board, where it is, who holds it

RESERVATIONS (Daniel, 2026-10-12). Several sessions share these boards, and a held
port is only visible while something has it open. So a session that takes a board
says so in a small file -- who, since when, for what -- that every checkout and every
session on this machine can read, and bench-flash.ps1 checks it before flashing:

    python scripts/board-port.py --reserve <board> --by <who> --for "<what>" [--pid N]
    python scripts/board-port.py --release <board> --by <who>
    python scripts/board-port.py --check   <board> --by <who>   # 0 free or yours, 3 someone else's
    python scripts/board-port.py --who     <board>
    python scripts/board-port.py --selftest

One JSON file per board in %USERPROFILE%/.valar-bench/reservations (override with
VALAR_BENCH_RESERVATIONS). Created with O_EXCL, so two sessions reserving at once
cannot both win. Only the holder releases; a claim that looks abandoned is reported,
never broken -- a person deletes the file. A reservation is a claim, not a lock on the
port: bench-flash.ps1 still refuses a port that something holds open.

Exit: 0 found / ok, 1 not connected, 2 bad usage, 3 reserved by someone else.
"""
import json
import os
import socket
import sys
import tempfile
import time
from datetime import datetime, timezone

ESPRESSIF_VID = 0x303A

# THE BENCH BOARDS. Keep in step with CLAUDE.md "Bench boards" and with
# scripts/bench-flash.ps1 (its -Board list and the rules it enforces).
BOARDS = {
    # name: (MAC as the USB serial, colons removed, upper case; product; role; rule)
    'missileer':       ('90706931E9D8', 'Missileer', 'bench',
                        'flash freely (Missileer work)'),
    'missileer-photo': ('90706931E794', 'Missileer', 'photo unit',
                        'Missileer envs only; flash on Daniel\'s go (product photos)'),
    'blipscope':       ('907069326E64', 'Blipscope', 'soak',
                        'SOAK BOARD: never for Missileer work; ask Daniel first'),
    'blipscope-bench': ('90706931E208', 'Blipscope', 'bench',
                        'the Blipscope session\'s board: never for Missileer work; ask Daniel first'),
}
# Two names on one serial would let a rule written for one board reach the other.
assert len({b[0] for b in BOARDS.values()}) == len(BOARDS), 'a serial is registered twice'


def norm(serial):
    return (serial or '').replace(':', '').upper()


def find(name):
    """(port, on_usb_serial_jtag) or (None, None)."""
    from serial.tools import list_ports   # lazily: reservations work without pyserial
    want = BOARDS[name][0]
    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID and norm(p.serial_number) == want:
            # The USB-Serial-JTAG peripheral spells the MAC with colons.
            return p.device, ':' in (p.serial_number or '')
    return None, None


def for_upload(name, timeout_s=15.0):
    port, jtag = find(name)
    if port is None or jtag:
        return port  # USB-Serial-JTAG: esptool's own reset reaches the ROM
    # TinyUSB CDC: esptool's DTR/RTS reset does not reach the ROM from here (it
    # reads "No serial data received"); the 1200-baud touch does.
    from serial import Serial
    try:
        s = Serial(port, 1200)
        s.dtr = False
        time.sleep(0.2)
        s.close()
    except Exception:
        pass  # the port vanishing mid-close is the reset working
    end = time.time() + timeout_s
    while time.time() < end:
        time.sleep(0.5)
        p2, jtag2 = find(name)
        if p2 and jtag2:
            return p2
    return None


# ---- reservations ----------------------------------------------------------------
def res_dir():
    return (os.environ.get('VALAR_BENCH_RESERVATIONS')
            or os.path.join(os.path.expanduser('~'), '.valar-bench', 'reservations'))


def res_path(name):
    return os.path.join(res_dir(), name + '.json')


def read_res(name):
    try:
        with open(res_path(name), encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as e:
        # An unreadable claim is still a claim: never treat it as free.
        return {'by': '<unreadable reservation: %s>' % e, 'since': '?', 'for': '?'}


def describe(r):
    pid = ', pid %s' % r['pid'] if r.get('pid') else ''
    return '%s since %s for "%s" (host %s%s)' % (r.get('by'), r.get('since'), r.get('for'),
                                                 r.get('host', '?'), pid)


def reserve(name, by, why, pid=None):
    """0 reserved (or already yours), 3 someone else holds it."""
    os.makedirs(res_dir(), exist_ok=True)
    r = {'board': name, 'serial': BOARDS[name][0], 'by': by, 'for': why,
         'since': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
         'host': socket.gethostname()}
    if pid:
        r['pid'] = pid
    try:
        fd = os.open(res_path(name), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        cur = read_res(name)
        if cur and cur.get('by') == by:
            print('%s: already yours (%s)' % (name, describe(cur)))
            return 0
        print('%s: RESERVED by %s' % (name, describe(cur or {})), file=sys.stderr)
        return 3
    with os.fdopen(fd, 'w', encoding='utf-8') as f:
        json.dump(r, f, indent=1)
    print('%s: reserved by %s' % (name, describe(r)))
    return 0


def release(name, by):
    cur = read_res(name)
    if cur is None:
        print('%s: not reserved' % name)
        return 0
    if cur.get('by') != by:
        print('%s: REFUSING to release: held by %s. Only the holder releases it; if it is '
              'abandoned, a person deletes %s' % (name, describe(cur), res_path(name)), file=sys.stderr)
        return 3
    os.remove(res_path(name))
    print('%s: released by %s' % (name, by))
    return 0


def check(name, by):
    cur = read_res(name)
    if cur is None:
        print('%s: free' % name)
        return 0
    if cur.get('by') == by:
        print('%s: yours (%s)' % (name, describe(cur)))
        return 0
    print('%s: RESERVED by %s' % (name, describe(cur)))  # stdout: the answer to --check, not an error
    return 3


def opt(argv, flag):
    if flag in argv:
        i = argv.index(flag)
        if i + 1 < len(argv) and not argv[i + 1].startswith('--'):
            return argv[i + 1]
    return None


def selftest():
    """Reservation rules against a scratch directory. Touches no USB and no real claim."""
    bad = 0

    def ok(cond, what):
        nonlocal bad
        print('  %s  %s' % ('ok  ' if cond else 'FAIL', what))
        bad += not cond

    saved = os.environ.get('VALAR_BENCH_RESERVATIONS')
    with tempfile.TemporaryDirectory() as d:
        os.environ['VALAR_BENCH_RESERVATIONS'] = d
        b = 'blipscope-bench'
        ok(check(b, 'alice') == 0, 'a board nobody reserved is free')
        ok(reserve(b, 'alice', 'captures') == 0, 'alice reserves it')
        ok(read_res(b) is not None and read_res(b)['by'] == 'alice', 'the claim is on disk, naming alice')
        ok(reserve(b, 'bob', 'soak') == 3, "bob cannot reserve alice's board")
        ok(read_res(b)['for'] == 'captures', "bob's attempt left alice's claim unchanged")
        ok(check(b, 'bob') == 3 and check(b, 'alice') == 0, 'check: 3 for bob, 0 for alice')
        ok(reserve(b, 'alice', 'more captures') == 0, 'reserving your own board again is fine')
        ok(release(b, 'bob') == 3 and read_res(b) is not None, 'bob cannot release it')
        ok(release(b, 'alice') == 0 and read_res(b) is None, 'alice releases it')
        ok(release(b, 'alice') == 0, 'releasing a free board is a no-op')
        with open(res_path(b), 'w') as f:
            f.write('{not json')
        ok(check(b, 'alice') == 3, 'CONTROL: an unreadable claim is still a claim, not free')
        os.remove(res_path(b))
    if saved is None:
        os.environ.pop('VALAR_BENCH_RESERVATIONS', None)
    else:
        os.environ['VALAR_BENCH_RESERVATIONS'] = saved
    print('SELFTEST PASSED' if not bad else 'SELFTEST FAILED (%d)' % bad)
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    for flag in ('--reserve', '--release', '--check', '--who'):
        if flag not in argv:
            continue
        name = opt(argv, flag)
        if name not in BOARDS:
            print('usage: board-port.py %s <%s> --by <who>' % (flag, '|'.join(BOARDS)), file=sys.stderr)
            return 2
        if flag == '--who':
            cur = read_res(name)
            print('%s: %s' % (name, describe(cur) if cur else 'free'))
            return 0
        by = opt(argv, '--by')
        if not by:
            print('%s needs --by <who>' % flag, file=sys.stderr)
            return 2
        if flag == '--reserve':
            why = opt(argv, '--for')
            if not why:
                print('--reserve needs --for "<what>"', file=sys.stderr)
                return 2
            pid = opt(argv, '--pid')
            return reserve(name, by, why, int(pid) if pid and pid.isdigit() else None)
        return release(name, by) if flag == '--release' else check(name, by)
    if '--list' in argv:
        for name, (mac, product, role, rule) in BOARDS.items():
            port, jtag = find(name)
            where = f'{port} ({"USB-Serial-JTAG" if jtag else "TinyUSB"})' if port else 'not connected'
            cur = read_res(name)
            print(f'{name:16s} {mac}  {product:9s} {role:10s}  {where:28s} {rule}')
            print(f'{"":16s} ' + ('reserved: ' + describe(cur) if cur else 'unreserved'))
        return 0
    names = [a for a in argv if not a.startswith('--')]
    if len(names) != 1 or names[0] not in BOARDS:
        print(f'usage: board-port.py <{"|".join(BOARDS)}> [--upload | --serial] | --list', file=sys.stderr)
        return 2
    name = names[0]
    if '--serial' in argv:
        # The table's answer, no USB involved: what the board MUST report, printed
        # beside the port it was found on so a flash log names the board by its ID.
        print(BOARDS[name][0])
        return 0
    port = for_upload(name) if '--upload' in argv else find(name)[0]
    if port is None:
        print(f'{name} ({BOARDS[name][0]}) is not connected', file=sys.stderr)
        return 1
    print(port)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
