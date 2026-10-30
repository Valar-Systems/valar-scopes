"""The weather replay proxy (docs/wx-device.md, "The replay route", RP1-RP6 and RS1-RS6). Python standard library only.

For one announced window, E2:08's `cloud-url` points here. `/api/v1/blipscope/wx/cref` is answered from the replay crop
(made by make_replay_crop.mts), re-centred on the request and stamped. Every other route is forwarded to
https://scopes.valarsystems.com with the request unchanged apart from the base URL and the `Host` that names it.

What it holds, only in memory and never written anywhere:
  - a /wx/cref request's lat/lon, for that one request, to re-centre the crop;
  - the X-Blip-Key and X-Blip-Device values seen in E2:08's requests, for the end scan (RS3). Nothing reads a key file.
It never logs, prints or stores a header, a query, a body, a coordinate or an address. Its log has five fields:
    <utc> <route> <replay|synth|forwarded|refused> <status> <bytes>
with the route one of ROUTES. Every line is checked against LOG_LINE before it is written.

    python -I proxy.py --allow-file F --crop C --log L [--port 8765] [--r-km 161] [--max-minutes 60] [--synth-k6]

--synth-k6 (K6; docs/wx-device.md, "The proxy's K6 routes"): GET /blips is answered from the synthetic feed
(synth_feed.py), re-centred on the query and honouring its limit; GET /enrich/{hex} is answered locally for exactly
the feed's 60 hexes and forwarded for any other. Both log as `synth`. The query is held for that request only.
    python -I proxy.py --selftest

`--allow-file` names the one-line file holding E2:08's bench-LAN address: the only client answered (RS2). The proxy
listens on the bench PC's own address on the route to that one. Commands arrive on stdin, one per line, and are
answered on stdout with fixed text, counts and booleans only:
    MODE old | MODE fresh   the stamp /wx/cref carries: the crop's real one (S-old) or the current minute
    STATUS                  counts per route, refusals, booleans
    SCAN <json>             RS3's scan: {"files": [...], "trace": [...]}; booleans only
    STOP                    close the port and exit (so do end of input and --max-minutes)
"""
import argparse
import ctypes
import datetime as dt
import http.client
import json
import math
import os
import queue
import re
import socket
import socketserver
import ssl
import struct
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # run under -I: the feed sits beside this file
import synth_feed

PRODUCTION = "scopes.valarsystems.com"
API = "/api/v1/blipscope"
# RFC 9110 7.6.1's hop-by-hop fields, plus the two legacy ones every proxy also drops. Any field Connection names is
# added per message (hop_names).
HOP = ("connection", "keep-alive", "proxy-authenticate", "proxy-authorization", "proxy-connection", "te", "trailer",
       "transfer-encoding", "upgrade")
ROUTES = ("wx/cref", "blips", "enrich", "config", "airports", "photo", "other")
LOG_LINE = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z (wx/cref|blips|enrich|config|airports|photo|other) "
                      r"(replay|synth|forwarded|refused) \d{3} \d+$")


def utc():
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def yn(b):
    return "yes" if b else "no"


def route_of(target):
    p = urlsplit(target).path
    if p == API + "/wx/cref":
        return "wx/cref"
    for name in ("blips", "enrich", "config", "airports", "photo"):
        if p == f"{API}/{name}" or p.startswith(f"{API}/{name}/"):
            return name
    return "other"


def hop_names(items):
    names = set(HOP)
    for n, v in items:
        if n.lower() == "connection":
            names |= {t.strip().lower() for t in v.split(",") if t.strip()}
    return names


def forward_headers(items, host):
    """The device's headers as sent, in order, less the hop-by-hop ones; `Host` names the new base. Nothing added."""
    drop = hop_names(items)
    return [(n, host) if n.lower() == "host" else (n, v) for n, v in items if n.lower() not in drop]


def back_headers(items):
    """Production's response headers as sent, in order, less the hop-by-hop ones. Nothing added."""
    drop = hop_names(items)
    return [(n, v) for n, v in items if n.lower() not in drop]


def js_round(x):
    return math.floor(x + 0.5)   # JavaScript's Math.round, which the server's quantise() uses (CLAUDE.md: pin it)


def quantise(lat, lon, r):
    """crop.ts quantise(): lat/lon to 2 dp, r to whole km, at least 1."""
    q = lambda x: js_round(x * 100) / 100
    return q(lat), q(lon), max(1, js_round(r))


class Crop:
    def __init__(self, data, r_km):
        if len(data) < 32 or data[:4] != b"WXC1" or data[4] != 1:
            raise ValueError("not a WXC1 v1 crop")
        self.data = bytes(data)
        self.frame, self.nw_lat, self.nw_lon, self.cell_lat, self.cell_lon = struct.unpack_from("<IiiII", data, 8)
        self.rows, self.cols = struct.unpack_from("<HH", data, 28)
        self.r_km = r_km

    def variant(self, lat_q, lon_q, stamp):
        """12 header bytes rewritten: the stamp at 8, and the north-west cell's centre at 12/16, placed so the crop's own
        centre sits on the request's quantised point. The cells keep their microdegree size; the body is untouched."""
        nw_lat = js_round(lat_q * 1e6 + (self.rows - 1) / 2 * self.cell_lat)
        nw_lon = js_round(lon_q * 1e6 - (self.cols - 1) / 2 * self.cell_lon)
        return self.data[:8] + struct.pack("<Iii", stamp, nw_lat, nw_lon) + self.data[20:]


class Upstream:
    """One persistent connection to production, used under a lock (the device sends one request at a time anyway)."""

    def __init__(self, host, port=443, tls=True):
        self.host, self.port, self.tls = host, port, tls
        self.ctx = ssl.create_default_context() if tls else None
        self.conn = None
        self.lock = threading.Lock()

    def _open(self):
        if self.tls:
            return http.client.HTTPSConnection(self.host, self.port, timeout=30, context=self.ctx)
        return http.client.HTTPConnection(self.host, self.port, timeout=30)

    def request(self, method, target, headers, body):
        with self.lock:
            for attempt in (0, 1):
                if self.conn is None:
                    self.conn = self._open()
                try:
                    # skip_host and skip_accept_encoding: http.client adds no header of its own (RS4).
                    self.conn.putrequest(method, target, skip_host=True, skip_accept_encoding=True)
                    for n, v in headers:
                        self.conn.putheader(n, v)
                    self.conn.endheaders(body)
                    r = self.conn.getresponse()
                    chunked = bool(r.chunked)
                    data = r.read()
                    if r.will_close:
                        self.conn.close()
                        self.conn = None
                    return r.status, r.reason, r.getheaders(), data, chunked
                except (http.client.HTTPException, OSError):
                    try:
                        self.conn.close()
                    except Exception:
                        pass
                    self.conn = None
                    # A stale keep-alive socket is retried once, and only for a request that is safe to repeat.
                    if attempt or method not in ("GET", "HEAD"):
                        raise


class Proxy:
    def __init__(self, allow_ip, crop, log_path, upstream, synth=None):
        self.allow_ip = allow_ip
        self.crop = crop
        self.mode = "old"
        self.log_path = log_path
        self.log = open(log_path, "a", encoding="ascii", newline="\n")
        self.upstream = upstream
        self.lock = threading.Lock()
        self.counts = {r: 0 for r in ROUTES}
        self.refused = self.chunked = self.r_mismatch = self.errors = 0
        self.keys, self.ids = set(), set()
        self.last_allowed = None
        self.synth, self.synth_t0, self.blips_n = synth, time.monotonic(), 0
        self.kinds = {}

    def note(self, route, kind, status, nbytes):
        line = f"{utc()} {route} {kind} {status} {nbytes}"
        if not LOG_LINE.match(line):              # a field outside the fixed set is never written
            raise ValueError("log line outside the fixed fields")
        with self.lock:
            self.log.write(line + "\n")
            self.log.flush()
            self.kinds[(route, kind)] = self.kinds.get((route, kind), 0) + 1
            if kind == "refused":
                self.refused += 1
            else:
                self.counts[route] += 1

    def status(self):
        age = "none" if self.last_allowed is None else str(int(time.monotonic() - self.last_allowed))
        per = " ".join(f"{r}={self.counts[r]}" for r in ROUTES)
        return (f"STATUS mode={self.mode} {per} refused={self.refused} chunked={self.chunked} "
                f"r_mismatch={self.r_mismatch} errors={self.errors} key_seen={yn(self.keys)} id_seen={yn(self.ids)} "
                f"since_last_request_s={age} "
                + " ".join(f"{r}_{k}={self.kinds.get((r, k), 0)}" for r in ("blips", "enrich")
                           for k in ("synth", "forwarded")))

    def scan(self, spec):
        """RS3: the key and the id only as seen in E2:08's requests, from memory; the address from the allow-list. The
        trace is the board's own serial log, which prints its own address at Wi-Fi join (src/main.cpp:479), so it is
        scanned for the key and the id only (RS2). Prints booleans and counts, never a value."""
        files, trace = list(spec.get("files", [])), list(spec.get("trace", []))
        try:
            lines = open(self.log_path, encoding="ascii", errors="replace").read().splitlines()
        except OSError:
            lines = None
        log_fixed = lines is not None and all(LOG_LINE.match(l) for l in lines)
        log_words = lines is not None and any(w in l.lower() for l in lines for w in ("lat", "lon", "x-blip", "host", "?"))
        if not self.keys:
            return f"SCAN instrument: no key seen in E2:08's requests (log fixed fields {yn(log_fixed)})"

        def forms(values):
            out = set()
            for v in values:
                for s in (v, v.lower(), v.upper(), quote(v, safe="")):
                    out.add(s.encode())
            return out

        k, i, a = forms(self.keys), forms(self.ids), {self.allow_ip.encode()}
        key = idf = addr = False
        read = missing = 0
        for path in files + trace:
            try:
                data = open(path, "rb").read()
            except OSError:
                missing += 1
                continue
            read += 1
            key |= any(n in data for n in k)
            idf |= any(n in data for n in i)
            if path not in trace:
                addr |= any(n in data for n in a)
        return (f"SCAN files_read={read} missing={missing} key_seen=yes key_found={yn(key)} id_found={yn(idf)} "
                f"address_found={yn(addr)} log_lines={len(lines or [])} log_fixed_fields={yn(log_fixed)} "
                f"log_has_lat_lon_header_or_query={yn(log_words)}")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # The stock handler logs the request line (path AND query) and the client's address to stderr. Never.
    def log_message(self, *a):
        pass

    def log_request(self, *a):
        pass

    def log_error(self, *a):
        pass

    def reply(self, status, reason, headers, body, chunked=False):
        self.send_response_only(status, reason)   # send_response would add Server and Date
        for n, v in headers:
            self.send_header(n, v)
        self.end_headers()
        if self.command == "HEAD" or status in (204, 304) or 100 <= status < 200:
            return
        if chunked:
            if body:
                self.wfile.write(b"%x\r\n" % len(body) + body + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.wfile.write(body)

    def error_json(self, status, reason, error):
        body = json.dumps({"v": 1, "error": error}, separators=(",", ":")).encode()
        self.reply(status, reason, [("Content-Type", "application/json"), ("Content-Length", str(len(body)))], body)
        return len(body)

    def handle_any(self):
        P = self.server.proxy
        items = list(self.headers.items())
        if self.client_address[0] != P.allow_ip:
            self.close_connection = True
            self.reply(403, "Forbidden", [("Content-Length", "0")], b"")
            P.note("other", "refused", 403, 0)
            return
        P.last_allowed = time.monotonic()
        for n, v in items:
            if n.lower() == "x-blip-key" and v:
                P.keys.add(v)
            elif n.lower() == "x-blip-device" and v:
                P.ids.add(v)
        route = route_of(self.path)
        if any(n.lower() == "transfer-encoding" for n, _ in items):
            # Forwarding a chunked body would need a framing header the device did not send. None is expected.
            P.chunked += 1
            self.close_connection = True
            self.reply(501, "Not Implemented", [("Content-Length", "0")], b"")
            P.note(route, "refused", 501, 0)
            return
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n > 0 else None

        if route == "wx/cref":
            q = parse_qs(urlsplit(self.path).query)
            try:
                lat_q, lon_q, r_q = quantise(float(q["lat"][0]), float(q["lon"][0]), float(q["r"][0]))
            except (KeyError, IndexError, ValueError):
                P.note(route, "replay", 400, self.error_json(400, "Bad Request", "bad_query"))
                return
            del q
            if r_q != P.crop.r_km:
                P.r_mismatch += 1
                P.note(route, "replay", 503, self.error_json(503, "Service Unavailable", "wx_stale"))
                return
            stamp = P.crop.frame if P.mode == "old" else int(time.time()) // 60 * 60
            etag = f'"replay-{stamp}"'
            hdrs = [("ETag", etag), ("X-Wx-Frame", str(stamp)), ("Cache-Control", "no-cache")]   # handler.ts's set
            if self.headers.get("If-None-Match") == etag:
                self.reply(304, "Not Modified", hdrs, b"")
                P.note(route, "replay", 304, 0)
                return
            data = P.crop.variant(lat_q, lon_q, stamp)
            del lat_q, lon_q
            self.reply(200, "OK", hdrs + [("Content-Type", "application/octet-stream"),
                                          ("Content-Length", str(len(data)))], data)
            P.note(route, "replay", 200, len(data))
            return

        if P.synth is not None and route == "blips" and self.command == "GET":
            q = parse_qs(urlsplit(self.path).query)
            try:
                lat0, lon0, r_km = float(q["lat"][0]), float(q["lon"][0]), float(q["r"][0])
                limit = int(q.get("limit", ["40"])[0])
            except (KeyError, IndexError, ValueError):
                P.note(route, "synth", 400, self.error_json(400, "Bad Request", "bad_query"))
                return
            del q
            with P.lock:
                k, P.blips_n = P.blips_n, P.blips_n + 1
            doc = P.synth.blips(P.synth.k6_ids(k, limit), time.monotonic() - P.synth_t0, time.time(), lat0, lon0, r_km)
            del lat0, lon0
            data = json.dumps(doc, separators=(",", ":")).encode()
            self.reply(200, "OK", [("Content-Type", "application/json"), ("Content-Length", str(len(data)))], data)
            P.note(route, "synth", 200, len(data))
            return
        if P.synth is not None and route == "enrich" and self.command == "GET":
            e = P.synth.enrich(urlsplit(self.path).path.rsplit("/", 1)[-1])
            if e is not None:   # exactly the feed's own hexes; any other is forwarded below
                data = json.dumps(e, separators=(",", ":")).encode()
                self.reply(200, "OK", [("Content-Type", "application/json"), ("Content-Length", str(len(data)))], data)
                P.note(route, "synth", 200, len(data))
                return

        try:
            status, reason, rh, data, chunked = P.upstream.request(self.command, self.path,
                                                                   forward_headers(items, P.upstream.host), body)
        except Exception:
            P.errors += 1
            self.close_connection = True
            self.reply(502, "Bad Gateway", [("Content-Length", "0")], b"")
            P.note(route, "forwarded", 502, 0)
            return
        out = back_headers(rh)
        if chunked:
            out.append(("Transfer-Encoding", "chunked"))   # the same framing production used, for this hop
        elif not any(h.lower() == "content-length" for h, _ in out) and status not in (204, 304) and \
                self.command != "HEAD":
            self.close_connection = True                    # close-delimited upstream: close-delimited here too
        self.reply(status, reason, out, data, chunked=chunked)
        P.note(route, "forwarded", status, len(data))

    do_GET = do_POST = do_PUT = do_HEAD = do_DELETE = do_OPTIONS = do_PATCH = handle_any


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def handle_error(self, request, client_address):
        # The stock version prints a traceback and the client's address. A fixed line instead.
        self.proxy.errors += 1
        sys.stderr.write(f"handler error: {sys.exc_info()[0].__name__}\n")


def keep_awake(on):
    """Ask Windows not to sleep while the proxy runs; released when it exits. No setting is changed."""
    if os.name == "nt":
        ES_CONTINUOUS, ES_SYSTEM_REQUIRED = 0x80000000, 0x00000001
        ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS | (ES_SYSTEM_REQUIRED if on else 0))


def own_address_toward(ip):
    """The bench PC's address on the route to `ip` (no packet is sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((ip, 9))
        return s.getsockname()[0]
    finally:
        s.close()


def serve(P, bind, port, max_minutes):
    srv = Server((bind, port), Handler)
    srv.proxy = P
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    keep_awake(True)
    cmds = queue.Queue()

    def reader():
        for line in sys.stdin:
            cmds.put(line.strip())
        cmds.put(None)   # end of input: the driver is gone, so stop
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
                    why = "time limit"
                    break
                continue
            if cmd is None:
                why = "end of input"
                break
            if cmd in ("MODE old", "MODE fresh"):
                P.mode = cmd.split()[1]
                print(f"OK mode={P.mode}", flush=True)
            elif cmd == "STATUS":
                print(P.status(), flush=True)
            elif cmd.startswith("SCAN "):
                try:
                    print(P.scan(json.loads(cmd[5:])), flush=True)
                except ValueError:
                    print("SCAN refused: not JSON", flush=True)
            elif cmd == "STOP":
                break
            else:
                print("? unknown command", flush=True)
    finally:
        srv.shutdown()
        srv.server_close()
        P.log.close()
        keep_awake(False)
        print(f"STOPPED ({why})", flush=True)


# ---------------------------------------------------------------------------------------------------------- selftest

class _Echo(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def _echo_server(seen, chunked):
    """A plain-HTTP stand-in for production: records the header names (and the Host value) of each request, exactly as
    they arrive on the socket, and answers with a fixed set of headers, chunked or not."""
    class H(socketserver.StreamRequestHandler):
        def handle(self):
            while True:
                line = self.rfile.readline()
                if not line:
                    return
                names, host, length = [], None, 0
                while True:
                    h = self.rfile.readline()
                    if h in (b"\r\n", b"\n", b""):
                        break
                    name, _, val = h.decode("latin-1").partition(":")
                    names.append(name)
                    if name.lower() == "host":
                        host = val.strip()
                    if name.lower() == "content-length":
                        length = int(val)
                body = self.rfile.read(length) if length else b""
                seen.append({"line": line.decode("latin-1").strip(), "names": names, "host": host, "body": body})
                payload = b'{"v":1,"a":[]}'
                head = [b"HTTP/1.1 200 OK", b"Content-Type: application/json", b"Connection: keep-alive",
                        b"Keep-Alive: timeout=5", b"X-Test: 1"]
                if chunked:
                    head.append(b"Transfer-Encoding: chunked")
                    out = b"\r\n".join(head) + b"\r\n\r\n" + b"%x\r\n" % len(payload) + payload + b"\r\n0\r\n\r\n"
                else:
                    head.append(b"Content-Length: %d" % len(payload))
                    out = b"\r\n".join(head) + b"\r\n\r\n" + payload
                self.wfile.write(out)
                self.wfile.flush()
    srv = _Echo(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def _ask(port, method, target, headers, body=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.putrequest(method, target, skip_host=True, skip_accept_encoding=True)
    for n, v in headers:
        c.putheader(n, v)
    c.endheaders(body)
    r = c.getresponse()
    data = r.read()
    out = (r.status, [n for n, _ in r.getheaders()], dict(r.getheaders()), data)
    c.close()
    return out


def _start(P):
    srv = Server(("127.0.0.1", 0), Handler)
    srv.proxy = P
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


def selftest():
    fails = []

    def check(ok, what):
        print(f"  {'PASS' if ok else 'FAIL'}  {what}")
        if not ok:
            fails.append(what)

    FAKE_KEY = "FAKE-KEY-selftest-not-a-real-key"     # EXAMPLE -- not a real key
    FAKE_ID = "0000000000000000"                       # EXAMPLE -- not a real device
    SENT = [("Host", "bench.invalid:8765"), ("User-Agent", "ESP32HTTPClient"), ("Connection", "keep-alive, X-Drop-Me"),
            ("X-Drop-Me", "1"), ("Accept-Encoding", "identity;q=1,chunked;q=0.1,*;q=0"), ("X-Blip-Key", FAKE_KEY),
            ("X-Blip-Model", "s3-128"), ("X-Blip-FW", "18"), ("X-Blip-Device", FAKE_ID)]
    WANT = ["Host", "User-Agent", "Accept-Encoding", "X-Blip-Key", "X-Blip-Model", "X-Blip-FW", "X-Blip-Device"]

    print("routes and headers")
    check([route_of(API + p) for p in ("/wx/cref?lat=1&lon=2&r=3", "/blips?lat=1", "/enrich/abc123", "/config",
                                       "/airports?lat=1", "/photo/k", "/leaderboard", "/wx/crefx")] ==
          ["wx/cref", "blips", "enrich", "config", "airports", "photo", "other", "other"], "route_of: every device path")
    fh = forward_headers(SENT, PRODUCTION)
    check([n for n, _ in fh] == WANT, "forward_headers: the names sent, less the hop-by-hop ones and those Connection names")
    check(dict(fh)["Host"] == PRODUCTION and dict(fh)["X-Blip-Key"] == FAKE_KEY, "forward_headers: Host renamed, the rest unchanged")
    check(not any(n.lower() in ("x-forwarded-for", "via", "forwarded") for n, _ in fh), "forward_headers: no proxy header added")

    tmp = tempfile.mkdtemp(prefix="wx-replay-selftest-")
    # A minimal valid WXC1: 3 x 4 cells of code 1 (one run, 0x1C), NW centre and cell size arbitrary.
    hdr = b"WXC1" + bytes([1, 1, 0, 0]) + struct.pack("<IiiIIHH", 1717358520, 13000000, -46000000, 10000, 14000, 3, 4)
    crop = Crop(hdr + b"\x1c", 161)

    for chunked in (False, True):
        seen = []
        echo = _echo_server(seen, chunked)
        log = os.path.join(tmp, f"log-{int(chunked)}.txt")
        P = Proxy("127.0.0.1", crop, log, Upstream("127.0.0.1", echo.server_address[1], tls=False))
        srv, port = _start(P)
        print(f"end to end, upstream {'chunked' if chunked else 'with Content-Length'}")
        st, names, h, data = _ask(port, "GET", API + "/blips?lat=1.5&lon=2.5&r=161", SENT)
        got = seen[-1] if seen else {"names": [], "host": None, "line": ""}
        check(st == 200 and data == b'{"v":1,"a":[]}', "GET /blips: 200 and production's body")
        check(got["names"] == WANT, "upstream received exactly the names sent, less the hop-by-hop ones")
        check(got["host"] == "127.0.0.1", "upstream's Host is the base's (here the stand-in's)")
        check(got["line"] == f"GET {API}/blips?lat=1.5&lon=2.5&r=161 HTTP/1.1", "method, path and query unchanged")
        want_back = ["Content-Type", "X-Test"] + (["Transfer-Encoding"] if chunked else ["Content-Length"])
        check(names == want_back, "the response's names are production's, less hop-by-hop, framed for this hop")
        check(not any(n.lower() in ("server", "date", "via") for n in names), "no Server, Date or Via of the proxy's")
        st, _, _, _ = _ask(port, "POST", API + "/leaderboard", SENT + [("Content-Length", "11")], b"hello world")
        check(st == 200 and seen[-1]["body"] == b"hello world", "POST: the body forwarded byte for byte")
        srv.shutdown(); srv.server_close(); echo.shutdown(); echo.server_close(); P.log.close()

    print("refusals, the replay crop, the log, the scan")
    seen = []
    echo = _echo_server(seen, False)
    log = os.path.join(tmp, "log-r.txt")
    P = Proxy("127.0.0.1", crop, log, Upstream("127.0.0.1", echo.server_address[1], tls=False))
    srv, port = _start(P)
    st, _, _, _ = _ask(port, "POST", API + "/blips", SENT + [("Transfer-Encoding", "chunked")], b"0\r\n\r\n")
    check(st == 501 and P.chunked == 1 and not seen, "a chunked request body: 501, counted, not forwarded")
    P.mode = "old"
    q = "/wx/cref?lat=12.3456&lon=-45.6789&r=161.2"
    st, _, h, old = _ask(port, "GET", API + q, SENT)
    check(st == 200 and h.get("ETag") == '"replay-1717358520"' and h.get("X-Wx-Frame") == "1717358520"
          and h.get("Cache-Control") == "no-cache" and h.get("Content-Type") == "application/octet-stream",
          "S-old: 200 with handler.ts's headers and the crop's own stamp")
    diff = [i for i in range(max(len(old), len(crop.data))) if old[i:i + 1] != crop.data[i:i + 1]]
    check(len(old) == len(crop.data) and diff and min(diff) >= 12 and max(diff) <= 19, "S-old differs from the crop only in bytes 12-19")
    la, lo, ch, cw = struct.unpack_from("<iiII", old, 12)
    r_ = struct.unpack_from("<H", old, 28)[0]
    c_ = struct.unpack_from("<H", old, 30)[0]
    check(abs((la - (r_ - 1) / 2 * ch) - 12.35e6) <= 1 and abs((lo + (c_ - 1) / 2 * cw) - (-45.68e6)) <= 1,
          "re-centred: the crop's own centre on the request's quantised point, within 1 microdegree")
    st, _, _, _ = _ask(port, "GET", API + q, SENT + [("If-None-Match", '"replay-1717358520"')])
    check(st == 304, "If-None-Match on the replay's ETag: 304")
    P.mode = "fresh"
    st, _, h, fresh = _ask(port, "GET", API + q, SENT)
    stamp = struct.unpack_from("<I", fresh, 8)[0]
    check(st == 200 and stamp % 60 == 0 and abs(time.time() - stamp) < 120 and h.get("ETag") == f'"replay-{stamp}"',
          "fresh: stamped to the current minute, and the ETag follows it")
    d2 = [i for i in range(len(fresh)) if fresh[i] != old[i]]
    check(d2 and min(d2) >= 8 and max(d2) <= 11, "S-old and the fresh crop differ only in bytes 8-11")
    st, _, _, _ = _ask(port, "GET", API + "/wx/cref?lat=12.35&lon=-45.68&r=100", SENT)
    check(st == 503 and P.r_mismatch == 1, "another range: 503 and counted, never a wrong-sized crop")
    srv.shutdown(); srv.server_close()

    P2 = Proxy("127.0.0.2", crop, os.path.join(tmp, "log-other.txt"), Upstream("127.0.0.1", echo.server_address[1], tls=False))
    srv2, port2 = _start(P2)
    before = len(seen)
    st, _, _, _ = _ask(port2, "GET", API + "/blips?lat=1&lon=2&r=3", SENT)
    lines2 = open(P2.log_path).read().split()
    check(st == 403 and len(seen) == before and lines2[1:] == ["other", "refused", "403", "0"],
          "any other client: 403, an other/refused line, and nothing forwarded")
    check(not P2.keys, "a refused request's headers are not even read")
    srv2.shutdown(); srv2.server_close(); P2.log.close(); echo.shutdown(); echo.server_close()

    P.log.flush()
    lines = open(log).read().splitlines()
    check(lines and all(LOG_LINE.match(l) for l in lines), f"every log line has exactly the fixed fields ({len(lines)} lines)")
    check(not any(w in l.lower() for l in lines for w in ("lat", "lon", "x-blip", "host", "?", "127.0.0.1")),
          "no coordinate, query, header or address in the log")
    clean = os.path.join(tmp, "clean.txt")
    open(clean, "w").write("nothing to see\n")
    planted = os.path.join(tmp, "planted.txt")
    open(planted, "w").write("x " + FAKE_KEY + " x\n")
    addr = os.path.join(tmp, "addr.txt")
    open(addr, "w").write("[WiFi] CONNECTED  IP=127.0.0.1\n")
    s = P.scan({"files": [clean, log]})
    check("key_found=no" in s and "id_found=no" in s and "address_found=no" in s and "log_fixed_fields=yes" in s,
          "the scan, clean files: key, id and address not found")
    check("key_found=yes" in P.scan({"files": [clean, planted]}), "the scan, the key planted: found")
    check("address_found=yes" in P.scan({"files": [addr]}), "the scan, the address in a file: found")
    check("address_found=no" in P.scan({"files": [clean], "trace": [addr]}), "the scan, the address only in the trace: not graded")
    print("K6: the synthetic routes (--synth-k6)")
    seen = []
    echo = _echo_server(seen, False)
    feed = synth_feed.Feed("k6", 60)
    PK = Proxy("127.0.0.1", crop, os.path.join(tmp, "log-k6.txt"), Upstream("127.0.0.1", echo.server_address[1], tls=False),
               synth=feed)
    srvk, portk = _start(PK)
    q = "/blips?lat=44.06&lon=-121.32&r=160&limit=40"
    st, _, _, d1 = _ask(portk, "GET", API + q, SENT)
    st2, _, _, d2 = _ask(portk, "GET", API + q, SENT)
    j1, j2 = json.loads(d1), json.loads(d2)
    check(st == 200 and j1["v"] == 1 and len(j1["a"]) == 40 and not seen, "K6 /blips: 200, v 1, 40 rows, nothing forwarded")
    check(st2 == 200 and j2["a"][0][0] == feed.c[20]["hex"], "K6 /blips again: the other set (its first row is contact 20)")
    st, _, _, de = _ask(portk, "GET", API + "/enrich/" + feed.c[7]["hex"] + "?cs=X&lat=1&lon=2", SENT)
    je = json.loads(de)
    check(st == 200 and je["t"] and je["op"] and not seen, "nothing forwarded for a K6 hex: its enrich is answered locally")
    st, _, _, _ = _ask(portk, "GET", API + "/enrich/abcdef", SENT)
    check(st == 200 and len(seen) == 1, "any other hex's enrich is forwarded")
    PK.log.flush()
    kl = open(PK.log_path).read().splitlines()
    check([l.split()[1:3] for l in kl] == [["blips", "synth"], ["blips", "synth"], ["enrich", "synth"], ["enrich", "forwarded"]]
          and all(LOG_LINE.match(l) for l in kl) and not any(w in l.lower() for l in kl for w in ("lat", "lon", "?", "x-blip")),
          "the K6 log: blips synth, enrich synth, enrich forwarded; fixed fields, no coordinate, query or header")
    check("blips_synth=2 blips_forwarded=0 enrich_synth=1 enrich_forwarded=1" in PK.status(), "STATUS counts each route by disposition")
    srvk.shutdown(); srvk.server_close(); echo.shutdown(); echo.server_close(); PK.log.close()
    check(re.match(r"^SCAN files_read=\d+ missing=\d+ key_seen=yes key_found=(yes|no) id_found=(yes|no) "
                   r"address_found=(yes|no) log_lines=\d+ log_fixed_fields=(yes|no) "
                   r"log_has_lat_lon_header_or_query=(yes|no)$", P.scan({"files": [clean]})) is not None,
          "the scan prints counts and booleans, and nothing else")
    empty = Proxy("127.0.0.1", crop, os.path.join(tmp, "log-e.txt"), None)
    check(empty.scan({"files": [planted]}).startswith("SCAN instrument"), "no key seen: 'instrument', not clean")
    empty.log.close()
    P.log.close()
    print(f"selftest: {'PASSED' if not fails else 'FAILED (' + str(len(fails)) + ')'}")
    return 0 if not fails else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--allow-file")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--crop")
    ap.add_argument("--r-km", type=int, default=161)
    ap.add_argument("--log")
    ap.add_argument("--max-minutes", type=float, default=60)
    ap.add_argument("--synth-k6", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        sys.exit(selftest())
    if not (a.allow_file and a.crop and a.log):
        ap.error("--allow-file, --crop and --log are required")
    allow = open(a.allow_file).read().strip()
    crop = Crop(open(a.crop, "rb").read(), a.r_km)
    P = Proxy(allow, crop, a.log, Upstream(PRODUCTION), synth=synth_feed.Feed("k6", 60) if a.synth_k6 else None)
    serve(P, own_address_toward(allow), a.port, a.max_minutes)


if __name__ == "__main__":
    main()
