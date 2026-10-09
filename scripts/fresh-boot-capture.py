"""The fresh-boot acceptance's serial capture: pyserial, ONE open, never a reopen mid-run.

Ruling 2026-10-08. Twice that day the capture's reopen after the step-5 power cut reset the
board it was measuring (rst:0x15, then reset reason=USB). Its wait was timed from
SerialPort.GetPortNames(), which kept listing COM18 for more than 15 s while the board was
unplugged: a port list is NOT a board-present signal. So this capture opens once, and when the
port drops (the power cut) it writes a marker and EXITS. Steps 5 and 6 are graded over the
network and the boot count at the Worker (fresh-boot-acceptance.sh --grade-cut / --grade-off /
--grade-boots), none of which needs the serial port.

  capture <port> <log>          one open; append lines; on a drop, write the marker and exit
  collect <port> <log> <secs>   AFTER every check is graded: one open, read <secs>, and record
                                whether the open reset the board (a rst:0x15 in what it read).
                                A reset here is recorded and changes no result.
  reset <port> <log>            --sabotage-extra-reset: pulse RTS (esptool's USB hard reset) so
                                the boot check has a real extra boot to catch

dtr and rts are set False BEFORE open in every mode: pyserial applies them as part of the open,
so the open itself never asserts either line.
"""
import datetime
import sys
import time

import serial


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S")


def open_port(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    return s


def main():
    mode, port, log = sys.argv[1], sys.argv[2], sys.argv[3]
    with open(log, "a", encoding="utf-8", buffering=1) as f:
        if mode == "capture":
            try:
                s = open_port(port)
            except Exception as e:
                f.write(f"{stamp()} [capture] could not open {port} ({type(e).__name__})\n")
                return 3
            f.write(f"{stamp()} [capture] attached to {port} (pyserial, one open)\n")
            try:
                while True:
                    b = s.readline()
                    if b:
                        f.write(f"{stamp()} {b.decode('utf-8', 'replace').rstrip()}\n")
            except Exception as e:
                f.write(f"{stamp()} [capture] port dropped ({type(e).__name__}) -- NOT reopening: "
                        "steps 5-6 are graded over the network\n")
            finally:
                try:
                    s.close()
                except Exception:
                    pass
            return 0
        if mode == "collect":
            secs = float(sys.argv[4])
            f.write(f"{stamp()} [collect] opening {port} after grading; a reset here changes no result\n")
            try:
                s = open_port(port)
            except Exception as e:
                f.write(f"{stamp()} [collect] could not open {port} ({type(e).__name__})\n")
                return 3
            reset, end = False, time.monotonic() + secs
            try:
                while time.monotonic() < end:
                    b = s.readline()
                    if b:
                        line = b.decode("utf-8", "replace").rstrip()
                        reset = reset or "rst:0x15" in line
                        f.write(f"{stamp()} {line}\n")
            except Exception as e:
                f.write(f"{stamp()} [collect] port dropped ({type(e).__name__})\n")
            finally:
                try:
                    s.close()
                except Exception:
                    pass
            f.write(f"{stamp()} [collect] the reopen reset the board: {'YES (rst:0x15)' if reset else 'no'}\n")
            return 0
        if mode == "reset":
            s = open_port(port)
            s.rts = True
            time.sleep(0.2)
            s.rts = False
            time.sleep(0.2)
            s.close()
            f.write(f"{stamp()} [sabotage] pulsed RTS on {port}: an extra reset the boot check must catch\n")
            return 0
    print(f"unknown mode {mode}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
