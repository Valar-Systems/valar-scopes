#!/usr/bin/env python3
"""fresh-boot-wlan.py -- is the owner's spare Wi-Fi adapter there and free? Four facts, never the raw output.

The automated fresh-boot's step 2 joins the board's setup network on the PC adapter the owner named: "Wi-Fi" (his
internet is on Ethernet; "Wi-Fi 5" is a virtual copy of the same card and is never used -- the owner, 2026-11-13
06:06Z). `netsh wlan show interfaces` answers that, and it also prints the adapter's physical address (a full MAC)
and, when connected, the network's name and BSSID. None of those may reach any output. So this reads netsh
in-process and prints ONLY:

    "Wi-Fi" listed: yes|no
    "Wi-Fi" state: <one word from netsh's fixed set, or "unrecognised">
    "Wi-Fi" radio: hardware on|off|unrecognised, software on|off|unrecognised
    "Wi-Fi 5" listed: yes|no

A value outside the known set prints "unrecognised", never the value: an unexpected value is evidence about the
instrument, and printing it could print something it carries.

    python scripts/fresh-boot-wlan.py status    the four lines; exit 0, or 2 if netsh cannot be read or parsed
    python scripts/fresh-boot-wlan.py check     the four lines, then exit 0 only if "Wi-Fi" is listed, disconnected,
                                                and both radios are on (the driver's preflight); 1 otherwise; 2 blind
    python scripts/fresh-boot-wlan.py --selftest

Any exception prints its type only: a message could carry a value.
"""
import re
import subprocess
import sys

ADAPTER = "Wi-Fi"
VIRTUAL = "Wi-Fi 5"
STATES = {"connected", "disconnected", "authenticating", "associating", "discovering", "disconnecting",
          "ad hoc network formed", "not ready"}


def blocks(text):
    """The interface blocks, as {key: [values]}: a block starts at a `Name : ...` line. A line with no key (the
    radio's second line) continues the previous key."""
    out, cur, last = [], None, None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        m = re.match(r"^([^:]+?)\s*:\s?(.*)$", line)
        if m and not re.match(r"^[0-9A-Fa-f]{2}$", m.group(1)):
            key, val = m.group(1).strip().lower(), m.group(2).strip()
            if key == "name":
                cur = {}
                out.append(cur)
            if cur is not None:
                cur.setdefault(key, []).append(val)
                last = key
        elif cur is not None and last:
            cur[last].append(line)
    return out


def facts(text):
    """The four facts, or None when nothing in the text parses as an interface block (the instrument is blind)."""
    bs = blocks(text)
    if not bs:
        return None
    named = {}
    for b in bs:
        n = (b.get("name") or [""])[0]
        named.setdefault(n, b)                       # exact name: "Wi-Fi 5" never answers for "Wi-Fi"
    f = {"listed": ADAPTER in named, "virtual": VIRTUAL in named, "state": None, "hw": None, "sw": None}
    b = named.get(ADAPTER)
    if b:
        st = (b.get("state") or [""])[0].lower()
        f["state"] = st if st in STATES else "unrecognised"
        f["hw"], f["sw"] = "unrecognised", "unrecognised"
        for part in b.get("radio status", []):
            m = re.match(r"^(hardware|software)\s+(on|off)$", part.strip(), re.I)
            if m:
                f["hw" if m.group(1).lower() == "hardware" else "sw"] = m.group(2).lower()
    return f


def lines(f):
    yn = lambda v: "yes" if v else "no"
    out = ['"%s" listed: %s' % (ADAPTER, yn(f["listed"]))]
    if f["listed"]:
        out.append('"%s" state: %s' % (ADAPTER, f["state"]))
        out.append('"%s" radio: hardware %s, software %s' % (ADAPTER, f["hw"], f["sw"]))
    out.append('"%s" listed: %s' % (VIRTUAL, yn(f["virtual"])))
    return out


def usable(f):
    return f["listed"] and f["state"] == "disconnected" and f["hw"] == "on" and f["sw"] == "on"


def read_netsh():
    r = subprocess.run(["netsh", "wlan", "show", "interfaces"], capture_output=True, timeout=30)
    for enc in ("oem", "utf-8"):
        try:
            return r.stdout.decode(enc, errors="replace")
        except LookupError:
            continue
    return r.stdout.decode("latin-1")


# --------------------------------------------------------------------------------------------------------- selftest
# Synthetic only. The physical address is locally administered (02:...), which scripts/check_macs.py allows by rule
# because no factory MAC can have that shape; the network name and BSSID are <...> placeholders, which
# scripts/check_wifi.py allows. Each still carries the SENTINEL the test looks for.
_SAMPLE = """
There are 2 interfaces on the system:

    Name                   : Wi-Fi
    Description            : Example FastConnect
    GUID                   : 00000000-0000-0000-0000-000000000000
    Physical address       : 02:00:5e:fb:a0:01
    Interface type         : Primary
    State                  : %(state)s
    SSID                   : <FBA-SENTINEL-NET>
    AP BSSID               : <FBA-SENTINEL-BSSID>
    Profile                : <FBA-SENTINEL-NET>
    Radio status           : Hardware On
                             Software %(sw)s

    Name                   : Wi-Fi 5
    Description            : Example FastConnect (virtual)
    Physical address       : 02:00:5e:fb:a0:02
    Interface type         : Secondary
    State                  : connected
    SSID                   : <FBA-SENTINEL-NET>
    Radio status           : Hardware On
                             Software On

    Hosted network status  : Not available
"""


def selftest():
    bad = 0

    def ok(name, cond):
        nonlocal bad
        print(("PASS " if cond else "FAIL ") + name)
        bad += 0 if cond else 1

    s = _SAMPLE % {"state": "connected", "sw": "On"}
    ok("plant: the sample carries the sentinels and the address", "SENTINEL" in s and "02:00:5e:fb:a0:01" in s)
    out = "\n".join(lines(facts(s)))
    ok("F1 no sentinel, no address, no 'fb:a0' reaches the output", "SENTINEL" not in out and "fb:a0" not in out)
    ok("F1-control: it says something about Wi-Fi", '"Wi-Fi" listed: yes' in out and '"Wi-Fi 5" listed: yes' in out)
    f = facts(s)
    ok("F2 connected -> not usable", f["state"] == "connected" and not usable(f))
    f = facts(_SAMPLE % {"state": "disconnected", "sw": "On"})
    ok("F3 disconnected, both radios on -> usable", usable(f) and f["hw"] == "on" and f["sw"] == "on")
    f = facts(_SAMPLE % {"state": "disconnected", "sw": "Off"})
    ok("F4 software radio off (the continuation line) -> not usable", f["sw"] == "off" and not usable(f))
    only_virtual = s.split("    Name                   : Wi-Fi 5", 1)[1]
    f = facts("    Name                   : Wi-Fi 5" + only_virtual)
    ok("F5 only \"Wi-Fi 5\" present -> \"Wi-Fi\" listed: no (no prefix match)", not f["listed"] and f["virtual"])
    f = facts(_SAMPLE % {"state": "<FBA-SENTINEL-STATE>", "sw": "On"})
    out = "\n".join(lines(f))
    ok("F6 an unknown state prints 'unrecognised', never the value", f["state"] == "unrecognised" and "SENTINEL" not in out)
    ok("F7 nothing parseable -> blind (None)", facts("The Wireless AutoConfig Service (wlansvc) is not running.") is None)
    print("SELFTEST PASSED" if bad == 0 else "SELFTEST FAILED: %d" % bad)
    return 0 if bad == 0 else 1


def main(argv):
    if argv[1:] == ["--selftest"]:
        return selftest()
    if argv[1:] not in (["status"], ["check"]):
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        return 2
    try:
        f = facts(read_netsh())
    except Exception as e:  # noqa: BLE001 -- the type only
        print("netsh: unreadable (%s)" % type(e).__name__)
        return 2
    if f is None:
        print("netsh: no interface block parsed (blind: the service is off, or the output is not the expected shape)")
        return 2
    for ln in lines(f):
        print(ln)
    if argv[1] == "check":
        if usable(f):
            print('"%s": usable (listed, disconnected, both radios on)' % ADAPTER)
            return 0
        print('"%s": NOT usable for step 2 (it must be listed, disconnected, and both radios on)' % ADAPTER)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
