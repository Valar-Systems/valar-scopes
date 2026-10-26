#!/usr/bin/env python3
"""Measure what one hex-only OpenSky /states/all query costs, from OpenSky's own header.

WHY (ruled 2026-10-12). The Missileer command-post watch, on the "OpenSky -- your account"
source, sends one GET /api/states/all?icao24=...&extended=1 per poll: the user's own hexes
plus the four built-in E-4B addresses, with NO bounding box. OpenSky's docs price
/states/all by bounding-box area ("<= 25 sq deg or serial-only query" = 1 credit,
"> 400 sq deg or global" = 4) and do not say which row an icao24-only query falls in. This
script asks the server instead: it reads X-Rate-Limit-Remaining (the header name in
OpenSky's REST docs, "Limitations / API Credits") on two identical queries, and the
difference is the price of one.

The credentials come ONLY from the environment, are sent only to OpenSky's token endpoint,
and are never printed, logged or written anywhere. Only numbers are printed.

Usage (bash):
    OPENSKY_CLIENT_ID=... OPENSKY_CLIENT_SECRET=... python scripts/opensky-credit-probe.py
Usage (PowerShell):
    $env:OPENSKY_CLIENT_ID='...'; $env:OPENSKY_CLIENT_SECRET='...'; python scripts/opensky-credit-probe.py
Anonymous tier (no credentials; IP-bucketed):
    python scripts/opensky-credit-probe.py --anonymous

Each run spends two queries' worth of credits (the first only establishes the balance).
Exit: 0 measured, 1 OpenSky answered but no usable header, 2 bad usage / token failure.
"""
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

TOKEN_URL = "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token"
STATES_URL = "https://opensky-network.org/api/states/all"
# What a Missileer with an EMPTY user list sends: the four built-in E-4B addresses
# (src/eam/AbncpIdentity.h kE4bAirframes), repeated icao24 params, extended=1.
HEXES = ["adfeb3", "adfeb4", "adfeb5", "adfeb6"]


def fetch_token(client_id, client_secret):
    body = urllib.parse.urlencode({
        "grant_type": "client_credentials",
        "client_id": client_id,
        "client_secret": client_secret,
    }).encode("ascii")
    req = urllib.request.Request(TOKEN_URL, data=body, method="POST",
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r).get("access_token") or None, r.status
    except urllib.error.HTTPError as e:
        return None, e.code  # the status only: an error body is never printed
    except urllib.error.URLError:
        return None, 0


def query(token):
    params = [("icao24", h) for h in HEXES] + [("extended", "1")]
    url = STATES_URL + "?" + urllib.parse.urlencode(params)
    headers = {"Authorization": "Bearer " + token} if token else {}
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            remaining = r.headers.get("X-Rate-Limit-Remaining")
            data = json.load(r)
            states = data.get("states") or []
            return r.status, remaining, None, len(states)
    except urllib.error.HTTPError as e:
        return e.code, e.headers.get("X-Rate-Limit-Remaining"), e.headers.get("X-Rate-Limit-Retry-After-Seconds"), None
    except urllib.error.URLError:
        return 0, None, None, None


def main():
    anonymous = "--anonymous" in sys.argv[1:]
    token = None
    if not anonymous:
        cid = os.environ.get("OPENSKY_CLIENT_ID", "")
        secret = os.environ.get("OPENSKY_CLIENT_SECRET", "")
        if not cid or not secret:
            print("set OPENSKY_CLIENT_ID and OPENSKY_CLIENT_SECRET in the environment (or pass --anonymous)")
            return 2
        token, status = fetch_token(cid, secret)
        del secret
        if not token:
            print(f"token request failed: HTTP {status}")
            return 2

    print("tier:", "anonymous (IP-bucketed)" if anonymous else "authenticated (this client's account)")
    print("query: GET /api/states/all with icao24=" + ",".join(HEXES) + " (repeated params), extended=1, no bounding box")
    s1, r1, ra1, n1 = query(token)
    print(f"query 1: HTTP {s1}  X-Rate-Limit-Remaining={r1}  states={n1}" + (f"  Retry-After-Seconds={ra1}" if ra1 else ""))
    time.sleep(6)  # past the 5 s / 10 s time resolution, so the second is not a cache of the first
    s2, r2, ra2, n2 = query(token)
    print(f"query 2: HTTP {s2}  X-Rate-Limit-Remaining={r2}  states={n2}" + (f"  Retry-After-Seconds={ra2}" if ra2 else ""))
    try:
        delta = int(r1) - int(r2)
    except (TypeError, ValueError):
        print("no usable X-Rate-Limit-Remaining on both responses; cost not measured")
        return 1
    print(f"cost of one hex-only query: {delta} credits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
