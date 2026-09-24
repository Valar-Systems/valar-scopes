#!/usr/bin/env python3
"""Tests for provision_one.py and provision-batch.py's use of it, against a fake
esptool with a simulated flash (scripts/fake_esptool.py). No board, no PlatformIO,
no network: build_nvs and the --verify-url check are injected.

    python3 scripts/test_provision_one.py
"""
import contextlib
import importlib.util
import io
import json
import os
import re
import sys
import tempfile
import unittest
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
MAC = "00:00:00:00:00:01"            # not a real board
SECRET = "test-only-secret-not-the-fleet-secret"
SALT = "test-salt"
NVS_OFF, NVS_SIZE = "0x9000", 0x15000


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

    def run_one(self, mac=MAC, code=200, verify_url="https://proxy.example"):
        return po.provision("COMX", mac, esptool_cmd=FAKE, dashed=True, baud=921600, salt=SALT,
                            secret=SECRET, nvs_offset=NVS_OFF, nvs_size=NVS_SIZE,
                            verify_url=verify_url, build_nvs=fake_nvs, verify_fn=self.verify_ok(code))


class ProvisionOne(Rig):
    def test_ok_writes_the_key_proves_it_and_verifies(self):
        status, dev_id, _ = self.run_one()
        self.assertEqual(status, "OK")
        self.assertEqual(dev_id, po.pd.device_id(MAC, SALT))
        subs = [(c["sub"], c["args"][0]) for c in self.calls()]
        self.assertEqual(subs, [("write-flash", NVS_OFF), ("verify-flash", NVS_OFF)])
        key = po.pd.device_key(SECRET, dev_id)
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

    def test_rejected_key_names_the_secret(self):
        status, _, detail = self.run_one(code=401)
        self.assertEqual(status, "FAIL")
        self.assertIn("REJECTED (401)", detail)

    def test_other_http_is_inconclusive_not_a_secret_mismatch(self):
        status, _, detail = self.run_one(code=503)
        self.assertEqual(status, "FAIL")
        self.assertIn("inconclusive (HTTP 503)", detail)
        self.assertNotIn("DEVICE_KEY_SECRET", detail)

    def test_bad_mac_touches_nothing(self):
        status, _, _ = self.run_one(mac="not-a-mac")
        self.assertEqual(status, "FAIL")
        self.assertEqual(self.calls(), [])


class Cli(Rig):
    """main(): exit 0 only on verified AND recorded; the secret never printed."""

    def setUp(self):
        super().setUp()
        self.log = Path(self.dir) / "provisioned.csv"
        pd = po.pd
        self.saved = (pd.salt_from_sources, pd.nvs_geometry, pd.find_esptool, pd.build_nvs, po.verify)
        pd.salt_from_sources = lambda env: SALT
        pd.nvs_geometry = lambda env: (NVS_OFF, NVS_SIZE)
        pd.find_esptool = lambda: (FAKE, True)
        pd.build_nvs = fake_nvs
        self.code = 200
        po.verify = lambda url, key, dev_id: self.code
        os.environ["DEVICE_KEY_SECRET"] = SECRET

    def tearDown(self):
        pd = po.pd
        pd.salt_from_sources, pd.nvs_geometry, pd.find_esptool, pd.build_nvs, po.verify = self.saved
        os.environ.pop("DEVICE_KEY_SECRET", None)
        super().tearDown()

    def main(self, *extra):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = po.main(["COMX", MAC, "--env", "blipscope-s3-128", "--verify-url", "https://proxy.example",
                          "--log", str(self.log), *extra])
        return rc, out.getvalue()

    def test_ok_exits_zero_records_one_row_and_never_prints_the_secret(self):
        rc, out = self.main()
        self.assertEqual(rc, 0, out)
        dev_id = po.pd.device_id(MAC, SALT)
        self.assertTrue(out.strip().endswith(f"RESULT OK {dev_id}"), out)
        rows = self.log.read_text().splitlines()
        self.assertEqual(len(rows), 2)                       # header + this board
        self.assertIn(dev_id, rows[1])
        self.assertNotIn(SECRET, out)
        self.assertIsNone(re.search(r"[0-9a-f]{64}", out), "the device key must not be printed")

    def test_rejected_key_exits_nonzero_and_records_nothing(self):
        self.code = 401
        rc, out = self.main()
        self.assertEqual(rc, 1)
        self.assertIn("RESULT FAIL", out)
        self.assertFalse(self.log.exists() and self.log.stat().st_size > 0)

    def test_no_secret_refuses_before_touching_the_board(self):
        os.environ.pop("DEVICE_KEY_SECRET")
        rc, out = self.main()
        self.assertEqual(rc, 1)
        self.assertIn("DEVICE_KEY_SECRET is not set", out)
        self.assertEqual(self.calls(), [])

    def test_verify_url_is_required(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            po.main(["COMX", MAC, "--env", "blipscope-s3-128"])


class BatchUsesTheSameFunction(Rig):
    def test_batch_writes_factory_first_then_calls_provision(self):
        self.assertEqual(Path(batch.po.__file__).resolve(), Path(po.__file__).resolve())
        image = Path(self.dir) / "factory.bin"
        image.write_bytes(b"\xe9" + b"F" * 0x20)

        class Cfg: pass
        cfg = Cfg()
        cfg.esptool_cmd, cfg.dashed, cfg.baud = FAKE, True, 921600
        cfg.salt, cfg.secret, cfg.nvs_offset, cfg.nvs_size = SALT, SECRET, NVS_OFF, NVS_SIZE
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
