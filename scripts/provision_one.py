#!/usr/bin/env python3
"""provision_one.py -- give ONE board its key, and exit 0 only when that is proven.

    DEVICE_KEY_SECRET=... python scripts/provision_one.py <port> <mac> --env blipscope-s3-128 \
        --verify-url https://scopes.valarsystems.com

The factory image must already be on the board (valar-flasher's bench mode writes
it; provision-batch.py writes it). This does the per-board step that follows, and
provision-batch.py calls the SAME function, `provision()`, so there is one copy:

    mac -> deviceId -> deviceKey -> NVS image
      -> write it at the nvs offset from the env's partition table
      -> PROVE it is on the board: `verify-flash` of that same image. The chip
         hashes the region and returns an MD5 -- the check write-flash already runs
         after every stub write -- NOT a read_flash, whose stub bulk-read dies on
         this board's USB-JTAG (INCOMING-INSPECTION.md).
      -> present the key to --verify-url: 200 proves DEVICE_KEY_SECRET here matches
         the Worker's
      -> append the manufacturing record, read back

"Verified" means all of that. It is what DONE is allowed to claim, and exit 0 is
reached by no other path: a skipped write, a board that did not take it, a
rejected key and an unwritable record each exit non-zero with one line saying which.

The MAC comes from the caller (it already read it to decide whether to skip the
board); it is validated, never trusted blindly. DEVICE_KEY_SECRET comes from the
environment only -- never an argument, never a file -- and is never printed.

Last line of output is machine-readable for a caller:
    RESULT OK <device_id>        |   RESULT FAIL <reason>
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("provision_device", Path(__file__).parent / "provision-device.py")
pd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pd)

MAC_RE = re.compile(r"^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$")


def verify(base: str, key: str, dev_id: str) -> int:
    """HTTP status from presenting this key to the proxy. 200 = accepted.

    The User-Agent is NOT decoration: Cloudflare's edge 403s the default
    `Python-urllib/3.x` before the Worker ever sees the request, which looks
    exactly like a rejected key and sent me chasing DEVICE_KEY_SECRET."""
    req = urllib.request.Request(
        base.rstrip("/") + "/v1/config",
        headers={"X-Blip-Key": key, "X-Blip-Device": dev_id, "X-Blip-Model": "s3-128",
                 "User-Agent": "Blipscope-Provisioner/1"})
    try:
        return urllib.request.urlopen(req, timeout=30).status
    except Exception as e:
        return getattr(e, "code", 0)


def provision(port: str, mac: str, *, esptool_cmd: list, dashed: bool, baud: int, salt: str,
              secret: str, nvs_offset: str, nvs_size: int, cloud_url: str | None = None,
              verify_url: str | None = None, build_nvs=None, verify_fn=None) -> tuple:
    """(status, dev_id, detail) with status "OK" or "FAIL". Never raises: a board
    that fails must not take a batch down with it. `build_nvs` and `verify_fn` are
    injectable so the pipeline can be tested without PlatformIO or the network."""
    build_nvs = build_nvs or pd.build_nvs
    verify_fn = verify_fn or verify
    mac = (mac or "").strip().lower().replace("-", ":")
    if not MAC_RE.match(mac):
        return "FAIL", "", "not a MAC address"
    try:
        dev_id = pd.device_id(mac, salt)
        key = pd.device_key(secret, dev_id)
        write = "write-flash" if dashed else "write_flash"
        check = "verify-flash" if dashed else "verify_flash"
        with tempfile.TemporaryDirectory() as td:
            nvs_bin = str(build_nvs(key, cloud_url, Path(td), nvs_size))
            for step, sub in (("nvs write", write), ("nvs verify", check)):
                cmd = esptool_cmd + ["--port", port, "--baud", str(baud), sub, nvs_offset, nvs_bin]
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
                if r.returncode != 0:
                    tail = (r.stdout + r.stderr).strip().splitlines()[-3:]
                    what = ("write failed" if step == "nvs write"
                            else "key is NOT on the board (verify-flash failed)")
                    return "FAIL", dev_id, f"{mac}: nvs {what} -- " + " / ".join(tail)
        if verify_url:
            code = verify_fn(verify_url, key, dev_id)
            if code == 401:
                # THE one that means the batch is bad: DEVICE_KEY_SECRET here does
                # not match the Worker's, so every key in this run is worthless.
                return "FAIL", dev_id, f"{mac}: key REJECTED (401) -- DEVICE_KEY_SECRET does not match the Worker's"
            if code != 200:
                # Anything else is the check failing, not the key. Don't send the
                # operator hunting for a secret mismatch that isn't there.
                return "FAIL", dev_id, f"{mac}: verify inconclusive (HTTP {code}) -- board is flashed; key not confirmed"
        return "OK", dev_id, f"{mac} -> {dev_id}"
    except Exception as e:
        return "FAIL", "", f"{mac}: {type(e).__name__}: {e}"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Provision one board (factory image already on it).")
    ap.add_argument("port")
    ap.add_argument("mac")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. blipscope-s3-128 (salt + NVS geometry)")
    ap.add_argument("--verify-url", required=True, help="check the minted key against this proxy base")
    ap.add_argument("--cloud-url", help="also bake this into NVS as cloud-url")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--log", default=str(REPO / "provisioned.csv"), help="manufacturing record to append")
    a = ap.parse_args(argv)

    def result(ok: bool, text: str) -> int:
        print(f"RESULT {'OK' if ok else 'FAIL'} {text}", flush=True)
        return 0 if ok else 1

    secret = os.environ.get("DEVICE_KEY_SECRET", "").strip()
    if not secret:
        return result(False, "DEVICE_KEY_SECRET is not set in this environment")
    log = Path(a.log)
    try:
        pd.log_preflight(log)
        salt = pd.salt_from_sources(a.env)
        nvs_offset, nvs_size = pd.nvs_geometry(a.env)
        esptool_cmd, dashed = pd.find_esptool()
    except SystemExit:
        return result(False, "setup failed (see the line above)")
    status, dev_id, detail = provision(a.port, a.mac, esptool_cmd=esptool_cmd, dashed=dashed, baud=a.baud,
                                      salt=salt, secret=secret, nvs_offset=nvs_offset, nvs_size=nvs_size,
                                      cloud_url=a.cloud_url, verify_url=a.verify_url)
    if status != "OK":
        return result(False, detail)
    try:
        pd.append_log(log, a.env, a.mac.strip().lower().replace("-", ":"), dev_id)
    except SystemExit:
        return result(False, f"{dev_id}: key is on the board and verified, but the record did not land")
    return result(True, dev_id)


if __name__ == "__main__":
    sys.exit(main())
