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
firmware built with ARDUINO_USB_MODE=1, e.g. the Blipscope soak build) gives it with
colons; the TinyUSB stack (ARDUINO_USB_MODE=0, the Missileer builds) gives it without.
Both are matched after removing the colons.

FOUR BOARDS, TWO OF EACH PRODUCT (Daniel, 2026-10-12). Two Blipscope units and two
Missileer units sit on the bench, so a product name is not an identity either: COM16
was taken for the bench Missileer that day and was E7:94, the photo unit. The
name below is what a command says; the serial is what it is checked against.

Usage:
    python scripts/board-port.py missileer            # print its current port
    python scripts/board-port.py missileer --upload   # the port esptool can flash:
                                                      # a TinyUSB board is first
                                                      # 1200-baud-touched into ROM
                                                      # download mode
    python scripts/board-port.py missileer --label    # its short label (no USB, no registry)
    python scripts/board-port.py missileer --host     # its mDNS name, for a tool to hold, not to log
    python scripts/board-port.py --list               # every board, where it is, who holds it

THE SERIALS ARE NOT IN THE REPO (docs/weatherscope-stage3.md A12.140). A MAC plus the public salt gives a device
id, so the repo names a board by its label, the MAC's last two bytes, and the serials live in one local file: the
path in BLIPSCOPE_BOARDS_FILE, else ~/.config/blipscope/boards, beside the token file. One name=SERIAL per line;
blank lines and # comments are ignored. Nothing here prints a serial. A missing or malformed file, a board absent
from it, a serial registered twice, or a path inside a git working tree is refused with exit 4.

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

Exit: 0 found / ok, 1 not connected, 2 bad usage, 3 reserved by someone else, 4 the registry cannot answer.
"""
import json
import os
import re
import socket
import sys
import tempfile
import time
from datetime import datetime, timezone

ESPRESSIF_VID = 0x303A

# THE BENCH BOARDS. Keep in step with CLAUDE.md "Bench boards" and with
# scripts/bench-flash.ps1 (its -Board list and the rules it enforces).
BOARDS = {
    # name: (label, the MAC's last two bytes; product; role; rule). The serial is the registry's.
    'missileer':       ('E9:D8', 'Missileer', 'bench',
                        'flash freely (Missileer work)'),
    'missileer-photo': ('E7:94', 'Missileer', 'photo unit',
                        'Missileer envs only; flash on Daniel\'s go (product photos)'),
    'weatherscope':    ('6E:64', 'Weatherscope', 'bench',
                        'the Weatherscope session\'s board (Daniel, 2026-10-20): never for Blipscope or Missileer work; ask Daniel first'),
    'blipscope-bench': ('E2:08', 'Blipscope', 'bench',
                        'the Blipscope session\'s board: never for Missileer work; ask Daniel first'),
}
# Two names on one label would let a rule written for one board reach the other.
assert len({b[0] for b in BOARDS.values()}) == len(BOARDS), 'a label is registered twice'


class NoRegistry(Exception):
    """Exit 4. The message names the path and a line number, never a line's content."""


def registry_path():
    return (os.environ.get('BLIPSCOPE_BOARDS_FILE')
            or os.path.join(os.path.expanduser('~'), '.config', 'blipscope', 'boards'))


def inside_git_tree(path):
    d = os.path.dirname(os.path.abspath(path))
    while True:
        if os.path.exists(os.path.join(d, '.git')):
            return True
        up = os.path.dirname(d)
        if up == d:
            return False
        d = up


def read_registry():
    """{name: serial}, from the local file. Refuses rather than guessing, and never prints a value."""
    p = registry_path()
    if inside_git_tree(p):
        raise NoRegistry('the board registry %s is inside a git working tree; it must live outside every repo' % p)
    try:
        text = open(p, encoding='ascii').read()
    except FileNotFoundError:
        raise NoRegistry('the board registry %s is missing. Create it, one name=SERIAL per line, or set '
                         'BLIPSCOPE_BOARDS_FILE (CLAUDE.md, "Bench boards").' % p)
    except (OSError, UnicodeDecodeError) as e:
        raise NoRegistry('the board registry %s cannot be read (%s)' % (p, type(e).__name__))
    if os.name == 'posix' and os.stat(p).st_mode & 0o077:
        raise NoRegistry('the board registry %s can be read by others: chmod 600 it' % p)
    out = {}
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        name, sep, serial = line.partition('=')
        if not sep or not re.fullmatch('[0-9A-F]{12}', serial):
            raise NoRegistry('the board registry %s: line %d is malformed' % (p, n))
        out[name] = serial
    if len(set(out.values())) != len(out):
        raise NoRegistry('the board registry %s: a serial is registered twice' % p)
    return out


def serial_of(name):
    reg = read_registry()
    if name not in reg:
        raise NoRegistry('the board registry %s has no entry for %s' % (registry_path(), name))
    return reg[name]


def host_of(name):
    return '%s-%s.local' % (BOARDS[name][1].lower(), serial_of(name)[6:].lower())


def norm(serial):
    return (serial or '').replace(':', '').upper()


def find(name):
    """(port, on_usb_serial_jtag) or (None, None)."""
    want = serial_of(name)                 # first: a refusal touches no USB
    from serial.tools import list_ports   # lazily: reservations work without pyserial
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
    r = {'board': name, 'label': BOARDS[name][0], 'by': by, 'for': why,
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
    # The registry (A12.140): made-up serials, built at run time, in a scratch directory. No USB is touched: each
    # refusal is raised before pyserial is imported.
    saved = os.environ.get('BLIPSCOPE_BOARDS_FILE')
    made = ['7F' * 5 + '%02X' % i for i in range(len(BOARDS))]
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'boards')
        os.environ['BLIPSCOPE_BOARDS_FILE'] = p
        ok(main(['weatherscope', '--label']) == 0, 'CONTROL: --label needs no registry')
        ok(main(['weatherscope']) == 4, 'a missing registry: exit 4, and no USB touched')
        ok(main(['--list']) == 4, '--list with no registry: exit 4')
        with open(p, 'w', encoding='ascii') as f:
            f.write('# a comment\n\n' + ''.join('%s=%s\n' % kv for kv in zip(BOARDS, made)))
        ok(read_registry() == dict(zip(BOARDS, made)), 'the registry reads back, comments skipped')
        ok(serial_of('weatherscope') == made[list(BOARDS).index('weatherscope')], 'serial_of finds its own board')
        ok(host_of('weatherscope') == 'weatherscope-' + made[list(BOARDS).index('weatherscope')][6:].lower() + '.local',
           'host_of composes the name the firmware gives itself')
        with open(p, 'w', encoding='ascii') as f:
            f.write('weatherscope=%s\n' % made[0])
        ok(main(['missileer', '--host']) == 4, 'a board absent from the registry: exit 4')
        with open(p, 'w', encoding='ascii') as f:
            f.write('a=%s\nb=%s\n' % (made[0], made[0]))
        ok(main(['weatherscope', '--host']) == 4, 'a serial registered twice: exit 4')
        with open(p, 'w', encoding='ascii') as f:
            f.write('weatherscope=not-a-serial\n')
        try:
            read_registry()
            said = ''
        except NoRegistry as e:
            said = str(e)
        ok('line 1' in said and 'not-a-serial' not in said, 'a malformed line is named by number, never by content')
        os.mkdir(os.path.join(d, '.git'))
        ok(main(['weatherscope', '--host']) == 4, 'a registry inside a git working tree: exit 4')
    if saved is None:
        os.environ.pop('BLIPSCOPE_BOARDS_FILE', None)
    else:
        os.environ['BLIPSCOPE_BOARDS_FILE'] = saved
    print('SELFTEST PASSED' if not bad else 'SELFTEST FAILED (%d)' % bad)
    return 1 if bad else 0


def main(argv):
    try:
        return run(argv)
    except NoRegistry as e:
        print('board-port: %s' % e, file=sys.stderr)
        return 4


def run(argv):
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
        for name, (label, product, role, rule) in BOARDS.items():
            port, jtag = find(name)
            where = f'{port} ({"USB-Serial-JTAG" if jtag else "TinyUSB"})' if port else 'not connected'
            cur = read_res(name)
            print(f'{name:16s} {label}  {product:9s} {role:10s}  {where:28s} {rule}')
            print(f'{"":16s} ' + ('reserved: ' + describe(cur) if cur else 'unreserved'))
        return 0
    names = [a for a in argv if not a.startswith('--')]
    if len(names) != 1 or names[0] not in BOARDS:
        print(f'usage: board-port.py <{"|".join(BOARDS)}> [--upload | --label | --host] | --list', file=sys.stderr)
        return 2
    name = names[0]
    if '--label' in argv:
        # The table's answer, no USB and no registry: printed beside the port it was found on,
        # so a flash log names the board without its MAC.
        print(BOARDS[name][0])
        return 0
    if '--host' in argv:
        print(host_of(name))
        return 0
    port = for_upload(name) if '--upload' in argv else find(name)[0]
    if port is None:
        print(f'{name} ({BOARDS[name][0]}) is not connected', file=sys.stderr)
        return 1
    print(port)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
