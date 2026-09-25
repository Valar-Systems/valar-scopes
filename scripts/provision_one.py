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
      -> NVS image -> write it at the nvs offset from the env's partition table,
         with `--after no-reset`, and require write-flash's OWN on-chip check:
         exit 0 AND the line "Hash of data verified." -- the flash MD5 of the
         region, computed on the chip BEFORE any reset (esptool cmds.py:1552-1572)
      -> reset via read-mac, which also re-checks the MAC: the same board answered
      -> present the key to --verify-url: 200 proves the Worker accepts it
      -> append the manufacturing record, read back

WHY THE CHECK IS BEFORE THE RESET, AND NEVER A LATER COMPARE OF NVS. Step 4
(2026-09-25) failed with the key present and correct: the write hard-reset the
board, the firmware booted and wrote its own entries into NVS (Wi-Fi AP settings,
PHY calibration), and a separate verify-flash of the whole partition then found a
digest mismatch. NVS is a MUTABLE partition; a byte compare taken after the app
has run proves nothing either way. The on-chip hash at write time is the board-side
proof. The definitive proof is Worker-side and comes later: the board's own
authenticated requests under its id, once it is on Wi-Fi.

EVERY esptool call's full output goes to the per-board log (--esptool-log), because
Step 4's diagnosis needed the write's output and only three lines had been kept.

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


HASH_OK = "Hash of data verified."
MAC_FOUND = re.compile(r"MAC:\s*((?:[0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2})")


def _run_logged(cmd: list, log_file) -> tuple:
    """(returncode, combined output). The FULL output is appended to log_file."""
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    out = (r.stdout or "") + (r.stderr or "")
    if log_file:
        Path(log_file).parent.mkdir(parents=True, exist_ok=True)
        with open(log_file, "a", encoding="utf-8") as f:
            f.write(f"$ {' '.join(cmd[2:])}\n{out}\n[exit {r.returncode}]\n\n")
    return r.returncode, out


def provision(port: str, mac: str, *, esptool_cmd: list, dashed: bool, baud: int, salt: str,
              mint_fn, nvs_offset: str, nvs_size: int, cloud_url: str | None = None,
              verify_url: str | None = None, build_nvs=None, verify_fn=None, on_step=None,
              log_file=None) -> tuple:
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
        no_reset = "no-reset" if dashed else "no_reset"
        read_mac = "read-mac" if dashed else "read_mac"
        with tempfile.TemporaryDirectory() as td:
            nvs_bin = str(build_nvs(key, cloud_url, Path(td), nvs_size))
            on_step("write")
            # The check is write-flash's own on-chip hash, BEFORE any reset: with
            # --after no-reset the firmware cannot run and change NVS in between.
            rc, out = _run_logged(esptool_cmd + ["--port", port, "--baud", str(baud), "--after", no_reset,
                                                 write, nvs_offset, nvs_bin], log_file)
            if rc != 0:
                tail = out.strip().splitlines()[-3:]
                return "FAIL", dev_id, f"{mac}: nvs write failed its on-chip hash -- " + " / ".join(tail)
            if HASH_OK not in out:
                # esptool skips its hash check SILENTLY when the ROM cannot do MD5
                # (cmds.py:1573), and exit 0 alone would then prove nothing.
                return "FAIL", dev_id, f"{mac}: nvs write was NOT verified on-chip (no '{HASH_OK}') -- not trusted"
        # Now reset, via a MAC read: it boots the firmware AND proves the board that
        # answered is the board that was written.
        rc, out = _run_logged(esptool_cmd + ["--port", port, read_mac], log_file)
        seen = MAC_FOUND.findall(out)
        if rc != 0 or not seen:
            return "FAIL", dev_id, f"{mac}: the key is written and verified, but the reset read failed -- reseat and re-run"
        if seen[0].replace("-", ":").lower() != mac:
            return "FAIL", dev_id, f"{mac}: a DIFFERENT board answered after the write -- stop and check the hub"
        on_step("verify")
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
    ap.add_argument("--esptool-log", help="full esptool output for this board (default: provision-logs/ in this repo)")
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
    from datetime import datetime, timezone
    esptool_log = a.esptool_log or str(REPO / "provision-logs" / (
        f"{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}-{a.mac.lower().replace(':', '').replace('-', '')}.log"))
    print(f"esptool log: {esptool_log}", flush=True)
    status, dev_id, detail = provision(a.port, a.mac, esptool_cmd=esptool_cmd, dashed=dashed, baud=a.baud,
                                      salt=salt, mint_fn=lambda m: mint(a.verify_url, token, m),
                                      nvs_offset=nvs_offset, nvs_size=nvs_size,
                                      cloud_url=a.cloud_url, verify_url=a.verify_url,
                                      on_step=lambda step: print(f"STEP {step}", flush=True),
                                      log_file=esptool_log)
    if status != "OK":
        return result(False, detail)
    try:
        pd.append_log(log, a.env, a.mac.strip().lower().replace("-", ":"), dev_id)
    except SystemExit:
        return result(False, f"{dev_id}: key is on the board and verified, but the record did not land")
    return result(True, dev_id)


if __name__ == "__main__":
    sys.exit(main())
