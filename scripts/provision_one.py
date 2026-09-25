#!/usr/bin/env python3
"""provision_one.py -- give ONE board its key, and exit 0 only when that is proven.

    python scripts/provision_one.py <port> <mac> --env blipscope-s3-128 \
        --verify-url https://scopes.valarsystems.com

THE KEY IS MINTED BY THE WORKER, NOT DERIVED HERE (docs/provisioning-mint.md).
Nobody on the bench holds DEVICE_KEY_SECRET -- it is Worker-only by design, and
this script never reads it. It presents the bench's PROVISION_TOKEN (read from a
file, never an argument, never printed) and the board's MAC to
POST /blipscope/provision, and gets back that board's id and key.

The factory image must already be on the board (valar-flasher's bench mode writes
it; provision-batch.py writes it). This does the per-board step that follows, and
provision-batch.py calls the SAME function, `provision()`, so there is one copy:

    mac -> mint (Worker) -> deviceId + key
      -> the id is RECOMPUTED here from the MAC and the firmware's salt; a mismatch
         means the Worker and the firmware disagree about the salt, so the key
         would belong to an id this board never reports -- refused, nothing written
      -> NVS image -> write it at the nvs offset from the env's partition table
      -> PROVE it is on the board: `verify-flash` of that same image (an on-chip
         MD5, NOT a read_flash, whose stub bulk-read dies on this board's USB-JTAG;
         INCOMING-INSPECTION.md)
      -> present the key to --verify-url: 200 proves the Worker accepts it
      -> append the manufacturing record, read back

"Verified" means all of that. It is what DONE is allowed to claim, and exit 0 is
reached by no other path.

Output a caller can read: `STEP write` then `STEP verify` as the board moves
through them, and a last line of
    RESULT OK <device_id>        |   RESULT FAIL <reason>
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("provision_device", Path(__file__).parent / "provision-device.py")
pd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pd)

MAC_RE = re.compile(r"^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$")
# The route, on the verify base. proxy/scripts/smoke-prod.sh greps THIS line to probe
# production with the bench's own path -- keep the shape `MINT_PATH = "..."`.
MINT_PATH = "/blipscope/provision"
# The same file valar-flasher's bench mode checks for; the flasher passes it here
# explicitly with --token-file so the two can never read different paths.
DEFAULT_TOKEN_FILE = Path(os.path.expanduser("~")) / ".config" / "valar-flasher" / "provision-token"
USER_AGENT = "Blipscope-Provisioner/2"


class MintError(Exception):
    pass


def read_token(path) -> str | None:
    try:
        t = Path(path).read_text(encoding="utf-8").strip()
        return t or None
    except OSError:
        return None


def mint(base: str, token: str, mac: str, urlopen=None) -> tuple:
    """(deviceId, key) from the Worker, or MintError with one line saying why.

    The User-Agent is NOT decoration: Cloudflare's edge 403s the default
    `Python-urllib/3.x` before the Worker sees the request, which would read as a
    rejected token."""
    urlopen = urlopen or urllib.request.urlopen
    req = urllib.request.Request(
        base.rstrip("/") + MINT_PATH, data=json.dumps({"mac": mac}).encode(), method="POST",
        headers={"Content-Type": "application/json", "X-Provision-Token": token, "User-Agent": USER_AGENT})
    try:
        with urlopen(req, timeout=30) as r:
            body = json.loads(r.read().decode())
            status = r.status
    except urllib.error.HTTPError as e:
        status = e.code
        try:
            body = json.loads(e.read().decode())
        except Exception:
            body = {}
    except Exception as e:
        raise MintError(f"mint unreachable ({type(e).__name__}) -- nothing written")
    if status == 200 and re.fullmatch(r"[0-9a-f]{16}", str(body.get("deviceId", ""))) \
            and re.fullmatch(r"[0-9a-f]{64}", str(body.get("key", ""))):
        return body["deviceId"], body["key"]
    err = body.get("error", "")
    if status == 403 and err == "forbidden":
        raise MintError("PROVISION_TOKEN rejected (403) -- the token file does not match the Worker's")
    if status == 429:
        raise MintError(f"daily mint cap ({body.get('cap', '?')}) reached; resets {body.get('resetsAt', '?')}")
    if status == 403 and err in ("revoked", "fake_device_id"):
        raise MintError(f"the Worker refuses this board: {err}")
    raise MintError(f"mint failed (HTTP {status} {err or 'no error field'}) -- nothing written")


def verify(base: str, key: str, dev_id: str) -> int:
    """HTTP status from presenting this key to the proxy. 200 = accepted."""
    req = urllib.request.Request(
        base.rstrip("/") + "/v1/config",
        headers={"X-Blip-Key": key, "X-Blip-Device": dev_id, "X-Blip-Model": "s3-128", "User-Agent": USER_AGENT})
    try:
        return urllib.request.urlopen(req, timeout=30).status
    except Exception as e:
        return getattr(e, "code", 0)


def provision(port: str, mac: str, *, esptool_cmd: list, dashed: bool, baud: int, salt: str,
              mint_fn, nvs_offset: str, nvs_size: int, cloud_url: str | None = None,
              verify_url: str | None = None, build_nvs=None, verify_fn=None, on_step=None) -> tuple:
    """(status, dev_id, detail) with status "OK" or "FAIL". Never raises: a board
    that fails must not take a batch down with it. `mint_fn(mac) -> (deviceId, key)`
    is the Worker call; `build_nvs` and `verify_fn` are injectable so the pipeline
    can be tested without PlatformIO or the network."""
    build_nvs = build_nvs or pd.build_nvs
    verify_fn = verify_fn or verify
    on_step = on_step or (lambda step: None)
    mac = (mac or "").strip().lower().replace("-", ":")
    if not MAC_RE.match(mac):
        return "FAIL", "", "not a MAC address"
    try:
        local_id = pd.device_id(mac, salt)
        try:
            dev_id, key = mint_fn(mac)
        except MintError as e:
            return "FAIL", local_id, f"{mac}: {e}"
        if dev_id != local_id:
            # The Worker derived the id with a different salt than this firmware
            # compiles in. Its key would belong to an id the board never reports.
            return "FAIL", local_id, (f"{mac}: the Worker's device id differs from this firmware's -- "
                                      f"salt drift between proxy/src/devicesalt.generated.ts and "
                                      f"include/DeviceIdentity.h; nothing written")
        write = "write-flash" if dashed else "write_flash"
        check = "verify-flash" if dashed else "verify_flash"
        with tempfile.TemporaryDirectory() as td:
            nvs_bin = str(build_nvs(key, cloud_url, Path(td), nvs_size))
            for step, sub in (("nvs write", write), ("nvs verify", check)):
                on_step("write" if step == "nvs write" else "verify")
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
                # The Worker minted this key a moment ago, so a 401 means the
                # verify base is not the Worker that minted it (a different env).
                return "FAIL", dev_id, f"{mac}: key REJECTED (401) -- the verify URL is not the Worker that minted it"
            if code != 200:
                return "FAIL", dev_id, f"{mac}: verify inconclusive (HTTP {code}) -- board is flashed; key not confirmed"
        return "OK", dev_id, f"{mac} -> {dev_id}"
    except Exception as e:
        return "FAIL", "", f"{mac}: {type(e).__name__}: {e}"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Provision one board (factory image already on it).")
    ap.add_argument("port")
    ap.add_argument("mac")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. blipscope-s3-128 (salt + NVS geometry)")
    ap.add_argument("--verify-url", required=True, help="the Worker to mint from and verify against")
    ap.add_argument("--token-file", default=str(DEFAULT_TOKEN_FILE), help="the bench's PROVISION_TOKEN (file)")
    ap.add_argument("--cloud-url", help="also bake this into NVS as cloud-url")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--log", default=str(REPO / "provisioned.csv"), help="manufacturing record to append")
    a = ap.parse_args(argv)

    def result(ok: bool, text: str) -> int:
        print(f"RESULT {'OK' if ok else 'FAIL'} {text}", flush=True)
        return 0 if ok else 1

    token = read_token(a.token_file)
    print(f"provision token present: {token is not None}", flush=True)   # a boolean, never the value
    if not token:
        return result(False, f"no provisioning token at {a.token_file} -- set it once from the password manager")
    log = Path(a.log)
    try:
        pd.log_preflight(log)
        salt = pd.salt_from_sources(a.env)
        nvs_offset, nvs_size = pd.nvs_geometry(a.env)
        esptool_cmd, dashed = pd.find_esptool()
    except SystemExit:
        return result(False, "setup failed (see the line above)")
    status, dev_id, detail = provision(a.port, a.mac, esptool_cmd=esptool_cmd, dashed=dashed, baud=a.baud,
                                      salt=salt, mint_fn=lambda m: mint(a.verify_url, token, m),
                                      nvs_offset=nvs_offset, nvs_size=nvs_size,
                                      cloud_url=a.cloud_url, verify_url=a.verify_url,
                                      on_step=lambda step: print(f"STEP {step}", flush=True))
    if status != "OK":
        return result(False, detail)
    try:
        pd.append_log(log, a.env, a.mac.strip().lower().replace("-", ":"), dev_id)
    except SystemExit:
        return result(False, f"{dev_id}: key is on the board and verified, but the record did not land")
    return result(True, dev_id)


if __name__ == "__main__":
    sys.exit(main())
