#!/usr/bin/env python3
"""E6 long-message fixtures: generate them, and serve them to a bench Missileer on the LAN.

WHY THIS EXISTS (ROADMAP E6, 2026-10-11). EAMs of 246 and 290 characters are reported
and valar-eam-feed accepts copies up to MAX_COPY = 500 (src/sources/eam/corrections.ts).
The firmware has only ever been fed ~30-character messages. This script produces the
/eam/latest bodies the long-message check runs on, in ONE place, so the host test
(test/host/test_eam_long_message.cpp) and the device (env missileer-s3-128-e6bench)
parse the same bytes:

    python scripts/e6-fixture-server.py --generate test/fixtures/eam-long
        writes latest-031.json, latest-075.json, latest-290.json, latest-500.json
        (deterministic: re-running produces identical files; --check DIR compares)

    python scripts/e6-fixture-server.py
        serves test/fixtures/eam-long on http://<this PC's LAN IP>:8765, and prints
        the E6_FIXTURE_BASE value the bench env is built with

THE SHAPE is valar-eam-feed's /eam/latest as served today (src/routes/eam.ts:
{fetched_at, messages: Msg[], on_watch}; src/types.ts Msg; src/normalize.ts for
groups/char_count). Every message is a STATION copy: the station stores the copy
without spaces, so `groups` is a single group equal to `text` (test/public-export
.test.ts and test/eam-reconcile.test.ts build their rows the same way). Message 0 is
the long one; messages 1..19 are ordinary 30-character copies. The long message's
id is fixed per length (eam-e6len031 ...), so a device polling the rotation keeps
at most 19 + 4 messages: bounded, and the same steady state the host test predicts.

SERVING. GET /eam/latest answers the four files in turn (031, 075, 290, 500, 031,
...), one per request. Every other path is a 404 -- in particular
/api/v1/missileer/config, so a bench device NEVER holds a game config and can never
offer a drill (DrillPolicy.h DecideOffer: NoConfig). The body is sent CHUNKED with
keep-alive by default, because that is what the production backend sends and what
the firmware's ChunkedSocketStream path was written for (HttpRequestManager.cpp);
--content-length switches to the other path.

Standard library only. Nothing here writes to the device, the backend or the network
beyond answering the requests it receives.
"""
import argparse
import json
import os
import socket
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LENGTHS = (31, 75, 290, 500)       # ROADMAP E6: heard 32-75; reported 246, 290; cap 500
FILLERS = 19                       # /eam/latest?limit=20 -> 20 messages, one of them long
ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"   # corrections.ts ALPHABET minus '?'
FREQS = (11175, 8992, 4724, 15016)  # HFGCS primaries, as /eam/stats by_freq names them


def lcg(seed):
    """A tiny deterministic generator: same seed, same copy, on every machine and Python."""
    x = seed & 0xFFFFFFFF
    while True:
        x = (1103515245 * x + 12345) & 0x7FFFFFFF
        yield x


def copy_of(length, seed):
    g = lcg(seed)
    return "".join(ALPHABET[next(g) % len(ALPHABET)] for _ in range(length))


def iso(minutes_before_noon):
    """heard_at as the backend serves it: ISO-8601 UTC with milliseconds and Z."""
    total = 12 * 60 - minutes_before_noon
    day = 11
    while total < 0:
        total += 24 * 60
        day -= 1
    return "2026-10-%02dT%02d:%02d:00.000Z" % (day, total // 60, total % 60)


def station_msg(msg_id, text, minutes_before_noon, freq, extra=None):
    m = {
        "id": msg_id,
        "type": "eam",
        "text": text,
        "groups": [text],          # a station copy has no spaces: one group == text
        "char_count": len(text),   # NON-space characters (normalize.ts countNonSpace)
        "group_count": 1,
        "heard_at": iso(minutes_before_noon),
        "frequency_khz": freq,
        "malformed": False,        # fewer than 3 groups: never flagged (normalize.ts)
        "source": "station",
        "preamble": text[:6],
    }
    if extra:
        m.update(extra)
    return m


def payload(length):
    msgs = [station_msg(
        "eam-e6len%03d" % length,
        copy_of(length, 1000 + length),
        3,
        11175,
        {"duration_sec": round(length * 0.42, 1), "reception_count": 2,
         "repeat_span_min": 4, "repeat_last_at": iso(-1)},
    )]
    for i in range(FILLERS):
        seed = 77 + i
        msg_id = "eam-%08x" % (next(lcg(seed * 7919)) & 0xFFFFFFFF)
        msgs.append(station_msg(msg_id, copy_of(30, seed), 40 + 37 * i, FREQS[i % len(FREQS)]))
    body = {"fetched_at": "2026-10-11T12:00:05.000Z", "messages": msgs, "on_watch": 3}
    # Compact, like Fastify's JSON.stringify -- the byte count is part of what is measured.
    return (json.dumps(body, separators=(",", ":")) + "\n").encode("ascii")


def fixture_name(length):
    return "latest-%03d.json" % length


def generate(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for n in LENGTHS:
        path = os.path.join(out_dir, fixture_name(n))
        with open(path, "wb") as f:
            f.write(payload(n))
        print("wrote %s (%d bytes)" % (path, len(payload(n))))


def check(out_dir):
    bad = 0
    for n in LENGTHS:
        path = os.path.join(out_dir, fixture_name(n))
        try:
            with open(path, "rb") as f:
                on_disk = f.read()
        except OSError as e:
            print("MISSING %s (%s)" % (path, e))
            bad += 1
            continue
        same = on_disk == payload(n)
        print("%s %s" % ("ok     " if same else "DIFFERS", path))
        bad += 0 if same else 1
    return 1 if bad else 0


def lan_ip():
    """The address this PC would use to reach the LAN. A UDP connect sends nothing."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))   # TEST-NET-1: never routed, never contacted
        return s.getsockname()[0]
    finally:
        s.close()


def serve(fixture_dir, port, content_length):
    bodies = []
    for n in LENGTHS:
        with open(os.path.join(fixture_dir, fixture_name(n)), "rb") as f:
            bodies.append((n, f.read()))
    state = {"next": 0, "served": 0}

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"   # keep-alive, and chunked encoding is legal

        def log_message(self, fmt, *args):
            pass   # one line per request, below, instead of the default access log

        def send_body(self, code, body):
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            if content_length:
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            else:
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for i in range(0, len(body), 1024):   # several chunks, like a real reply
                    part = body[i:i + 1024]
                    self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
            self.wfile.flush()

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/eam/latest":
                n, body = bodies[state["next"] % len(bodies)]
                state["next"] += 1
                state["served"] += 1
                print("[e6-fixture] #%d GET %s -> latest-%03d.json (%d bytes, %s) from %s"
                      % (state["served"], self.path, n, len(body),
                         "content-length" if content_length else "chunked",
                         self.client_address[0]), flush=True)
                self.send_body(200, body)
            else:
                print("[e6-fixture] GET %s -> 404" % self.path, flush=True)
                self.send_body(404, b'{"error":"not served by the E6 fixture server"}\n')

    ip = lan_ip()
    httpd = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    print("[e6-fixture] serving %s on http://%s:%d  (%s)"
          % (fixture_dir, ip, port, "content-length" if content_length else "chunked"))
    print("[e6-fixture] build the bench image with, in PowerShell:")
    print("    $env:E6_FIXTURE_BASE = 'http://%s:%d'" % (ip, port))
    print("[e6-fixture] Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    print("[e6-fixture] stopped after %d /eam/latest responses" % state["served"])


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_dir = os.path.join(here, "..", "test", "fixtures", "eam-long")
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--generate", metavar="DIR", help="write the fixtures to DIR and exit")
    ap.add_argument("--check", metavar="DIR", help="exit 1 unless DIR holds exactly what --generate writes")
    ap.add_argument("--dir", default=default_dir, help="fixtures to serve (default: test/fixtures/eam-long)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--content-length", action="store_true",
                    help="send Content-Length instead of chunked (the production backend sends chunked)")
    a = ap.parse_args()
    if a.generate:
        generate(a.generate)
        return 0
    if a.check:
        return check(a.check)
    serve(os.path.normpath(a.dir), a.port, a.content_length)
    return 0


if __name__ == "__main__":
    sys.exit(main())
