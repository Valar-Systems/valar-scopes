"""W5''''s local feed (docs/wx-device.md, "One synthetic feed"; W5'''' centred on E2:08's own location, ruled 2026-10-30
~18:10Z). Python standard library only.

For one announced window, E2:08's data source is `local` and its `local-url` points here. GET /data/aircraft.json is
answered from synth_feed.Feed("w5", 36) in the local feed's shape: non-ICAO contacts (the device settles enrichment
offline, so nothing leaves for them), on the W5'''' schedule from the arm's start. Any other path is 404. Nothing is
forwarded anywhere.

What it holds, only in memory and never written anywhere: the centre and range it is given (E2:08's own location and
radius, read from its page by the driver). It never logs, prints or stores a header, a query, a coordinate or an
address. Its log has five fixed fields:
    <utc> aircraft.json <served|refused|missing> <status> <n>
Every line is checked against LOG_LINE before it is written.

    python -I local_feed.py --allow-file F --log L [--port 8766] [--max-minutes 150]
    python -I local_feed.py --selftest

Commands on stdin, answered on stdout with fixed text and counts only:
    CENTRE <lat> <lon> <range_km>   set the centre and range (never echoed); until then every request is 503
    ARM                             the arm starts now: the schedule's clock restarts
    STATUS                          counts and booleans
    STOP                            close the port and exit (so do end of input and --max-minutes)
"""
import argparse
import datetime as dt
import json
import os
import queue
import re
import socket
import sys
import tempfile
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # run under -I: the feed sits beside this file
import synth_feed

PATH = "/data/aircraft.json"
LOG_LINE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z aircraft\.json (served|refused|missing) \d{3} \d+$")


def utc():
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Feed:
    def __init__(self, allow_ip, log_path):
        self.allow_ip, self.log_path = allow_ip, log_path
        self.log = open(log_path, "a", encoding="ascii", newline="\n")
        self.feed = synth_feed.Feed("w5", 36)
        self.lock = threading.Lock()
        self.centre = None            # (lat, lon, range_km), memory only
        self.t0 = time.monotonic()
        self.arms = self.served = self.refused = self.missing = 0
        self.last_n = 0

    def note(self, kind, status, n):
        line = f"{utc()} aircraft.json {kind} {status} {n}"
        if not LOG_LINE.match(line):
            raise ValueError("log line outside the fixed fields")
        with self.lock:
            self.log.write(line + "\n"); self.log.flush()
            if kind == "served": self.served += 1
            elif kind == "refused": self.refused += 1
            else: self.missing += 1

    def body(self):
        t = time.monotonic() - self.t0
        lat, lon, rk = self.centre
        ids = self.feed.w5_ids(t)
        self.last_n = len(ids)
        return json.dumps(self.feed.aircraft_json(ids, t, time.time(), lat, lon, rk), separators=(",", ":")).encode(), len(ids)

    def status(self):
        return (f"STATUS centre_set={'yes' if self.centre else 'no'} arms={self.arms} served={self.served} "
                f"refused={self.refused} missing={self.missing} last_n={self.last_n}")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a): pass
    def log_request(self, *a): pass
    def log_error(self, *a): pass

    def send(self, status, body, ctype="application/json"):
        self.send_response_only(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        F = self.server.feed
        if self.client_address[0] != F.allow_ip:
            self.close_connection = True
            F.note("refused", 403, 0)   # noted before the reply, so the count is never behind it
            self.send(403, b"")
            return
        if self.path.split("?", 1)[0] != PATH:
            F.note("missing", 404, 0)
            self.send(404, b"")
            return
        if F.centre is None:
            F.note("missing", 503, 0)
            self.send(503, b"")
            return
        data, n = F.body()
        F.note("served", 200, n)
        self.send(200, data)

    do_HEAD = do_GET


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False


def own_address_toward(ip):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((ip, 9))
    a = s.getsockname()[0]
    s.close()
    return a


def serve(F, bind, port, max_minutes):
    srv = Server((bind, port), Handler)
    srv.feed = F
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    cmds = queue.Queue()

    def reader():
        for line in sys.stdin:
            cmds.put(line.strip())
        cmds.put(None)
    threading.Thread(target=reader, daemon=True).start()
    print("LISTENING", flush=True)
    deadline = time.monotonic() + max_minutes * 60
    why = "stop"
    try:
        while True:
            try:
                cmd = cmds.get(timeout=max(0.1, min(5.0, deadline - time.monotonic())))
            except queue.Empty:
                if time.monotonic() >= deadline:
                    why = "time limit"; break
                continue
            if cmd is None:
                why = "end of input"; break
            if cmd.startswith("CENTRE "):
                try:
                    lat, lon, rk = (float(v) for v in cmd.split()[1:4])
                    assert -90 <= lat <= 90 and -180 <= lon <= 180 and 0 < rk <= 500
                    F.centre = (lat, lon, rk)
                    print("OK centre set", flush=True)
                except (ValueError, AssertionError):
                    print("CENTRE refused: not three numbers in range", flush=True)
                del cmd
            elif cmd == "ARM":
                F.t0, F.arms = time.monotonic(), F.arms + 1
                print(f"OK armed {F.arms}", flush=True)
            elif cmd == "STATUS":
                print(F.status(), flush=True)
            elif cmd == "STOP":
                break
            else:
                print("? unknown command", flush=True)
    finally:
        srv.shutdown(); srv.server_close(); F.log.close()
        print(f"STOPPED ({why})", flush=True)


def selftest():
    fails = []

    def check(ok, what):
        print(f"  {'PASS' if ok else 'FAIL'}  {what}")
        if not ok: fails.append(what)

    tmp = tempfile.mkdtemp(prefix="local-feed-selftest-")

    def start(allow):
        F = Feed(allow, os.path.join(tmp, f"log-{allow}.txt"))
        srv = Server(("127.0.0.1", 0), Handler)
        srv.feed = F
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        return F, srv, srv.server_address[1]

    def get(port, path):
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=5) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, b""

    F, srv, port = start("127.0.0.1")
    st, _ = get(port, PATH)
    check(st == 503 and F.missing == 1, "before CENTRE: 503, counted, nothing generated")
    F.centre = (44.06, -121.32, 160.0)
    F.t0 = time.monotonic() - 200          # 200 s after the arm's start: the window, n 24
    st, body = get(port, PATH)
    j = json.loads(body)
    check(st == 200 and len(j["aircraft"]) == 24 and F.last_n == 24, "200 s into the arm: 24 contacts, the schedule's first plateau")
    F.t0 = time.monotonic() - 460
    st, body = get(port, PATH)
    check(st == 200 and len(json.loads(body)["aircraft"]) == 36, "460 s into the arm: 36 contacts")
    a0 = j["aircraft"][0]
    check(int(a0["hex"], 16) >= synth_feed.NON_ICAO_LO and int(a0["hex"], 16) <= synth_feed.NON_ICAO_HI and
          a0["flight"].startswith("SYN"), "non-ICAO hexes and SYN callsigns")
    st, _ = get(port, "/data/other.json")
    check(st == 404, "any other path: 404")
    srv.shutdown(); srv.server_close(); F.log.close()
    F2, srv2, port2 = start("127.0.0.2")
    F2.centre = (44.06, -121.32, 160.0)
    st, _ = get(port2, PATH)
    check(st == 403 and F2.refused == 1 and F2.served == 0, "any other client: 403, refused, nothing served")
    srv2.shutdown(); srv2.server_close(); F2.log.close()
    lines = open(F.log_path).read().splitlines()
    check(lines and all(LOG_LINE.match(l) for l in lines), f"every log line has exactly the fixed fields ({len(lines)} lines)")
    check(not any(w in l for l in lines for w in ("44.", "121", "127.0.0.1", "?", "lat", "lon")),
          "no coordinate, query or address in the log")
    print(f"selftest: {'PASSED' if not fails else 'FAILED (' + str(len(fails)) + ')'}")
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--allow-file")
    ap.add_argument("--log")
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--max-minutes", type=float, default=150)
    a = ap.parse_args()
    if a.selftest:
        sys.exit(selftest())
    if not (a.allow_file and a.log):
        ap.error("--allow-file and --log are required")
    allow = open(a.allow_file).read().strip()
    serve(Feed(allow, a.log), own_address_toward(allow), a.port, a.max_minutes)


if __name__ == "__main__":
    main()
