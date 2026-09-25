#!/usr/bin/env python3
"""Tests for provision_one.py and provision-batch.py's use of it, against a fake
esptool with a simulated flash (scripts/fake_esptool.py). No board, no PlatformIO,
no network: the Worker mint, build_nvs and the --verify-url check are injected.

    python3 scripts/test_provision_one.py
"""
import contextlib
import email.message
import importlib.util
import io
import json
import os
import re
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, HERE / file)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


po = load("provision_one", "provision_one.py")
batch = load("provision_batch", "provision-batch.py")

FAKE = [sys.executable, str(HERE / "fake_esptool.py")]
MAC = "02:00:00:00:00:01"            # not a real board (locally administered)
SALT = "test-salt"
NVS_OFF, NVS_SIZE = "0x9000", 0x15000
TOKEN = "test-provision-token-not-a-real-one"


def fake_key(dev_id):
    return (dev_id * 4)[:64]          # a well-formed 64-hex stand-in for a minted key


def fake_mint(mac):
    """Stands in for the Worker: the id it would derive, and a key."""
    dev = po.pd.device_id(mac, SALT)
    return dev, fake_key(dev)


def fake_nvs(key, cloud_url, workdir, size):
    """Stands in for nvs_partition_gen: a deterministic image carrying the key."""
    p = Path(workdir) / "nvs.bin"
    body = b"NVS" + key.encode()
    p.write_bytes(body + b"\xff" * (size - len(body)))
    return p


class Rig(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.dir = self.td.name
        os.environ["FAKE_ESPTOOL_DIR"] = self.dir
        for k in ("FAKE_FAIL", "FAKE_DROP_WRITES"):
            os.environ.pop(k, None)
        self.verified = []

    def tearDown(self):
        self.td.cleanup()

    def calls(self):
        p = Path(self.dir) / "calls.jsonl"
        return [json.loads(l) for l in p.read_text().splitlines()] if p.exists() else []

    def flash(self):
        return (Path(self.dir) / "flash.bin").read_bytes()

    def verify_ok(self, code=200):
        def f(url, key, dev_id):
            self.verified.append((url, dev_id))
            return code
        return f

    def run_one(self, mac=MAC, code=200, verify_url="https://proxy.example", mint_fn=fake_mint):
        return po.provision("COMX", mac, esptool_cmd=FAKE, dashed=True, baud=921600, salt=SALT,
                            mint_fn=mint_fn, nvs_offset=NVS_OFF, nvs_size=NVS_SIZE,
                            verify_url=verify_url, build_nvs=fake_nvs, verify_fn=self.verify_ok(code))


class ProvisionOne(Rig):
    def test_ok_writes_the_minted_key_proves_it_and_verifies(self):
        status, dev_id, _ = self.run_one()
        self.assertEqual(status, "OK")
        self.assertEqual(dev_id, po.pd.device_id(MAC, SALT))
        subs = [(c["sub"], c["args"][0]) for c in self.calls()]
        self.assertEqual(subs, [("write-flash", NVS_OFF), ("verify-flash", NVS_OFF)])
        key = fake_key(dev_id)
        self.assertEqual(self.flash()[0x9000:0x9000 + 3 + len(key)], b"NVS" + key.encode())
        self.assertEqual(self.verified, [("https://proxy.example", dev_id)])

    def test_a_board_that_did_not_take_the_write_fails(self):
        os.environ["FAKE_DROP_WRITES"] = "1"
        status, _, detail = self.run_one()
        self.assertEqual(status, "FAIL")
        self.assertIn("NOT on the board", detail)
        self.assertEqual(self.verified, [], "must not call the key good when it is not on the board")

    def test_write_failure(self):
        os.environ["FAKE_FAIL"] = "write-flash"
        status, _, detail = self.run_one()
        self.assertEqual((status, "nvs write failed" in detail), ("FAIL", True))

    def test_a_refused_key_at_verify_is_a_failure(self):
        status, _, detail = self.run_one(code=401)
        self.assertEqual(status, "FAIL")
        self.assertIn("REJECTED (401)", detail)

    def test_other_http_is_inconclusive(self):
        status, _, detail = self.run_one(code=503)
        self.assertEqual(status, "FAIL")
        self.assertIn("inconclusive (HTTP 503)", detail)

    def test_bad_mac_touches_nothing(self):
        status, _, _ = self.run_one(mac="not-a-mac")
        self.assertEqual(status, "FAIL")
        self.assertEqual(self.calls(), [])

    def test_a_refused_mint_writes_nothing(self):
        def refused(mac):
            raise po.MintError("PROVISION_TOKEN rejected (403) -- the token file does not match the Worker's")
        status, _, detail = self.run_one(mint_fn=refused)
        self.assertEqual(status, "FAIL")
        self.assertIn("PROVISION_TOKEN rejected", detail)
        self.assertEqual(self.calls(), [], "no write after a refused mint")

    def test_salt_drift_is_refused_before_any_write(self):
        """The Worker returned an id this firmware would never report."""
        def drifted(mac):
            return "0" * 16, fake_key("0" * 16)
        status, _, detail = self.run_one(mint_fn=drifted)
        self.assertEqual(status, "FAIL")
        self.assertIn("salt drift", detail)
        self.assertEqual(self.calls(), [])


class _Resp(io.BytesIO):
    def __init__(self, body, status):
        super().__init__(json.dumps(body).encode())
        self.status = status

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


def urlopen_returning(status, body, seen=None):
    def urlopen(req, timeout=None):
        if seen is not None:
            seen.append(req)
        if status == 200:
            return _Resp(body, 200)
        raise urllib.error.HTTPError(req.full_url, status, "x", email.message.Message(),
                                     io.BytesIO(json.dumps(body).encode()))
    return urlopen


class MintClient(unittest.TestCase):
    """The HTTP half: what the bench sends, and what each Worker answer becomes."""

    def test_ok_returns_id_and_key_and_sends_the_token_in_a_header(self):
        seen = []
        dev, key = "1111222233334444", "ab" * 32
        got = po.mint("https://proxy.example/", TOKEN, MAC,
                      urlopen=urlopen_returning(200, {"deviceId": dev, "key": key, "mintsToday": 1}, seen))
        self.assertEqual(got, (dev, key))
        req = seen[0]
        self.assertEqual(req.full_url, "https://proxy.example" + po.MINT_PATH)
        self.assertEqual(req.get_method(), "POST")
        self.assertEqual(req.get_header("X-provision-token"), TOKEN)
        self.assertEqual(json.loads(req.data), {"mac": MAC})
        self.assertNotIn(TOKEN, req.full_url, "the token never rides the URL")

    def test_403_names_the_token(self):
        with self.assertRaises(po.MintError) as e:
            po.mint("https://p", TOKEN, MAC, urlopen=urlopen_returning(403, {"error": "forbidden"}))
        self.assertIn("PROVISION_TOKEN rejected (403)", str(e.exception))

    def test_429_names_the_cap_and_the_reset(self):
        with self.assertRaises(po.MintError) as e:
            po.mint("https://p", TOKEN, MAC, urlopen=urlopen_returning(
                429, {"error": "daily_cap", "cap": 120, "resetsAt": "2026-09-26T00:00:00.000Z"}))
        self.assertIn("daily mint cap (120)", str(e.exception))
        self.assertIn("2026-09-26", str(e.exception))

    def test_a_200_without_a_well_formed_key_is_not_a_mint(self):
        with self.assertRaises(po.MintError):
            po.mint("https://p", TOKEN, MAC, urlopen=urlopen_returning(200, {"deviceId": "x", "key": "y"}))


class Cli(Rig):
    """main(): the token FILE gates everything; exit 0 only on verified AND recorded."""

    def setUp(self):
        super().setUp()
        self.log = Path(self.dir) / "provisioned.csv"
        self.token_file = Path(self.dir) / "provision-token"
        self.token_file.write_text(TOKEN + "\n", encoding="utf-8")
        pd = po.pd
        self.saved = (pd.salt_from_sources, pd.nvs_geometry, pd.find_esptool, pd.build_nvs, po.verify, po.mint)
        pd.salt_from_sources = lambda env: SALT
        pd.nvs_geometry = lambda env: (NVS_OFF, NVS_SIZE)
        pd.find_esptool = lambda: (FAKE, True)
        pd.build_nvs = fake_nvs
        self.code = 200
        po.verify = lambda url, key, dev_id: self.code
        self.minted = []
        po.mint = lambda base, token, mac: (self.minted.append(token), fake_mint(mac))[1]

    def tearDown(self):
        pd = po.pd
        pd.salt_from_sources, pd.nvs_geometry, pd.find_esptool, pd.build_nvs, po.verify, po.mint = self.saved
        super().tearDown()

    def main(self, *extra):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = po.main(["COMX", MAC, "--env", "blipscope-s3-128", "--verify-url", "https://proxy.example",
                          "--token-file", str(self.token_file), "--log", str(self.log), *extra])
        return rc, out.getvalue()

    def test_ok_exits_zero_records_one_row_and_never_prints_the_token(self):
        rc, out = self.main()
        self.assertEqual(rc, 0, out)
        dev_id = po.pd.device_id(MAC, SALT)
        self.assertTrue(out.strip().endswith(f"RESULT OK {dev_id}"), out)
        self.assertIn("provision token present: True", out)
        self.assertEqual([l for l in out.splitlines() if l.startswith("STEP ")], ["STEP write", "STEP verify"])
        rows = self.log.read_text().splitlines()
        self.assertEqual(len(rows), 2)                       # header + this board
        self.assertIn(dev_id, rows[1])
        self.assertEqual(self.minted, [TOKEN], "the file's token (trimmed) is what was sent")
        self.assertNotIn(TOKEN, out)
        self.assertIsNone(re.search(r"[0-9a-f]{64}", out), "the device key must not be printed")

    def test_no_token_file_refuses_before_touching_the_board(self):
        self.token_file.unlink()
        rc, out = self.main()
        self.assertEqual(rc, 1)
        self.assertIn("provision token present: False", out)
        self.assertIn("no provisioning token at", out)
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.minted, [])

    def test_an_empty_token_file_is_no_token(self):
        self.token_file.write_text("  \n", encoding="utf-8")
        rc, out = self.main()
        self.assertEqual(rc, 1)
        self.assertIn("provision token present: False", out)

    def test_rejected_key_exits_nonzero_and_records_nothing(self):
        self.code = 401
        rc, out = self.main()
        self.assertEqual(rc, 1)
        self.assertIn("RESULT FAIL", out)
        self.assertFalse(self.log.exists() and self.log.stat().st_size > 0)

    def test_verify_url_is_required(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            po.main(["COMX", MAC, "--env", "blipscope-s3-128"])


class NoBenchSecret(unittest.TestCase):
    """DEVICE_KEY_SECRET is Worker-only: no bench script reads it."""

    READ = re.compile(r"(environ|getenv)[^\n]{0,40}DEVICE_KEY_SECRET")

    def test_no_bench_script_reads_the_secret(self):
        for name in ("provision_one.py", "provision-batch.py", "provision-device.py", "mint-fixture.py"):
            src = (HERE / name).read_text(encoding="utf-8")
            self.assertIsNone(self.READ.search(src), f"{name} reads DEVICE_KEY_SECRET")

    def test_control_the_pattern_finds_a_read(self):
        self.assertIsNotNone(self.READ.search('secret = os.environ.get("DEVICE_KEY_SECRET", "")'))


class BatchUsesTheSameFunction(Rig):
    def test_batch_writes_factory_first_then_calls_provision(self):
        self.assertEqual(Path(batch.po.__file__).resolve(), Path(po.__file__).resolve())
        image = Path(self.dir) / "factory.bin"
        image.write_bytes(b"\xe9" + b"F" * 0x20)

        class Cfg: pass
        cfg = Cfg()
        cfg.esptool_cmd, cfg.dashed, cfg.baud = FAKE, True, 921600
        cfg.salt, cfg.mint_fn, cfg.nvs_offset, cfg.nvs_size = SALT, fake_mint, NVS_OFF, NVS_SIZE
        cfg.image, cfg.cloud_url, cfg.verify_url = image, None, "https://proxy.example"
        cfg.dry_run, cfg.force, cfg.count, cfg.env = False, False, 0, "blipscope-s3-128"
        cfg.log = Path(self.dir) / "provisioned.csv"
        state = batch.State(set())
        spy = []
        real, saved_nvs, saved_verify = batch.po.provision, batch.po.pd.build_nvs, batch.po.verify

        def recording(*a, **k):
            spy.append(a[:2])
            return real(*a, **k)
        batch.po.provision, batch.po.pd.build_nvs, batch.po.verify = recording, fake_nvs, (lambda *a: 200)
        os.environ["FAKE_MAC"] = MAC
        try:
            port, status, detail = batch.provision_one("COMX", cfg, state)
        finally:
            batch.po.provision, batch.po.pd.build_nvs, batch.po.verify = real, saved_nvs, saved_verify
        self.assertEqual(status, "OK", detail)
        self.assertEqual(spy, [("COMX", MAC)])
        subs = [(c["sub"], c["args"][0] if c["args"] else "") for c in self.calls()]
        self.assertEqual(subs, [("read-mac", ""), ("write-flash", "0x0"),
                                ("write-flash", NVS_OFF), ("verify-flash", NVS_OFF)])


if __name__ == "__main__":
    unittest.main(verbosity=2)
