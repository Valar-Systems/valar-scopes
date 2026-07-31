#!/usr/bin/env python3
"""provision-batch.py -- burn a whole batch: N boards at once, each with its own key.

WHY THIS EXISTS. scripts/provision-device.py does one board correctly; fifty of
them one at a time is an hour of plugging, waiting, and unplugging. The slow part
is not the tooling, it is that a single board is idle for most of its ~60 s while
esptool talks to it. Run eight at once on a powered hub and the batch is ~10 min,
and you touch the bench 7 times instead of 50.

    python scripts/provision-batch.py --env blipscope-s3-128-prodburn \
        --verify-url https://scopes.valarsystems.com --count 50

Plug in a hub-full, walk away, come back, swap them out. It keeps watching for
newly-attached boards until --count is reached (or Ctrl-C).

HOW IT DIFFERS FROM THE SINGLE-BOARD SCRIPT (both matter at batch scale):

  * ONE build, up front. The per-board path never invokes `pio` -- eight
    concurrent `pio run -t upload` would race on the same .pio/build directory.
    We build once, then drive esptool directly.

  * TWO esptool calls per board, in this order and no other:
        write_flash 0x0     firmware.factory.bin
        write_flash <nvs>   <that board's nvs.bin>
    The factory image spans 0x0..end-of-app, so it CONTAINS the NVS region and
    blanks it with 0xFF gap fill -- the key must therefore be written after it.
    They cannot be combined into one call: esptool 5 rejects overlapping regions.
    NVS offset and size come from the env's partition table, never hardcoded.

  * IDEMPOTENT BY MAC, not by port. COM numbers are recycled the moment you
    unplug, so port identity means nothing here. A board whose MAC is already in
    provisioned.csv is recognised and skipped, which makes "did I already do this
    one?" a question you never have to answer.

  * FAILURE-ISOLATED. One board that won't talk fails alone; the other seven
    finish. Failures are listed again at the end so nothing is lost in scrollback.

DEVICE_KEY_SECRET must be in the environment, exactly as for the single-board
script. It is never printed and never written to disk.

!! Every board is fully erased and re-provisioned: this writes the factory image,
   so any existing Wi-Fi credentials and config are gone. That is correct for
   factory boards and wrong for a configured bench board.

!! Stop the bench serial capture first (scripts/bench-capture.ps1). It reattaches
   to any ESP32-S3 it finds and will fight this for the port.
"""
from __future__ import annotations

import argparse
import csv
import importlib.util
import os
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Reuse the single-board script's logic rather than restating it. The salt
# parsing, id derivation and key derivation MUST stay bit-identical to the
# firmware's -- a second copy that drifts would mint keys for ids no device ever
# produces, and it would only surface after the boards had shipped.
_spec = importlib.util.spec_from_file_location(
    "provision_device", Path(__file__).parent / "provision-device.py")
pd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pd)

FACTORY_OFFSET = "0x0"
# NVS offset/size come from the env's partition table at run time (see
# provision-device.nvs_geometry): this SKU moved to an 84 KB nvs, and a hardcoded
# 0x5000 would leave 64 KB of the partition holding whatever was there before.

print_lock = threading.Lock()


def say(msg: str) -> None:
    """Serialised print. Eight workers writing at once otherwise interleave
    mid-line and the log becomes unreadable exactly when something goes wrong."""
    with print_lock:
        print(msg, flush=True)


def factory_image(env: str) -> Path:
    """The merged bootloader+partitions+app image, built by the platform as
    `firmware.factory.bin`. We flash this rather than the OTA `firmware.bin`:
    the OTA image is app-only, and writing it at 0x0 yields a board with no
    bootloader that looks bricked."""
    p = REPO / ".pio" / "build" / env / "firmware.factory.bin"
    if not p.exists():
        pd.die(f"{p} not found -- the build did not produce a merged factory image")
    return p


def build_once(env: str) -> None:
    say(f"  building {env} (once, for the whole batch) ...")
    r = subprocess.run([pd.pio_exe(), "run", "-e", env], cwd=REPO)
    if r.returncode != 0:
        pd.die("build failed")


_discovery_complained = False


def list_esp_ports() -> list[str]:
    """Attached Espressif USB devices. VID 303A covers the S3's native USB-CDC
    and USB-JTAG; a hub full of identical boards all present as 303A:1001.

    Discovery failures are REPORTED, once. This swallowed its exception silently
    and, with a bare "pio" that was not on PATH, presented as a watcher sitting
    at zero boards forever with no clue on stdout."""
    global _discovery_complained
    try:
        import json
        out = subprocess.run([pd.pio_exe(), "device", "list", "--json-output"],
                             capture_output=True, text=True, timeout=60).stdout
        return sorted(d["port"] for d in json.loads(out) if "303A" in (d.get("hwid") or "").upper())
    except Exception as e:
        if not _discovery_complained:
            _discovery_complained = True
            say(f"  !! port discovery failing ({type(e).__name__}: {e}) -- no boards will be seen")
        return []


def already_done(log_path: Path) -> set[str]:
    """MACs already provisioned, from the manufacturing record. Survives restarts,
    so an interrupted batch resumes instead of re-burning what it finished."""
    done: set[str] = set()
    if log_path.exists():
        with log_path.open(newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                if row.get("mac"):
                    done.add(row["mac"].lower())
    return done


def verify(base: str, key: str, dev_id: str) -> int:
    """HTTP status from presenting this key to the proxy. 200 = accepted.

    The User-Agent is NOT decoration: Cloudflare's edge 403s the default
    `Python-urllib/3.x` before the Worker ever sees the request, which looks
    exactly like a rejected key and sent me chasing DEVICE_KEY_SECRET."""
    import urllib.request
    req = urllib.request.Request(
        base.rstrip("/") + "/v1/config",
        headers={"X-Blip-Key": key, "X-Blip-Device": dev_id, "X-Blip-Model": "s3-128",
                 "User-Agent": "Blipscope-Provisioner/1"})
    try:
        return urllib.request.urlopen(req, timeout=30).status
    except Exception as e:
        return getattr(e, "code", 0)


def provision_one(port: str, cfg, state) -> tuple[str, str, str]:
    """Returns (port, status, detail). Never raises: a board that fails must not
    take the batch down with it."""
    try:
        mac = pd.read_mac(cfg.esptool_cmd, cfg.dashed, port)
    except SystemExit:
        return port, "FAIL", "could not read MAC (bad cable, or hold BOOT + tap RESET)"
    except Exception as e:
        return port, "FAIL", f"could not read MAC: {e}"

    with state.lock:
        if mac in state.done:
            return port, "SKIP", f"{mac} already provisioned"
        if mac in state.inflight:
            return port, "SKIP", f"{mac} already in flight"
        state.inflight.add(mac)

    try:
        dev_id = pd.device_id(mac, cfg.salt)
        key = pd.device_key(cfg.secret, dev_id)
        say(f"  [{port}] {mac} -> {dev_id}  flashing ...")

        if cfg.dry_run:
            return port, "DRY", f"{mac} -> {dev_id} (nothing flashed)"

        with tempfile.TemporaryDirectory() as td:
            nvs_bin = pd.build_nvs(key, cfg.cloud_url, Path(td), cfg.nvs_size)
            write = "write-flash" if cfg.dashed else "write_flash"
            # TWO calls, factory FIRST then NVS -- not one call with both offsets.
            # The factory image spans 0x0..end-of-app, which contains the NVS
            # region, and esptool 5 rejects overlapping regions within a single
            # write_flash. Order is load-bearing: the factory image blanks NVS
            # (0xFF gap fill), so the key must be written after it, not before.
            for label, off, img in (("factory", FACTORY_OFFSET, str(cfg.image)),
                                    ("nvs", cfg.nvs_offset, str(nvs_bin))):
                cmd = cfg.esptool_cmd + ["--port", port, "--baud", str(cfg.baud),
                                         write, off, img]
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
                if r.returncode != 0:
                    tail = (r.stdout + r.stderr).strip().splitlines()[-3:]
                    return port, "FAIL", f"{mac}: {label} write failed -- " + " / ".join(tail)

        if cfg.verify_url:
            code = verify(cfg.verify_url, key, dev_id)
            if code == 401:
                # THE one that means the batch is bad: DEVICE_KEY_SECRET here does
                # not match the Worker's, so every key in this run is worthless.
                return port, "FAIL", f"{mac}: key REJECTED (401) -- DEVICE_KEY_SECRET does not match the Worker's"
            if code != 200:
                # Anything else is the check failing, not the key. Don't send the
                # operator hunting for a secret mismatch that isn't there.
                return port, "FAIL", f"{mac}: verify inconclusive (HTTP {code}) -- board is flashed; key not confirmed"

        with state.lock:
            state.done.add(mac)
            with cfg.log.open("a", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                if state.new_log:
                    w.writerow(["utc", "env", "mac", "device_id"])
                    state.new_log = False
                w.writerow([datetime.now(timezone.utc).isoformat(timespec="seconds"),
                            cfg.env, mac, dev_id])
            n = len(state.done) - state.preexisting
        return port, "OK", f"{mac} -> {dev_id}   [{n}" + (f"/{cfg.count}]" if cfg.count else "]")
    except Exception as e:
        return port, "FAIL", f"{mac}: {e}"
    finally:
        with state.lock:
            state.inflight.discard(mac)


class State:
    def __init__(self, done: set[str], new_log: bool):
        self.lock = threading.Lock()
        self.done = done
        self.inflight: set[str] = set()
        self.preexisting = len(done)
        self.new_log = new_log


def main() -> None:
    ap = argparse.ArgumentParser(description="Provision a batch of boards in parallel.")
    ap.add_argument("--env", required=True, help="PlatformIO env, e.g. blipscope-s3-128-prodburn")
    ap.add_argument("--jobs", type=int, default=0, help="boards in parallel (default: however many are attached, max 8)")
    ap.add_argument("--count", type=int, default=0, help="stop after this many NEW boards")
    ap.add_argument("--verify-url", help="check each minted key against this proxy base")
    ap.add_argument("--cloud-url", help="also bake this into NVS as cloud-url")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--once", action="store_true", help="do what's attached now, then exit (no watching)")
    ap.add_argument("--dry-run", action="store_true", help="compute and report; flash nothing")
    ap.add_argument("--skip-build", action="store_true", help="reuse the existing build")
    ap.add_argument("--log", default=str(REPO / "provisioned.csv"))
    args = ap.parse_args()

    secret = os.environ.get("DEVICE_KEY_SECRET", "").strip()
    if not secret:
        pd.die("set DEVICE_KEY_SECRET in the environment (the Worker's secret)")

    print(f"\n=== batch provisioning [{args.env}] ===")
    salt = pd.salt_from_sources(args.env)
    nvs_offset, nvs_size = pd.nvs_geometry(args.env)
    esptool_cmd, dashed = pd.find_esptool()   # once: it mutates PYTHONPATH
    if not args.skip_build and not args.dry_run:
        build_once(args.env)

    class Cfg: pass
    cfg = Cfg()
    cfg.env, cfg.salt, cfg.secret = args.env, salt, secret
    cfg.esptool_cmd, cfg.dashed, cfg.baud = esptool_cmd, dashed, args.baud
    cfg.nvs_offset, cfg.nvs_size = nvs_offset, nvs_size
    cfg.image = factory_image(args.env) if not args.dry_run else None
    cfg.verify_url, cfg.cloud_url = args.verify_url, args.cloud_url
    cfg.count, cfg.dry_run = args.count, args.dry_run
    cfg.log = Path(args.log)

    done = already_done(cfg.log)
    state = State(done, new_log=not cfg.log.exists())
    if done:
        print(f"  {len(done)} board(s) already in {cfg.log.name} -- they will be skipped if re-plugged")

    failures: list[str] = []
    busy: set[str] = set()
    # Sized for the hub, NOT for what happens to be plugged in right now: the
    # normal flow is to start this and THEN load the hub, so sizing from the
    # current port count would pin the whole batch at one-at-a-time.
    jobs = args.jobs or 8
    print(f"  up to {jobs} board(s) in parallel; "
          + ("single pass" if args.once else "watching for boards -- Ctrl-C when done") + "\n")

    try:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {}
            while True:
                ports = set(list_esp_ports())
                # A vanished port frees its slot: COM numbers are recycled, so the
                # next board to appear there must be read fresh, not assumed done.
                busy &= ports
                for port in sorted(ports - busy):
                    busy.add(port)
                    futures[pool.submit(provision_one, port, cfg, state)] = port

                for fut in [f for f in futures if f.done()]:
                    port = futures.pop(fut)
                    p, status, detail = fut.result()
                    say(f"  [{p}] {status}  {detail}")
                    if status == "FAIL":
                        failures.append(f"[{p}] {detail}")
                    # Leave the port in `busy` until it is physically unplugged,
                    # otherwise a finished board sitting on the hub is re-flashed
                    # in a loop.

                newly = len(state.done) - state.preexisting
                if cfg.count and newly >= cfg.count:
                    say(f"\n  reached --count {cfg.count}")
                    break
                if args.once and not futures and not (ports - busy):
                    break
                time.sleep(2)
    except KeyboardInterrupt:
        print("\n  stopping (letting in-flight boards finish) ...")

    newly = len(state.done) - state.preexisting
    print(f"\n=== batch complete: {newly} newly provisioned, {len(failures)} failed ===")
    for f in failures:
        print(f"  FAILED  {f}")
    print(f"  record: {cfg.log}\n")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
