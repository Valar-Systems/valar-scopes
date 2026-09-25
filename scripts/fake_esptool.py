#!/usr/bin/env python3
"""A fake esptool for testing the provisioning pipeline without a board.

It keeps a SIMULATED FLASH (a file of 0xFF) and logs every call, one JSON line per
invocation. It models the three things about the real board and the real esptool
that the provisioning flow depends on -- the first version modelled none of them,
and Step 4 (2026-09-25) failed on the board for exactly that reason:

  1. THE FIRMWARE BOOTS AFTER A HARD RESET AND WRITES NVS. `--after hard-reset` (the
     default) boots the app, which writes its own entries (Wi-Fi AP settings, PHY
     calibration) into the NVS partition. A byte compare of NVS taken after that
     boot fails even when the key is present. Modelled with FAKE_BOOT_WRITES_NVS=1.
  2. write-flash VERIFIES ON-CHIP, BEFORE ANY RESET: it prints "Hash of data verified."
     when the flash MD5 matches, and exits non-zero with "MD5 of file does not match
     data in flash!" when it does not (esptool cmds.py:1552-1572).
  3. THAT CHECK CAN BE SKIPPED SILENTLY: esptool swallows NotImplementedInROMError and
     prints nothing (cmds.py:1573). Modelled with FAKE_NO_HASH=1: exit 0, no line.

    FAKE_ESPTOOL_DIR       where flash.bin, calls.jsonl, macs.txt live (required)
    FAKE_MAC               what read-mac reports (default 02:00:00:00:00:01)
    FAKE_MAC_SEQUENCE      comma-separated: successive read-mac calls report these
    FAKE_FAIL              a subcommand that exits 2 (e.g. write-flash)
    FAKE_DROP_WRITES       "1" = the board did not take the write (hash mismatch)
    FAKE_NO_HASH           "1" = write-flash skips its hash check silently
    FAKE_BOOT_WRITES_NVS   "1" = a hard reset boots firmware that writes into NVS

Accepts both spellings (write_flash / write-flash, hard_reset / hard-reset).
"""
import json
import os
import sys

FLASH_SIZE = 0x40000
NVS_OFF, NVS_SIZE = 0x9000, 0x15000


def main(argv):
    d = os.environ["FAKE_ESPTOOL_DIR"]
    flash_path, log_path = os.path.join(d, "flash.bin"), os.path.join(d, "calls.jsonl")
    if not os.path.exists(flash_path):
        with open(flash_path, "wb") as f:
            f.write(b"\xff" * FLASH_SIZE)
    args = list(argv)
    port, after = None, "hard-reset"
    while args and args[0].startswith("--"):
        opt = args.pop(0)
        if opt in ("--port", "--baud", "--chip", "--before", "--after"):
            val = args.pop(0)
            if opt == "--port":
                port = val
            if opt == "--after":
                after = val.replace("_", "-")
    sub = (args.pop(0) if args else "").replace("_", "-")
    rec = {"sub": sub, "port": port, "after": after, "args": args}
    if sub == "write-flash":
        # What was WRITTEN, recorded at write time -- so a test can check the image the
        # on-chip hash verified, instead of reading NVS back after the firmware booted.
        rec["written_head"] = [open(p, "rb").read()[:96].hex() for p in args[1::2]]
    with open(log_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(rec) + "\n")
    rc = run(sub, args, flash_path, d)
    # The reset happens AFTER the operation, as in esptool. A hard reset boots the app.
    if rc == 0 and after == "hard-reset" and os.environ.get("FAKE_BOOT_WRITES_NVS") == "1":
        flash = bytearray(open(flash_path, "rb").read())
        flash[NVS_OFF + 0x20:NVS_OFF + 0x24] = b"\xaa\xaa\xaa\xfa"       # a page-state word flips
        flash[NVS_OFF + 0x3000:NVS_OFF + 0x3010] = b"nvs.net80211-ap\0"   # the AP settings land
        with open(flash_path, "wb") as f:
            f.write(flash)
    return rc


def run(sub, args, flash_path, d):
    if os.environ.get("FAKE_FAIL", "").replace("_", "-") == sub:
        print(f"A fatal error occurred: fake failure of {sub}")
        return 2
    if sub == "read-mac":
        seq = [m for m in os.environ.get("FAKE_MAC_SEQUENCE", "").split(",") if m]
        if seq:
            n_path = os.path.join(d, "macs.txt")
            n = int(open(n_path).read()) if os.path.exists(n_path) else 0
            open(n_path, "w").write(str(n + 1))
            mac = seq[min(n, len(seq) - 1)]
        else:
            mac = os.environ.get("FAKE_MAC", "02:00:00:00:00:01")
        print(f"MAC: {mac}")
        return 0
    if sub in ("write-flash", "verify-flash"):
        flash = bytearray(open(flash_path, "rb").read())
        for off_s, path in zip(args[0::2], args[1::2]):
            off, data = int(off_s, 0), open(path, "rb").read()
            if sub == "write-flash":
                if os.environ.get("FAKE_DROP_WRITES") != "1":
                    flash[off:off + len(data)] = data
                if os.environ.get("FAKE_NO_HASH") == "1":
                    continue                              # esptool's silent skip
                if bytes(flash[off:off + len(data)]) != data:
                    print("MD5 of file does not match data in flash!")
                    print("A fatal error occurred: MD5 of file does not match data in flash!")
                    return 2
                print("Hash of data verified.")
            elif bytes(flash[off:off + len(data)]) != data:
                print("Verification failed (digest mismatch).")
                print("A fatal error occurred: Verification failed.")
                return 2
        if sub == "write-flash":
            with open(flash_path, "wb") as f:
                f.write(flash)
        else:
            print("Verification successful (digest matched).")
        return 0
    print(f"fake esptool: unsupported subcommand {sub!r}")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
