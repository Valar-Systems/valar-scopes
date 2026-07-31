#!/usr/bin/env python3
"""provision-device.py -- one command per board: MAC -> id -> key -> NVS -> flash.

WHY THIS EXISTS. Per-device keys are the right auth model for a fleet (a leaked
device revokes one device, not all 600 -- see proxy/src/deviceauth.ts), but they
only work if every board leaves the bench with its OWN minted key. Pasting that
into a web form 600 times is not a plan. This does the whole chain unattended:

    MAC (read over USB)
      -> deviceId  = SHA-256(MAC || LEADERBOARD_SALT)[:8]      (16 hex chars)
      -> deviceKey = HMAC-SHA256(DEVICE_KEY_SECRET, deviceId)  (64 hex chars)
      -> NVS partition image containing that key
      -> flash app + NVS

The key insight that makes it unattended: LeaderboardId() is a pure function of
the factory MAC and a compile-time salt (include/DeviceIdentity.h), and esptool
can read the MAC over USB WITHOUT booting the app. So nothing has to be typed,
read off a serial log, or copied between windows.

    python scripts/provision-device.py --env blipscope-s3-128-cloud
    python scripts/provision-device.py --env ... --verify-url https://scopes.valarsystems.com
    python scripts/provision-device.py --env ... --skip-app        # NVS only, app already on
    python scripts/provision-device.py --env ... --dry-run         # compute + report, flash nothing

DEVICE_KEY_SECRET must be in the environment (the Worker's secret). It is never
printed and never written to disk -- only the derived per-device key reaches the
NVS image, and only the deviceId is logged.

!! FLASHING NVS ERASES ALL DEVICE CONFIG, including saved Wi-Fi credentials.
   That is correct for a factory board (the customer does Wi-Fi setup via the
   device's own AP) and WRONG for a configured bench board -- use --skip-nvs
   there, or expect to re-join Wi-Fi afterwards.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import hmac
import os
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PIO_HOME = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))

# NVS geometry is READ FROM THE ENV'S PARTITION TABLE, never assumed. It used to
# be hardcoded 0x9000/0x5000 -- true for min_spiffs.csv and default_16MB.csv, and
# silently wrong the moment a SKU moved to partitions-s3-16mb-bignvs.csv (84 KB).
# A short image written into a long partition leaves whatever was there before
# beyond its end, and the platform's uploader writes boot_app0 at a hardcoded
# 0xe000 -- inside an enlarged NVS -- so the tail is not blank, it is garbage.
# Generating the image at the FULL partition size overwrites all of it, which is
# what makes the result deterministic.
NVS_DEFAULT_OFFSET, NVS_DEFAULT_SIZE = "0x9000", 0x5000
NVS_NAMESPACE = "config"
KEY_NVS_KEY = "cloud-key"   # matches ConfigurationWebServer's prefs key
URL_NVS_KEY = "cloud-url"

# Must match include/DeviceIdentity.h. Parsed from source below rather than
# hardcoded, because a drifted salt silently changes EVERY device id and
# invalidates every key already minted -- a failure that would only surface
# after the boards had shipped.
SALT_HEADER = REPO / "include" / "DeviceIdentity.h"


def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def salt_from_sources(env: str) -> str:
    """The salt actually compiled into this env, cross-checked against the header.

    Two places can define it: the #ifndef default in DeviceIdentity.h and a
    -DLEADERBOARD_SALT build flag. If a build flag exists it wins at compile
    time, so it must win here too -- otherwise we would mint keys against ids
    the firmware never produces.
    """
    header_default = None
    if SALT_HEADER.exists():
        m = re.search(r'define\s+LEADERBOARD_SALT\s+"([^"]*)"', SALT_HEADER.read_text(encoding="utf-8"))
        if m:
            header_default = m.group(1)

    override = None
    ini = (REPO / "platformio.ini").read_text(encoding="utf-8")
    in_env = False
    for line in ini.splitlines():
        if line.startswith("[env:"):
            in_env = line.strip() == f"[env:{env}]"
        if in_env:
            m = re.search(r'-DLEADERBOARD_SALT=\\?"([^"\\]*)', line)
            if m:
                override = m.group(1)

    salt = override or header_default
    if not salt:
        die("could not determine LEADERBOARD_SALT from DeviceIdentity.h or platformio.ini")
    if override and header_default and override != header_default:
        print(f"  note: env overrides the header salt ({header_default!r} -> {override!r})")
    return salt


def nvs_geometry(env: str) -> tuple[str, int]:
    """(offset, size) of the nvs partition for this env, from its partition CSV.

    Resolution mirrors PlatformIO's: `board_build.partitions` inside the env (or
    the [env] it extends) names either a repo-local CSV or one shipped with the
    framework. Parsed rather than assumed, for the same reason the salt is --
    a mismatch here is invisible until the boards are already provisioned.
    """
    ini = (REPO / "platformio.ini").read_text(encoding="utf-8")
    blocks, cur = {}, None
    for line in ini.splitlines():
        if line.startswith("["):
            cur = line.strip().strip("[]")
            blocks[cur] = []
        elif cur:
            blocks[cur].append(line)

    def find(section: str, depth: int = 0) -> str | None:
        if depth > 5 or section not in blocks:
            return None
        for line in blocks[section]:
            m = re.match(r"\s*board_build\.partitions\s*=\s*(\S+)", line)
            if m:
                return m.group(1)
        for line in blocks[section]:  # follow `extends = ...` only if not set here
            m = re.match(r"\s*extends\s*=\s*(\S+)", line)
            if m:
                return find(m.group(1), depth + 1)
        return None

    csv_name = find(f"env:{env}")
    if not csv_name:
        return NVS_DEFAULT_OFFSET, NVS_DEFAULT_SIZE

    for cand in (REPO / csv_name,
                 PIO_HOME / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / csv_name):
        if cand.exists():
            for row in cand.read_text(encoding="utf-8").splitlines():
                if row.strip().startswith("#") or not row.strip():
                    continue
                f = [c.strip() for c in row.split(",")]
                if len(f) >= 5 and f[0] == "nvs":
                    off, size = f[3], int(f[4], 0)
                    print(f"  partitions {csv_name}: nvs at {off}, {size // 1024} KB")
                    return off, size
    die(f"could not find an nvs row in {csv_name}")


def pio_exe() -> str:
    """PlatformIO's launcher, by absolute path.

    A bare "pio" is frequently NOT on PATH -- it lives in PlatformIO's own venv,
    and the VS Code / IDE integrations add it per-shell. Combined with a silent
    `except: return []` in port discovery that cost 15 minutes of a batch watcher
    finding zero boards with nothing on stdout to say why.
    """
    for c in (PIO_HOME / "penv" / "Scripts" / "platformio.exe",   # Windows
              PIO_HOME / "penv" / "bin" / "platformio"):           # POSIX
        if c.exists():
            return str(c)
    return "pio"


def pio_python() -> str:
    """PlatformIO's OWN interpreter, not ours.

    esptool ships as a PlatformIO package but its dependencies (rich_click et al)
    are installed only inside PlatformIO's venv -- running it with the system
    python fails with ModuleNotFoundError. Same for nvs_partition_gen.
    """
    for c in (PIO_HOME / "penv" / "Scripts" / "python.exe",   # Windows
              PIO_HOME / "penv" / "bin" / "python"):           # POSIX
        if c.exists():
            return str(c)
    print("  note: PlatformIO venv not found, falling back to this interpreter")
    return sys.executable


def find_esptool() -> tuple[list[str], bool]:
    """(command prefix, uses_dashed_subcommands).

    esptool 5 renamed every subcommand from snake_case to kebab-case and warns
    loudly on the old spelling; esptool 4 only accepts the old one. Detect rather
    than pin, so this keeps working across a platform bump.
    """
    base = PIO_HOME / "packages" / "tool-esptoolpy"
    if not ((base / "esptool" / "__init__.py").exists() or (base / "esptool.py").exists()):
        die(f"esptool not found under {base} -- run a `pio run` once first")
    os.environ["PYTHONPATH"] = str(base) + os.pathsep + os.environ.get("PYTHONPATH", "")
    cmd = [pio_python(), "-m", "esptool"]
    try:
        ver = subprocess.run(cmd + ["version"], capture_output=True, text=True, timeout=60).stdout
        major = int(re.search(r"(\d+)\.", ver).group(1))
    except Exception:
        major = 4
    return cmd, major >= 5


def find_nvs_gen() -> Path:
    p = (PIO_HOME / "packages" / "framework-espidf" / "components" / "nvs_flash"
         / "nvs_partition_generator" / "nvs_partition_gen.py")
    if not p.exists():
        die(f"nvs_partition_gen.py not found at {p}")
    return p


def read_mac(esptool_cmd: list[str], dashed: bool, port: str | None) -> str:
    """Read the factory MAC over USB. No app boot required, so this works on a
    blank board straight off the reel."""
    cmd = esptool_cmd + (["--port", port] if port else []) + ["read-mac" if dashed else "read_mac"]
    print(f"  $ {' '.join(cmd[2:])}")
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
        out = r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        die("esptool read_mac timed out -- is the board connected? (hold BOOT, tap RESET)")
    # esptool prints "MAC: aa:bb:.." (and on some versions "BASE MAC: ..").
    macs = re.findall(r"(?:BASE\s+)?MAC:\s*((?:[0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2})", out)
    if not macs:
        die("could not parse a MAC from esptool output:\n" + out)
    return macs[0].replace("-", ":").lower()


def device_id(mac: str, salt: str) -> str:
    """SHA-256(raw 6 MAC bytes || salt bytes), first 8 bytes as hex.

    Byte-for-byte what DeviceIdentity::LeaderboardId() computes on-device:
    the RAW mac bytes (not the printable string) followed by the salt's ASCII.
    """
    raw = bytes(int(o, 16) for o in mac.split(":"))
    return hashlib.sha256(raw + salt.encode("ascii")).digest()[:8].hex()


def device_key(secret: str, dev_id: str) -> str:
    """HMAC-SHA256(secret, deviceId) hex -- what deviceauth.ts recomputes."""
    return hmac.new(secret.encode("utf-8"), dev_id.encode("utf-8"), hashlib.sha256).hexdigest()


def build_nvs(key: str, cloud_url: str | None, workdir: Path, size: int = NVS_DEFAULT_SIZE) -> Path:
    csv_path, bin_path = workdir / "nvs.csv", workdir / "nvs.bin"
    rows = [["key", "type", "encoding", "value"],
            [NVS_NAMESPACE, "namespace", "", ""],
            [KEY_NVS_KEY, "data", "string", key]]
    if cloud_url:
        rows.append([URL_NVS_KEY, "data", "string", cloud_url])
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        csv.writer(f).writerows(rows)
    subprocess.run([pio_python(), str(find_nvs_gen()), "generate",
                    str(csv_path), str(bin_path), str(size)],
                   check=True, capture_output=True, text=True)
    csv_path.unlink()  # it contains the device key in plaintext -- do not leave it around
    return bin_path


def main() -> None:
    ap = argparse.ArgumentParser(description="Provision one board with a per-device key.")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. blipscope-s3-128-cloud")
    ap.add_argument("--port", help="serial port (auto-detect if omitted)")
    ap.add_argument("--cloud-url", help="also bake this into NVS as cloud-url")
    ap.add_argument("--verify-url", help="after minting, check the key against this proxy base")
    ap.add_argument("--skip-app", action="store_true", help="flash NVS only (app already present)")
    ap.add_argument("--skip-nvs", action="store_true", help="do NOT touch NVS (preserves Wi-Fi)")
    ap.add_argument("--dry-run", action="store_true", help="compute and report; flash nothing")
    ap.add_argument("--log", default=str(REPO / "provisioned.csv"), help="append a record here")
    args = ap.parse_args()

    secret = os.environ.get("DEVICE_KEY_SECRET", "").strip()
    if not secret:
        die("set DEVICE_KEY_SECRET in the environment (the Worker's secret)")

    print(f"\n=== provisioning [{args.env}] ===")
    salt = salt_from_sources(args.env)
    nvs_offset, nvs_size = nvs_geometry(args.env)
    esptool_cmd, dashed = find_esptool()

    mac = read_mac(esptool_cmd, dashed, args.port)
    dev_id = device_id(mac, salt)
    key = device_key(secret, dev_id)
    print(f"  MAC        {mac}")
    print(f"  deviceId   {dev_id}          (salt {salt!r})")
    print(f"  deviceKey  {'*' * 56}{key[-8:]}   (not logged)")

    if args.dry_run:
        print("\n  dry run: nothing flashed\n")
        return

    if not args.skip_app:
        print("\n  flashing app ...")
        r = subprocess.run([pio_exe(), "run", "-e", args.env, "-t", "upload"]
                           + (["--upload-port", args.port] if args.port else []),
                           cwd=REPO)
        if r.returncode != 0:
            die("app upload failed")

    if not args.skip_nvs:
        print("\n  building + flashing NVS (ERASES Wi-Fi and all device config) ...")
        with tempfile.TemporaryDirectory() as td:
            nvs_bin = build_nvs(key, args.cloud_url, Path(td), nvs_size)
            cmd = esptool_cmd + (["--port", args.port] if args.port else []) \
                + ["write-flash" if dashed else "write_flash", nvs_offset, str(nvs_bin)]
            r = subprocess.run(cmd)
            if r.returncode != 0:
                die("NVS flash failed")

    if args.verify_url:
        # Verifies the MINTING, not the board: a 200 proves DEVICE_KEY_SECRET here
        # matches the Worker's, so every key from this run is valid. A 401 means
        # the secret is wrong and the whole batch would ship unauthenticated.
        import urllib.request
        # UA matters: Cloudflare 403s the default Python-urllib one at the edge,
        # which is indistinguishable from a rejected key unless you know.
        req = urllib.request.Request(
            args.verify_url.rstrip("/") + "/v1/config",
            headers={"X-Blip-Key": key, "X-Blip-Device": dev_id, "X-Blip-Model": "s3-128",
                     "User-Agent": "Blipscope-Provisioner/1"})
        try:
            code = urllib.request.urlopen(req, timeout=30).status
        except Exception as e:
            code = getattr(e, "code", 0)
        print(f"\n  verify {args.verify_url}: HTTP {code}"
              + ("  OK - key accepted" if code == 200 else "  *** KEY REJECTED ***"))
        if code != 200:
            die("minted key was rejected -- check DEVICE_KEY_SECRET matches the Worker's")

    # Provisioning record. deviceId only: the key is re-derivable from the secret,
    # so there is no reason to persist it and every reason not to.
    log = Path(args.log)
    new = not log.exists()
    with log.open("a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["utc", "env", "mac", "device_id"])
        w.writerow([datetime.now(timezone.utc).isoformat(timespec="seconds"), args.env, mac, dev_id])
    print(f"\n  logged to {log}")
    print("  DONE\n")


if __name__ == "__main__":
    main()
