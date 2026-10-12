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
"90:70:69:31:E9:D8"; the TinyUSB stack (ARDUINO_USB_MODE=0, the Missileer builds)
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
    python scripts/board-port.py --list               # every board, where it is

Exit: 0 found, 1 not connected, 2 bad usage.
"""
import sys
import time

from serial import Serial
from serial.tools import list_ports

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


def main(argv):
    if '--list' in argv:
        for name, (mac, product, role, rule) in BOARDS.items():
            port, jtag = find(name)
            where = f'{port} ({"USB-Serial-JTAG" if jtag else "TinyUSB"})' if port else 'not connected'
            print(f'{name:16s} {mac}  {product:9s} {role:10s}  {where:28s} {rule}')
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
