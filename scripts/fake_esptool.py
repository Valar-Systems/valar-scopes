#!/usr/bin/env python3
"""A fake esptool for testing the provisioning pipeline without a board.

It keeps a SIMULATED FLASH (a file of 0xFF) so verify-flash compares against what
was really written, and logs every call, one JSON line per invocation.

    FAKE_ESPTOOL_DIR   where flash.bin and calls.jsonl live (required)
    FAKE_MAC           what read-mac reports (default 00:00:00:00:00:01)
    FAKE_FAIL          a subcommand that exits 2 (e.g. write-flash)
    FAKE_DROP_WRITES   "1" = writes exit 0 but store nothing: a board that did not take it

Accepts both spellings (write_flash / write-flash), like esptool 4 and 5.
"""
import json
import os
import sys

FLASH_SIZE = 0x40000


def main(argv):
    d = os.environ["FAKE_ESPTOOL_DIR"]
    flash_path, log_path = os.path.join(d, "flash.bin"), os.path.join(d, "calls.jsonl")
    if not os.path.exists(flash_path):
        with open(flash_path, "wb") as f:
            f.write(b"\xff" * FLASH_SIZE)
    args = list(argv)
    port = None
    while args and args[0].startswith("--"):
        opt = args.pop(0)
        if opt in ("--port", "--baud", "--chip", "--before", "--after"):
            val = args.pop(0)
            if opt == "--port":
                port = val
    sub = (args.pop(0) if args else "").replace("_", "-")
    with open(log_path, "a", encoding="utf-8") as f:
        f.write(json.dumps({"sub": sub, "port": port, "args": args}) + "\n")
    if os.environ.get("FAKE_FAIL", "").replace("_", "-") == sub:
        print(f"A fatal error occurred: fake failure of {sub}")
        return 2
    if sub == "read-mac":
        print(f"MAC: {os.environ.get('FAKE_MAC', '00:00:00:00:00:01')}")
        return 0
    if sub in ("write-flash", "verify-flash"):
        flash = bytearray(open(flash_path, "rb").read())
        for off_s, path in zip(args[0::2], args[1::2]):
            off, data = int(off_s, 0), open(path, "rb").read()
            if sub == "write-flash":
                if os.environ.get("FAKE_DROP_WRITES") != "1":
                    flash[off:off + len(data)] = data
            elif bytes(flash[off:off + len(data)]) != data:
                print(f"Verification of {path} at {off_s} FAILED: digest mismatch")
                return 1
        if sub == "write-flash":
            with open(flash_path, "wb") as f:
                f.write(flash)
            print("Hash of data verified.")
        else:
            print("-- verify OK (digest matched)")
        return 0
    print(f"fake esptool: unsupported subcommand {sub!r}")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
