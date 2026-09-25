#!/usr/bin/env python3
"""mint-fixture.py -- what the BENCH used to derive, as a fixture the Worker's mint
must reproduce.

The provisioning route (proxy/src/provision.ts) replaces provision-device.py's
local derivation. Equivalence is asserted, not argued: this runs provision-device.py's
OWN device_id() and device_key() -- the code that minted every key provisioned so
far -- on fake MACs with a test secret, and proxy/test/provision.test.ts requires the
Worker to return the same id and key for each. The input comes from the other side
of the contract, so a drift in either implementation fails the suite.

Ids are stored as their SHA-256, not in the clear: a 16-hex id in the tree is what
scripts/check_device_ids.py refuses, and these are synthetic but indistinguishable.
The 64-hex key is not id-shaped.

    python scripts/mint-fixture.py           regenerate
    python scripts/mint-fixture.py --check   fail if the fixture is stale (CI)
"""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "proxy" / "test" / "fixtures" / "mint-equivalence.json"
_spec = importlib.util.spec_from_file_location("provision_device", REPO / "scripts" / "provision-device.py")
pd = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pd)

SECRET = "test-device-secret"   # the proxy suite's TEST_DEVICE_SECRET
# Fake, locally-administered unicast MACs (first octet 0x02/0x06): never a factory board.
MACS = ["02:00:00:00:00:01", "02:12:34:56:78:9a", "06:ff:ee:dd:cc:bb", "02:90:70:69:31:e2"]


def build() -> str:
    salt = pd.salt_from_sources("blipscope-s3-128")
    cases = []
    for mac in MACS:
        dev = pd.device_id(mac, salt)
        cases.append({"mac": mac, "deviceIdSha256": hashlib.sha256(dev.encode()).hexdigest(),
                      "key": pd.device_key(SECRET, dev)})
    doc = {"generatedBy": "scripts/mint-fixture.py (provision-device.py device_id + device_key)",
           "salt": salt, "secret": SECRET, "cases": cases}
    return json.dumps(doc, indent=1) + "\n"


def main() -> int:
    body = build()
    if "--check" in sys.argv:
        cur = OUT.read_text(encoding="utf-8").replace("\r\n", "\n") if OUT.exists() else ""
        if cur != body:
            print(f"{OUT} is stale -- run: python scripts/mint-fixture.py")
            return 1
        print(f"mint-equivalence.json is current ({len(MACS)} cases)")
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(body, encoding="utf-8", newline="\n")
    print(f"wrote {OUT} ({len(MACS)} cases)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
