#!/usr/bin/env python3
"""F1-R / F1-R-1's plant (docs/proxy-bill.md step 5): the readers against a FAKE Workers Observability API.

Synthetic data only, no network, no token: each reader is loaded from a temp copy beside a stub token_file.py, and its
`post` is replaced by the fake's, which RECORDS every event it returns. So the fetched counts below are the fake's
record, one instrument for the old reader and the new.

  python -X utf8 -I tools/bill/colo_occupancy_plant.py <old reader .py> <new reader .py> <pre-recording reader .py> <pre-classify reader .py> <pre-population reader .py> <pre-famcount reader .py> <pre-famhours reader .py> <pre-popgap reader .py>

The old reader is the one F1-R replaces (blob af55507b9ad7); the pre-recording reader is F1-R's first commit of the
change (blob 74556db07510), which kept lc's and scan's attempts in memory only (the reviewer's ruling (4)). Rows, in
the registered order: the fake's own controls, R-RED, R-PRE, R-CLEAN, R-BACKSTOP, R-SCAN, R-ABR-RED (a byte-asserted
sabotage), R-ABR, R-ABR-ABSENT, R-ABR-RECOVER, then R-REC-RED, R-REC, R-REC-SCAN-RED and R-REC-SCAN (red first), then
F1 NL-FIX's grade rows G-LABEL, G-UNION, G-EDGE and G-COLO (registration 2dd057605a8c; red on the reader before the fix,
blob 50db3cb8cb2a, at G-LABEL and G-UNION only), then F1 CLASSIFY's rows C-RED, C-TABLE, C-NOID, C-BLIND, C-PRE and
C-SRC (registration 627dd8c67b72; C-RED runs the pre-classify reader, blob 88052942e847). The proxy sources the reader
parses (proxy/src/index.ts and missileer.ts) are copied from this tree into the plant's temp tree; C-SRC gets a second
tree whose index.ts has API_PREFIX's name changed by a byte-asserted plant. Then F1 POPULATION's rows P-RED, P-NOSTEP,
P-STEP, P-ANCHOR, P-BLIND-2 and P-BLIND-5 (registration 5cf7d7288c34, corrected 75213feac918; P-RED runs the
pre-population reader, blob 9b1a1610626f), on real synthetic events so the reader's no-key filter is exercised.
Then F1 FAMILY COUNT's rows F-RED, F-IS, F-NOT, F-MID, F-ANCHOR, F-ERA, F-STABLE, F-SUM and F-BLIND (registration
ff8d7f596c0c; F-RED runs the pre-famcount reader, blob ac94f99a83d0), also on real synthetic events, so its 401, model
and starts-with leaves are exercised; every F row asserts 0 events requests.
Then F1 FAMILY HOURS' rows Q-RED, Q-FOLLOWS, Q-WRONG, Q-MID, Q-ANCHOR, Q-STABLE, Q-POP, Q-PRE, Q-BLIND-1, Q-BLIND-5 and
Q-BLIND-K (registration f522149ae7ac; Q-RED runs the pre-famhours reader, blob ba636810808b), on a synthetic window of 91
hours with the reader's clock pinned (last_closed_hour) to 2026-11-24T18:00Z; every Q row asserts 0 events requests.
Then F1 POP-GAP's rows PG-RED, PG-GAP, PG-OWN-NLOW, PG-OWN-NHIGH, PG-OWN-NSPLIT, PG-PARTIAL, PG-COLLAPSE, PG-CTL0,
PG-ANCHOR, PG-BLIND-A and PG-BLIND-W (registration 7ee3a7a57822, whose Q- names would collide with FAMILY HOURS'
Q-ANCHOR; PG-RED runs the pre-popgap reader, blob 21a83381b247), on synthetic events for the two population anchors and
the 19 window hours, with the reader's clock pinned to 2026-11-24T23:00Z; every PG row asserts 0 events requests.
Exit 0 when every row reads as predicted; 1 when any is OFF; 2 the rig (a plant that did not apply, a fake control off).
"""
import contextlib
import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile
from datetime import datetime, timedelta, timezone

SVC = "blipscope-proxy"
H = datetime(2000, 1, 1, tzinfo=timezone.utc)          # the plant's hour (synthetic)
HIS = "2000-01-01T00:00:00Z"
EXAMPLE_ID = "0" * 16                                    # the example id (CLAUDE.md), synthetic
LC_H = datetime(2026, 11, 19, 12, tzinfo=timezone.utc)  # the reader's LC_HOUR; R-REC's events there are synthetic
GATE = '    if not (isinstance(st, dict) and "abr_level" in st and _num(st["abr_level"]) and st["abr_level"] == 1):\n'
GATE_PLANT = '    if not isinstance(st, dict):   # PLANT R-ABR-RED: the ABR gate removed\n'


def ms(t):
    return int(t.timestamp() * 1000)


def ev(i, t, svc=SVC, **src):
    return {"$metadata": {"id": "e%03d" % i, "service": svc}, "timestamp": ms(t), "source": src}


def hour_h(d_in):
    t = H + timedelta(minutes=1)
    evs = [ev(1, t, evt="req", colo="ZZ1", devl="lbl-a"),
           ev(2, t, evt="req", colo="ZZ1", devl="lbl-b"),
           ev(3, t, evt="req", colo="ZZ2", devl="lbl-c"),
           ev(4, t, evt="req", colo="ZZ1"),                                  # unattributed: a colo, no label
           ev(5, t, evt="other", colo="ZZ1"),                                # not a req line
           ev(6, t, svc="some-other-worker", evt="req", colo="ZZ2", devl="lbl-x"),
           ev(8, H - timedelta(seconds=1), evt="req", colo="ZZ1", dev=EXAMPLE_ID)]   # D-before
    if d_in:
        evs.append(ev(7, t, evt="req", colo="ZZ1", dev=EXAMPLE_ID))                # D-in
    return evs


def lc_set():
    """R-REC's LC hour (the reader's LC_HOUR), synthetic: scope 5, dev 1, no dev 4, impossible 0."""
    t = LC_H + timedelta(minutes=1)
    return [ev(21, t, evt="req", colo="ZZ1", devl="lbl-a"),
            ev(22, t, evt="req", colo="ZZ1", devl="lbl-b"),
            ev(23, t, evt="req", colo="ZZ2", devl="lbl-c"),
            ev(24, t, evt="req", colo="ZZ1"),
            ev(25, t, evt="other", colo="ZZ1"),
            ev(26, t, svc="some-other-worker", evt="req", colo="ZZ2", devl="lbl-x"),
            ev(27, t, evt="req", colo="ZZ1", dev=EXAMPLE_ID)]


def scan_set():
    t = H + timedelta(minutes=1)
    h1, h2 = t - timedelta(hours=1), t - timedelta(hours=2)
    return [e for e in hour_h(False) if e["$metadata"]["id"] != "e008"] + [
        ev(10, h1, evt="req", colo="ZZ1", dev=EXAMPLE_ID),
        ev(11, h1, evt="req", colo="ZZ1", devl="lbl-a"),
        ev(12, h1, evt="req", colo="ZZ2", devl="lbl-c"),
        ev(13, h2, evt="req", colo="ZZ1", dev=EXAMPLE_ID)]


def classify_set(noid=False, notime=False):
    """F1 CLASSIFY's hour (synthetic): 10 no_label lines covering every family, status and flag; 3 labelled lines; 1 line
    with no colo; one non-req event and one other service. noid: two raw paths that must never be printed. notime: one
    no_label line with no `timestamp` (kept only as the fake's private `_ts`, so the fake still windows it)."""
    m = lambda k: H + timedelta(minutes=k)
    evs = [ev(101, m(1), evt="req", colo="ZZ1", ep="/api/v1/missileer/plant-seg-zq9" if noid else "/api/v1/missileer/status",
              status=200, model=""),
           ev(102, m(6), evt="req", colo="ZZ1", ep="/missileer", status=200, model=""),
           ev(103, m(11), evt="req", colo="ZZ2", ep="/healthz", status=200, model=""),
           ev(104, m(16), evt="req", colo="ZZ1", ep="/blipscope/enroll", status=200, model="plant-model"),
           ev(105, m(21), evt="req", colo="ZZ1", ep="/api/v1/blipscope/blips", status=401, model="plant-model"),
           ev(106, m(26), evt="req", colo="ZZ2", ep="/v1/enrich/0a0b0c" if noid else "/v1/blips", status=401, model="plant-model"),
           ev(107, m(31), evt="req", colo="ZZ1", ep="/api/v1/weatherscope/forecast", status=403, model="plant-model"),
           ev(108, m(36), evt="req", colo="ZZ2", ep="/wp-login.php", status=404, model=""),
           ev(109, m(41), evt="req", colo="ZZ1", ep="/credits", status=200, model=""),
           ev(110, m(46), evt="req", colo="ZZ1", ep="/api/v1/blipscope/config", status=200, model="plant-model", authPath="device"),
           ev(111, m(50), evt="req", colo="ZZ1", ep="/api/v1/blipscope/blips", status=200, model="plant-model", devl="lbl-a"),
           ev(112, m(50), evt="req", colo="ZZ1", ep="/api/v1/blipscope/blips", status=200, model="plant-model", devl="lbl-b"),
           ev(113, m(50), evt="req", colo="ZZ2", ep="/api/v1/blipscope/blips", status=200, model="plant-model", devl="lbl-c"),
           ev(114, m(51), evt="req", colo="", ep="/", status=200, model=""),
           ev(115, m(52), evt="other", colo="ZZ1"),
           ev(116, m(52), svc="some-other-worker", evt="req", colo="ZZ2", ep="/healthz", status=200)]
    if notime:
        evs[2]["_ts"] = evs[2].pop("timestamp")
    return evs


POP_L = datetime(2026, 11, 19, 12, tzinfo=timezone.utc)      # the reader's ANCHOR-PRE hour (L); events here are synthetic
POP_H0 = datetime(2026, 11, 23, 14, tzinfo=timezone.utc)     # the reader's ANCHOR-POST hour (H0); synthetic
POP_W0 = datetime(2026, 11, 21, 22, tzinfo=timezone.utc)     # the reader's first window hour; synthetic


def pop_set(post_nokey=249, step=0):
    """F1 POPULATION's hours, as real events so the reader's filters are exercised: L with 3695 req lines (36 no-key,
    3659 carrying the example id); H0 with 5553 (post_nokey no-key, the rest labelled); 24 window hours of 1000 req lines
    each, hour i holding 10 + i no-key (+step from the 13th hour, the site-merge hour) and the rest the example id."""
    evs, k = [], [200000]

    def add(t, n, **src):
        for j in range(n):
            k[0] += 1
            evs.append(ev(k[0], t + timedelta(seconds=j % 3500), evt="req", colo="ZZ1", **src))

    add(POP_L, 36)
    add(POP_L, 3659, dev=EXAMPLE_ID)
    add(POP_H0, post_nokey)
    add(POP_H0, 5553 - post_nokey, devl="lbl-a")
    for i in range(24):
        nk = 10 + i + (step if i >= 12 else 0)
        add(POP_W0 + timedelta(hours=i), nk)
        add(POP_W0 + timedelta(hours=i), 1000 - nk, dev=EXAMPLE_ID)
    return evs


FC_K = datetime(2026, 11, 23, 17, tzinfo=timezone.utc)      # the reader's K hour (classify17's); events here are synthetic
FC_W = [datetime(2026, 11, 22, h, tzinfo=timezone.utc) for h in (4, 5, 6)]   # the reader's window hours; synthetic
API_EPS = ("/api/v1/blipscope/blips", "/api/v1/weatherscope/forecast", "/v1/blips")   # one per API prefix


def fam_set(s05=233, s06=252, e1=100, k05=236):
    """F1 FAMILY COUNT's hours, as real events so the reader's leaves are exercised. Every line carries `model`, "" when
    it has none, as the proxy writes it (index.ts:452; AMENDMENT 1). K (17Z): 218 api-401-with-model
    no-key lines over the three prefixes and 14 other no-key lines (no 401, model ""), so each of the 401, model and
    api leaves moves its count from 232 to 218; H0: 247 + 2. The window: 04Z 2 no-key; 05Z k05 no-key, s05 of them S;
    06Z 255, s06 of them S; keyed (`dev`) lines in each, e1 of 05Z's api-with-model. 05Z's no-key lines: three at minute
    1, the rest spread over 05:05-06:00, so the first bucket with 10 or more is 05:05Z in every row that reaches it."""
    evs, k = [], [300000]

    def add(t, n, secs=None, **src):
        for j in range(n):
            k[0] += 1
            evs.append(ev(k[0], t + timedelta(seconds=secs(j) if secs else j % 3500), evt="req", colo="ZZ1", **src))

    S = dict(status=401, model="plant-model")
    for t, n_api in ((FC_K, (200, 10, 8)), (POP_H0, (247, 0, 0))):
        for ep, n in zip(API_EPS, n_api):
            add(t, n, ep=ep, **S)
    add(FC_K, 2, ep="/api/v1/missileer/status", status=200, model="")
    add(FC_K, 10, ep="/credits", status=200, model="")
    add(FC_K, 1, ep="/wp-login.php", status=404, model="")
    add(FC_K, 1, ep="/healthz", status=200, model="")
    add(FC_K, 50, ep=API_EPS[0], status=200, model="plant-model", devl="lbl-a")
    add(POP_H0, 1, ep="/missileer", status=200, model="")
    add(POP_H0, 1, ep="/wp-login.php", status=404, model="")
    add(POP_H0, 30, ep=API_EPS[0], status=200, model="plant-model", devl="lbl-a")
    add(FC_W[0], 2, ep="/credits", status=200, model="")
    add(FC_W[0], 40, ep=API_EPS[0], status=200, model="plant-model", dev=EXAMPLE_ID)
    rest = k05 - 3
    nk05 = [dict(ep=API_EPS[i % 3], **S) for i in range(s05)] + [dict(ep="/credits", status=200, model="")] * (k05 - s05)
    for j, src in enumerate(nk05):
        add(FC_W[1], 1, secs=lambda _, j=j: 60 if j < 3 else 300 + (j - 3) * 3300 // rest, **src)
    add(FC_W[1], e1, ep=API_EPS[1], status=200, model="plant-model", dev=EXAMPLE_ID)
    add(FC_W[1], 20, ep="/credits", status=200, model="", dev=EXAMPLE_ID)
    for i in range(s06):
        add(FC_W[2], 1, secs=lambda _, i=i: 60 + i * 13, ep=API_EPS[i % 3], **S)
    add(FC_W[2], 255 - s06, ep="/credits", status=200, model="")
    add(FC_W[2], 40, ep=API_EPS[2], status=200, model="plant-model", dev=EXAMPLE_ID)
    return evs


QH_FIRST = datetime(2026, 11, 21, tzinfo=timezone.utc)      # the reader's FH_FIRST; every event here is synthetic
QH_END = datetime(2026, 11, 24, 18, tzinfo=timezone.utc)     # the pinned last closed hour: N = 91
QH_LEAD = {"NIGHT": 62, "DAWN": 113, "DAY": 247, "DUSK": 218}   # the lead, with the excess seen (247, 218)


def qh_class(h):
    return "DAWN" if h.hour == 5 else "DAY" if 6 <= h.hour <= 16 else "DUSK" if h.hour == 17 else "NIGHT"


def qh_hours():
    return [QH_FIRST + timedelta(hours=i) for i in range(int((QH_END - QH_FIRST).total_seconds() // 3600) + 1)]


def qh_night_from_22z():
    """Q-WRONG's and Q-MID's hours: every NIGHT hour from 11-22 22:00Z (after the population's 24), oldest first."""
    return [h for h in qh_hours() if qh_class(h) == "NIGHT" and h >= datetime(2026, 11, 22, 22, tzinfo=timezone.utc)]


def qh_set(over=None):
    """F1 FAMILY HOURS' window as real events, so the reader's leaves are exercised: S lines (no key, 401, a model, over the
    three API prefixes) by the lead -- QUIET (0) before 11-22 05:00Z, 203 at 05:00Z, 248 at 06:00Z, then QH_LEAD by
    class -- with over = {hour: S} replacing an hour. K (11-23 17:00Z) also carries the 14 other no-key lines (no 401,
    model "") that make each of the 401, model and api leaves move K from 232 to 218; keyed 401-with-model lines sit in K
    and 05:00Z so the no-key leaves are load-bearing."""
    over = dict(over or {})
    evs, k = [], [400000]

    def add(t, n, **src):
        for j in range(n):
            k[0] += 1
            evs.append(ev(k[0], t + timedelta(seconds=(j * 13) % 3600), evt="req", colo="ZZ1", **src))

    for h in qh_hours():
        n = 0 if h < FC_W[1] else 203 if h == FC_W[1] else 248 if h == FC_W[2] else QH_LEAD[qh_class(h)]
        n = over.get(h, n)
        for i in range(n):
            add(h + timedelta(seconds=i * 13 % 3600), 1, ep=API_EPS[i % 3], status=401, model="plant-model")
    add(FC_K, 2, ep="/api/v1/missileer/status", status=200, model="")
    add(FC_K, 10, ep="/credits", status=200, model="")
    add(FC_K, 1, ep="/wp-login.php", status=404, model="")
    add(FC_K, 1, ep="/healthz", status=200, model="")
    add(FC_K, 50, ep=API_EPS[0], status=401, model="plant-model", devl="lbl-a")
    add(FC_W[1], 40, ep=API_EPS[2], status=401, model="plant-model", dev=EXAMPLE_ID)
    return evs


PG_FIRST = datetime(2026, 11, 24, 2, tzinfo=timezone.utc)    # the reader's PG_FIRST; every event here is synthetic
PG_CLOCK = datetime(2026, 11, 24, 23, tzinfo=timezone.utc)   # the pinned last closed hour: 20Z closed, before PG_LAST_START
PG_HOURS = [PG_FIRST + timedelta(hours=i) for i in range(19)]
PG_STOP = [h for h in PG_HOURS if 4 <= h.hour <= 18]


def pg_set(over=None, post_n=249):
    """F1 POP-GAP's hours as real events, so the reader's leaves are exercised. The anchors as the population's: L 36 no-key
    + 3659 keyed; H0 post_n no-key + the rest labelled (5553 in all). Each window hour holds (k, s, o): k keyed lines
    (`dev`, status 200, a model), s family lines (no key, 401, a model, over the three API prefixes), o other no-key lines
    (/credits, status 200, model ""). Default: 02Z (200, 68, 14), 03Z (200, 8, 14), the stop (180, 0, 14), 19Z and 20Z
    (200, 0, 14); over = {hour: (k, s, o)} replaces an hour."""
    over = dict(over or {})
    evs, k = [], [500000]

    def add(t, n, **src):
        for j in range(n):
            k[0] += 1
            evs.append(ev(k[0], t + timedelta(seconds=(j * 7) % 3600), evt="req", colo="ZZ1", **src))

    add(POP_L, 36)
    add(POP_L, 3659, dev=EXAMPLE_ID)
    add(POP_H0, post_n)
    add(POP_H0, 5553 - post_n, devl="lbl-a")
    for h in PG_HOURS:
        spec = (200, 68, 14) if h.hour == 2 else (200, 8, 14) if h.hour == 3 else (180, 0, 14) if h in PG_STOP else (200, 0, 14)
        kk, s, o = over.get(h, spec)
        add(h, kk, ep=API_EPS[0], status=200, model="plant-model", dev=EXAMPLE_ID)
        for i in range(s):
            add(h + timedelta(seconds=i * 11 % 3600), 1, ep=API_EPS[i % 3], status=401, model="plant-model")
        add(h, o, ep="/credits", status=200, model="")
    return evs


class Fake:
    """The API's documented shapes: timeframe; eq/exists/DOES_NOT_EXIST leaves; filterCombination and groups; events
    paging (limit, offset); calculations count. preflight: per-attempt {"abr": n or None (omitted), "count": n or None
    (the true count)} for a count query carrying `dev exists`."""

    def __init__(self, events, ignore_filters=False, counts_zero=False, preflight=None, blind_hours=(), ignore_ops=(),
                 adjust=None):
        self.events = sorted(events, key=lambda e: e["$metadata"]["id"])
        self.ignore, self.counts_zero, self.preflight = ignore_filters, counts_zero, list(preflight or [])
        self.blind_hours = set(blind_hours)      # F1 POPULATION: hour starts (ms) whose count queries answer at ABR 100
        self.ignore_ops = set(ignore_ops)        # F1 FAMILY COUNT: operations the fake silently treats as always true
        self.adjust = dict(adjust or {})         # F1 FAMILY COUNT: {(from ms, to ms): delta} added to that window's count
        self.returned, self.events_requests, self.count_requests, self.preflight_attempts = [], 0, 0, 0

    def leaf(self, e, f):
        if f.get("kind") == "group":
            return self.match(e, f)
        key, op = f["key"], f["operation"]
        if key == "$metadata.service":
            present, val = True, e["$metadata"]["service"]
        else:
            src = e.get("source") or {}
            present, val = key in src, src.get(key)
        if op in self.ignore_ops:
            return True
        if op == "starts_with":
            return present and isinstance(val, str) and val.startswith(f["value"])
        if op in ("neq", "!="):
            return present and val != f["value"]
        if op in ("eq", "="):
            return present and val == f["value"]
        if op in ("exists", "EXISTS"):
            return present
        if op == "DOES_NOT_EXIST":
            return not present
        raise ValueError("fake: operation %r not implemented" % op)

    def match(self, e, prm):
        flt = prm.get("filters") or []
        res = [self.leaf(e, f) for f in flt]
        return all(res) if str(prm.get("filterCombination", "and")).lower() == "and" else any(res)

    @staticmethod
    def is_preflight(prm):
        return any(f.get("key") == "dev" and f.get("operation") in ("exists", "EXISTS") for f in prm.get("filters") or [])

    def post(self, url, body, tok):
        if tok != "plant-token":
            raise RuntimeError("fake: not the plant's token")
        tf = body["timeframe"]
        inwin = [e for e in self.events if tf["from"] <= e.get("timestamp", e.get("_ts")) < tf["to"]]
        prm = body.get("parameters") or {}
        sel = inwin if self.ignore else [e for e in inwin if self.match(e, prm)]
        stats = {"elapsed": 1, "rows_read": len(inwin), "bytes_read": 0}
        if body.get("view") == "calculations":
            self.count_requests += 1
            n, abr = (0 if self.counts_zero else len(sel)), 1
            if self.preflight and self.is_preflight(prm):
                spec = self.preflight[min(self.preflight_attempts, len(self.preflight) - 1)]
                self.preflight_attempts += 1
                abr = spec["abr"]
                n = n if spec["count"] is None else spec["count"]
            elif self.is_preflight(prm):
                self.preflight_attempts += 1
            n += self.adjust.get((tf["from"], tf["to"]), 0)
            if tf["from"] in self.blind_hours:
                abr = 100
            if abr is not None:
                stats["abr_level"] = abr
            agg = [{"value": n, "count": n, "sampleInterval": 1 if n else 0}]
            return {"result": {"calculations": [{"aggregates": agg}], "statistics": stats}}
        self.events_requests += 1
        off = body.get("offset")
        if off:
            sel = [e for e in sel if e["$metadata"]["id"] > off]
        page = sel[:body.get("limit", 100)]
        self.returned.extend(page)
        stats["abr_level"] = 1
        return {"result": {"events": {"events": json.loads(json.dumps(page))}, "statistics": stats}}

    def with_dev(self):
        return sum(1 for e in self.returned if "dev" in (e.get("source") or {}))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def run(mod, fake, argv):
    mod.post = fake.post
    for k in ("REASK_S", "CALL_GAP_S"):
        if hasattr(mod, k):
            setattr(mod, k, 0)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mod.main(argv)
    return rc, buf.getvalue()


def main(argv):
    if len(argv) != 8:
        print(__doc__)
        return 2
    tmp = tempfile.mkdtemp(prefix="f1r-plant-")
    try:
        proxy_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "proxy", "src")
        for tree in (tmp, os.path.join(tmp, "srcplant")):
            os.makedirs(os.path.join(tree, "tools", "bill"))
            os.makedirs(os.path.join(tree, "proxy", "src"))
            with open(os.path.join(tree, "tools", "token_file.py"), "w", encoding="utf-8") as fh:
                fh.write('def token(purpose):\n    return "plant-token"\n')
            for f in ("index.ts", "missileer.ts"):
                shutil.copyfile(os.path.join(proxy_src, f), os.path.join(tree, "proxy", "src", f))
        # C-SRC's tree: API_PREFIX's name changed in its index.ts, asserted to apply exactly once and to change the file.
        idx_p = os.path.join(tmp, "srcplant", "proxy", "src", "index.ts")
        idx = open(idx_p, "rb").read()
        anchor = b"const API_PREFIX = "
        if idx.count(anchor) != 1:
            print("RIG: C-SRC's anchor is not in index.ts exactly once; the plant cannot apply")
            return 2
        idx2 = idx.replace(anchor, b"const API_PREFIX_PLANT = ")
        if idx2 == idx:
            print("RIG: C-SRC's plant did not change index.ts")
            return 2
        open(idx_p, "wb").write(idx2)
        print("plant C-SRC applied: API_PREFIX renamed once in the second tree's index.ts")
        paths = {}
        for name, src in (("old_reader", argv[0]), ("new_reader", argv[1]), ("prerec_reader", argv[2]),
                          ("precl_reader", argv[3]), ("prepop_reader", argv[4]), ("prefam_reader", argv[5]),
                          ("prefh_reader", argv[6]), ("prepg_reader", argv[7])):
            paths[name] = os.path.join(tmp, "tools", "bill", name + ".py")
            shutil.copyfile(src, paths[name])
        paths["src_reader"] = os.path.join(tmp, "srcplant", "tools", "bill", "src_reader.py")
        shutil.copyfile(argv[1], paths["src_reader"])
        new_src = open(paths["new_reader"], encoding="utf-8").read()
        if new_src.count(GATE) != 1:
            print("RIG: the ABR gate line is not in the new reader exactly once; the sabotage cannot plant")
            return 2
        sab = new_src.replace(GATE, GATE_PLANT)
        if sab == new_src:
            print("RIG: the sabotage did not change the reader")
            return 2
        paths["sab_reader"] = os.path.join(tmp, "tools", "bill", "sab_reader.py")
        with open(paths["sab_reader"], "w", encoding="utf-8", newline="\n") as fh:
            fh.write(sab)
        print("sabotage R-ABR-RED planted: the gate line replaced once; reader changed (%d -> %d bytes)" % (len(new_src), len(sab)))
        old, new, sbr, prerec, precl, srcr, prepop, prefam, prefh, prepg = (load(paths[n], n) for n in (
            "old_reader", "new_reader", "sab_reader", "prerec_reader", "precl_reader", "src_reader", "prepop_reader",
            "prefam_reader", "prefh_reader", "prepg_reader"))

        # The fake's own controls, in the new reader's own query form.
        f = Fake(hour_h(True))
        hb = {"from": ms(H), "to": ms(H + timedelta(hours=1))}
        q = lambda prm: f.post("", {"timeframe": hb, "view": "calculations", "parameters": dict(prm, calculations=[{"operator": "count"}])},
                               "plant-token")["result"]["calculations"][0]["aggregates"][0]["value"]
        c_imp, c_scope = q(new.params(new.IMPOSSIBLE, scope=False)), q(new.params())
        print("fake control: impossible AND service = %d (want 0); scope = %d (want 5)" % (c_imp, c_scope))
        if (c_imp, c_scope) != (0, 5):
            print("RIG: the fake's own controls are off; no row is run")
            return 2

        out = lambda n: os.path.join(tmp, n + ".json")
        rows, off = [], 0

        def row(name, got, want):
            nonlocal off
            ok = got == want
            off += not ok
            rows.append(name)
            print("%-14s %s\n               got  %s\n               want %s" % (name, "AS PREDICTED" if ok else "OFF", got, want))

        def hist(path):
            if not os.path.exists(path):
                return None
            d = json.load(open(path, encoding="utf-8"))
            return {k: d[k] for k in ("histogram", "req_lines", "no_colo", "no_label")}

        f = Fake(hour_h(True)); rc, _ = run(old, f, ["read", HIS, out("red")])
        row("R-RED", dict(exit=rc, returned=len(f.returned), with_dev=f.with_dev()), dict(exit=5, returned=7, with_dev=1))

        f = Fake(hour_h(True)); rc, o = run(new, f, ["read", HIS, out("pre")])
        row("R-PRE", dict(exit=rc, counts=f.count_requests, events_requests=f.events_requests, returned=len(f.returned),
                          preflight_refused="REFUSED (pre-flight)" in o),
            dict(exit=5, counts=1, events_requests=0, returned=0, preflight_refused=True))

        f = Fake(hour_h(False)); rc, o = run(new, f, ["read", HIS, out("clean")])
        row("R-CLEAN", dict(exit=rc, counts=f.count_requests, returned=len(f.returned), with_dev=f.with_dev(), read=hist(out("clean"))),
            dict(exit=0, counts=1, returned=4, with_dev=0,
                 read={"histogram": {"1": 1, "2": 1}, "req_lines": 4, "no_colo": 0, "no_label": 1}))

        f = Fake(hour_h(True), ignore_filters=True, counts_zero=True); rc, o = run(new, f, ["read", HIS, out("back")])
        row("R-BACKSTOP", dict(exit=rc, returned=len(f.returned), with_dev=f.with_dev(), backstop="REFUSED (backstop)" in o,
                               written=os.path.exists(out("back"))),
            dict(exit=5, returned=7, with_dev=1, backstop=True, written=False))

        f = Fake(scan_set()); rc, o = run(new, f, ["scan", out("scan"), HIS])
        lines = [l.strip() for l in o.splitlines()]
        row("R-SCAN", dict(exit=rc, counts=f.count_requests, events_requests=f.events_requests, returned=len(f.returned),
                           rows=[l for l in lines if " dev " in l and " devl " in l and not l.startswith("attempts ")],
                           first=[l for l in lines if l.startswith("devl first appears")],
                           clean=[l for l in lines if l.startswith("first clean hour")]),
            dict(exit=0, counts=6, events_requests=0, returned=0,
                 rows=["2000-01-01T00:00Z dev 0 devl 3", "1999-12-31T23:00Z dev 1 devl 2", "1999-12-31T22:00Z dev 1 devl 0"],
                 first=["devl first appears in the hour starting 1999-12-31T23:00Z"],
                 clean=["first clean hour 2000-01-01T00:00Z"]))

        sampled = [{"abr": 100, "count": 0}] * 3
        f = Fake(hour_h(True), preflight=sampled); rc, o = run(sbr, f, ["read", HIS, out("abrred")])
        row("R-ABR-RED", dict(exit=rc, preflight_attempts=f.preflight_attempts, returned=len(f.returned), with_dev=f.with_dev(),
                              read=hist(out("abrred"))),
            dict(exit=0, preflight_attempts=1, returned=4, with_dev=0,
                 read={"histogram": {"1": 1, "2": 1}, "req_lines": 4, "no_colo": 0, "no_label": 1}))

        f = Fake(hour_h(True), preflight=sampled); rc, o = run(new, f, ["read", HIS, out("abr")])
        row("R-ABR", dict(exit=rc, preflight_attempts=f.preflight_attempts, events_requests=f.events_requests,
                          returned=len(f.returned), written=os.path.exists(out("abr"))),
            dict(exit=6, preflight_attempts=3, events_requests=0, returned=0, written=False))

        f = Fake(hour_h(True), preflight=[{"abr": None, "count": 0}] * 3); rc, o = run(new, f, ["read", HIS, out("absent")])
        row("R-ABR-ABSENT", dict(exit=rc, preflight_attempts=f.preflight_attempts, events_requests=f.events_requests,
                                 returned=len(f.returned)),
            dict(exit=6, preflight_attempts=3, events_requests=0, returned=0))

        f = Fake(hour_h(True), preflight=[{"abr": 100, "count": 0}, {"abr": 1, "count": None}])
        rc, o = run(new, f, ["read", HIS, out("recover")])
        row("R-ABR-RECOVER", dict(exit=rc, preflight_attempts=f.preflight_attempts, events_requests=f.events_requests,
                                  preflight_count="preflight dev count 1" in o),
            dict(exit=5, preflight_attempts=2, events_requests=0, preflight_count=True))

        # R-REC (the reviewer's ruling (4)): every attempt on record. LC-2 first answers at ABR 100, then at ABR 1.
        def rec_json(path, q=None):
            if not os.path.exists(path):
                return None
            d = json.load(open(path, encoding="utf-8"))
            at = [a for a in d["attempts"] if q is None or a["query"] == q]
            return [[a["attempt"], (a["statistics"] or {}).get("abr_level"), a["value"], a.get("count")] for a in at]

        def printed(o, prefix):
            return [l.strip() for l in o.splitlines() if l.strip().startswith(prefix)]

        lc2_first_sampled = [{"abr": 100, "count": 0}, {"abr": 1, "count": None}]
        f = Fake(lc_set(), preflight=lc2_first_sampled); rc, o = run(prerec, f, ["lc"])
        row("R-REC-RED", dict(exit=rc, lc2_printed=printed(o, "attempts LC-2:"), lc2_json=None),
            dict(exit=0, lc2_printed=[], lc2_json=None))

        f = Fake(lc_set(), preflight=lc2_first_sampled); rc, o = run(new, f, ["lc", out("lcrec")])
        row("R-REC", dict(exit=rc, lc2_printed=printed(o, "attempts LC-2:"), lc2_json=rec_json(out("lcrec"), "LC-2")),
            dict(exit=0, lc2_printed=["attempts LC-2: 1: status 200 abr 100 (not a value); 2: status 200 abr 1 count 1 (value)"],
                 lc2_json=[[1, 100, False, None], [2, 1, True, 1]]))

        f = Fake(scan_set()); rc, o = run(prerec, f, ["scan", HIS])
        row("R-REC-SCAN-RED", dict(exit=rc, printed=len(printed(o, "attempts scan ")), json=None),
            dict(exit=0, printed=0, json=None))

        f = Fake(scan_set()); rc, o = run(new, f, ["scan", out("scanrec"), HIS])
        sj = rec_json(out("scanrec"))
        rows_json = json.load(open(out("scanrec"), encoding="utf-8"))["rows"] if os.path.exists(out("scanrec")) else None
        row("R-REC-SCAN", dict(exit=rc, printed=len(printed(o, "attempts scan ")),
                               json=None if sj is None else dict(attempts=len(sj), all_values_first_try=all(a[0] == 1 and a[2] for a in sj),
                                                                 rows=rows_json)),
            dict(exit=0, printed=6, json=dict(attempts=6, all_values_first_try=True,
                                              rows=[["2000-01-01T00:00Z", 0, 3], ["1999-12-31T23:00Z", 1, 2], ["1999-12-31T22:00Z", 1, 0]])))

        # G rows (F1 NL-FIX, registration 2dd057605a8c): grade() only, on synthetic result-file pairs 70 s apart, abr [1],
        # histogram {1:1, 2:1}; each pair varies only req_lines / no_colo / no_label. The frozen escape (proxy-bill.md:1413)
        # is "more than 5% of req lines without a colo OR A BOARD KEY"; the reader before the fix ran only the colo half.
        # Whole ESCAPE lines are compared, never a fragment of one.
        esc_union = "ESCAPE: over 5% of req lines without a colo or a board key -- stop and report, with no reading"
        esc_colo_old = "ESCAPE: over 5% of req lines without a colo -- stop and report, with no reading"

        def pair(tag, req, no_colo, no_label):
            ps = []
            for i, at in enumerate(("2000-01-01T02:00:00Z", "2000-01-01T02:01:10Z")):
                p = out("g-%s-%d" % (tag, i))
                with open(p, "w", encoding="utf-8", newline="\n") as fh:
                    json.dump({"hour": HIS, "read_at": at, "pages": 1, "abr": [1], "req_lines": req, "no_colo": no_colo,
                               "no_label": no_label, "preflight_dev": 0, "fetched": req, "fetched_dev": 0,
                               "histogram": {"1": 1, "2": 1}, "attempts": []}, fh)
                ps.append(p)
            return ps

        def graded(tag, req, no_colo, no_label):
            rc, o = run(new, Fake(hour_h(False)), ["grade"] + pair(tag, req, no_colo, no_label))
            return rc, [l.strip() for l in o.splitlines() if l.strip().startswith("ESCAPE:")]

        rc, esc = graded("label", 1000, 0, 51)
        row("G-LABEL", dict(exit=rc, escape=esc), dict(exit=3, escape=[esc_union]))
        rc, esc = graded("union", 1000, 30, 21)
        row("G-UNION", dict(exit=rc, escape=esc), dict(exit=3, escape=[esc_union]))
        rc, esc = graded("edge", 1000, 0, 50)
        row("G-EDGE", dict(exit=rc, escape=esc), dict(exit=0, escape=[]))
        # The control: the colo half still fires. Equal on the reader before the fix and after, so its line may be either
        # whole, known form, and nothing else.
        rc, esc = graded("colo", 1000, 51, 0)
        row("G-COLO", dict(exit=rc, escape_is_a_colo_line=len(esc) == 1 and esc[0] in (esc_union, esc_colo_old)),
            dict(exit=3, escape_is_a_colo_line=True))

        # C rows (F1 CLASSIFY, registration 627dd8c67b72): `classify`, counts only, every printed line labelled. Whole
        # table lines are compared, never a fragment of one.
        label = "[diagnostic after F1's no-reading; changes no reading]"
        table_heads = ("anchor:", "family ", "authPath present", "buckets ")

        def cl_body(o):
            lines = o.splitlines()
            labelled = bool(lines) and all(l.startswith(label + " ") for l in lines)
            return labelled, [l[len(label) + 1:] if l.startswith(label + " ") else l for l in lines]

        tab = lambda body: [l for l in body if l.startswith(table_heads)]
        z = "2xx/3xx %d 401 %d 403 %d 404 %d 429 %d other4xx %d 5xx %d | model %d | total %d"
        c_table = ["anchor: req lines 14, no colo 1, no label 10",
                   "family missileer " + z % (2, 0, 0, 0, 0, 0, 0, 0, 2),
                   "family healthz " + z % (1, 0, 0, 0, 0, 0, 0, 0, 1),
                   "family enroll " + z % (1, 0, 0, 0, 0, 0, 0, 1, 1),
                   "family api " + z % (1, 2, 1, 0, 0, 0, 0, 4, 4),
                   "family notfound " + z % (0, 0, 0, 1, 0, 0, 0, 0, 1),
                   "family page " + z % (1, 0, 0, 0, 0, 0, 0, 0, 1),
                   "authPath present 1",
                   "buckets missileer 1 1 0 0 0 0 0 0 0 0 0 0",
                   "buckets healthz 0 0 1 0 0 0 0 0 0 0 0 0",
                   "buckets enroll 0 0 0 1 0 0 0 0 0 0 0 0",
                   "buckets api 0 0 0 0 1 1 1 0 0 1 0 0",
                   "buckets notfound 0 0 0 0 0 0 0 1 0 0 0 0",
                   "buckets page 0 0 0 0 0 0 0 0 1 0 0 0"]

        rc, o = run(precl, Fake(classify_set()), ["classify", HIS, out("cred")])
        row("C-RED", dict(exit=rc, table=len(tab(cl_body(o)[1])), written=os.path.exists(out("cred"))),
            dict(exit=2, table=0, written=False))

        f = Fake(classify_set()); rc, o = run(new, f, ["classify", HIS, out("ctab")])
        lab, body = cl_body(o)
        f2 = Fake(classify_set()); run(new, f2, ["read", HIS, out("ctabread")])
        ra = hist(out("ctabread"))
        row("C-TABLE", dict(exit=rc, labelled=lab, table=tab(body),
                            read_anchor=None if ra is None else {k: ra[k] for k in ("req_lines", "no_colo", "no_label")}),
            dict(exit=0, labelled=True, table=c_table, read_anchor={"req_lines": 14, "no_colo": 1, "no_label": 10}))

        raws = ("/v1/enrich/0a0b0c", "0a0b0c", "plant-seg-zq9")
        rc, o = run(new, Fake(classify_set(noid=True)), ["classify", HIS, out("cnoid")])
        lab, body = cl_body(o)
        jb = open(out("cnoid"), "rb").read().decode("utf-8") if os.path.exists(out("cnoid")) else None
        fam = [l for l in tab(body) if l.startswith("family ")]
        total = lambda l: int(l.rsplit("total ", 1)[1])
        row("C-NOID", dict(exit=rc, raw_in_stdout=any(r in o for r in raws), raw_in_json=None if jb is None else any(r in jb for r in raws),
                           family_sum=sum(total(l) for l in fam), api_total=[total(l) for l in fam if l.startswith("family api ")]),
            dict(exit=0, raw_in_stdout=False, raw_in_json=False, family_sum=10, api_total=[4]))

        rc, o = run(new, Fake(classify_set(notime=True)), ["classify", HIS, out("cblind")])
        lab, body = cl_body(o)
        row("C-BLIND", dict(exit=rc, labelled=lab, families=len([l for l in body if l.startswith("family ")]),
                            buckets=[l for l in body if l.startswith("buckets ")]),
            dict(exit=3, labelled=True, families=6,
                 buckets=["buckets %s BLIND" % n for n in ("missileer", "healthz", "enroll", "api", "notfound", "page")]))

        f = Fake(hour_h(True)); rc, o = run(new, f, ["classify", HIS, out("cpre")])
        lab, body = cl_body(o)
        row("C-PRE", dict(exit=rc, labelled=lab, events_requests=f.events_requests,
                          refused=any(l.startswith("REFUSED (pre-flight)") for l in body), written=os.path.exists(out("cpre"))),
            dict(exit=5, labelled=True, events_requests=0, refused=True, written=False))

        f = Fake(classify_set()); rc, o = run(srcr, f, ["classify", HIS, out("csrc")])
        lab, body = cl_body(o)
        row("C-SRC", dict(exit=rc, labelled=lab, rig=any(l.startswith("RIG:") for l in body), table=len(tab(body)),
                          count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=2, labelled=True, rig=True, table=0, count_requests=0, events_requests=0))

        # P rows (F1 POPULATION, registration 5cf7d7288c34, corrected 75213feac918): count queries only, every line labelled.
        hstr = lambda t: t.strftime("%Y-%m-%dT%H:00Z")
        w_ms = lambda i: ms(POP_W0 + timedelta(hours=i))
        pa = ["ANCHOR-PRE %s: req 3695, no-key 36 (must read 3695 / 36) -> HOLDS" % hstr(POP_L),
              "ANCHOR-POST %s: req 5553, no-key 249 (must read 5553 / 249) -> HOLDS" % hstr(POP_H0)]
        anchors = lambda body: [l for l in body if l.startswith("ANCHOR-")]
        verdict = lambda body: [l for l in body if l.startswith(("verdict:", "no population:"))]

        rc, o = run(prepop, Fake(pop_set()), ["population", out("pred")])
        row("P-RED", dict(exit=rc, verdict=len(verdict(cl_body(o)[1])), written=os.path.exists(out("pred"))),
            dict(exit=2, verdict=0, written=False))

        f = Fake(pop_set()); rc, o = run(new, f, ["population", out("pnostep")])
        lab, body = cl_body(o)
        row("P-NOSTEP", dict(exit=rc, labelled=lab, anchors=anchors(body), medians=[l for l in body if l.startswith("median ")],
                             verdict=verdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=0, labelled=True, anchors=pa, medians=["median no-key before 10:00Z 15.5, from 10:00Z 27.5, step +12.0"],
                 verdict=["verdict: no step; C 0.0330"], count_requests=52, events_requests=0))

        rc, o = run(new, Fake(pop_set(step=400)), ["population", out("pstep")])
        lab, body = cl_body(o)
        row("P-STEP", dict(exit=rc, anchors=anchors(body), verdict=verdict(body)),
            dict(exit=0, anchors=pa, verdict=["verdict: step +412.0 lines/h; no C -- (c) at full volume OR another source "
                                              "that began that hour; the population cannot tell which"]))

        # Where it stopped, not what it read there: P-SAB stops here too, with another count, and must read the same.
        f = Fake(pop_set(post_nokey=248)); rc, o = run(new, f, ["population", out("panchor")])
        lab, body = cl_body(o)
        row("P-ANCHOR", dict(exit=rc, stopped_at=[l.split(" ", 1)[0] for l in anchors(body) if " -> OFF" in l],
                             hours=len([l for l in body if l.startswith("hour ")]), count_requests=f.count_requests),
            dict(exit=3, stopped_at=["ANCHOR-POST"], hours=0, count_requests=4))

        f = Fake(pop_set(), blind_hours=(w_ms(3), w_ms(15))); rc, o = run(new, f, ["population", out("pblind2")])
        lab, body = cl_body(o)
        row("P-BLIND-2", dict(exit=rc, window=[l for l in body if l.startswith("window:")],
                              blind=[l for l in body if l.startswith("hour ") and l.endswith("BLIND (dropped)")], verdict=verdict(body)),
            dict(exit=0, window=["window: 22 hour(s) read, 2 BLIND"],
                 blind=["hour %s BLIND (dropped)" % hstr(POP_W0 + timedelta(hours=i)) for i in (3, 15)],
                 verdict=["verdict: no step; C 0.0330"]))

        # It must be able to fail: the 24 attempted hours and the "no population" line, not the exit code alone.
        f = Fake(pop_set(), blind_hours=tuple(w_ms(i) for i in (1, 5, 9, 13, 17)))
        rc, o = run(new, f, ["population", out("pblind5")])
        lab, body = cl_body(o)
        row("P-BLIND-5", dict(exit=rc, attempted=len([l for l in body if l.startswith("hour ")]), verdict=verdict(body)),
            dict(exit=3, attempted=24, verdict=["no population: 5 hour(s) BLIND (more than 4)"]))

        # F rows (F1 FAMILY COUNT, registration ff8d7f596c0c): count queries only, every line labelled. Every stop row
        # checks where it stopped and how many count requests it made, never its exit alone; every row 0 events requests.
        fverdict = lambda body: [l for l in body if l.startswith("verdict:")]
        fsteps = lambda body: [l for l in body if l.startswith(("K1 ", "K2 ", "K3 ", "K4 ", "K5 ", "H ", "E1 ", "E2 ", "W ", "O "))]
        tail = " -- the instrument cannot answer at this scale: stop and decide"
        stopped = lambda at: ["verdict: OUTSIDE (stopped at %s)%s" % (at, tail)]
        onset = ["onset: %s (the first five-minute bucket with 10 or more no-key lines)" % (FC_W[1] + timedelta(minutes=5)).strftime("%Y-%m-%dT%H:%MZ")]
        hold = lambda st, t, what, got, want: "%s %s %s: %d (must read %s) -> HOLDS" % (st, hstr(t), what, got, want)
        b05 = [3] + [sum(1 for j in range(233) if 300 * (b - 1) <= j * 3300 // 233 < 300 * b) for b in range(1, 12)]
        f_steps = [hold("K1", FC_K, "no-key", 232, 232), hold("K2", FC_K, "no-key AND 401", 218, 218),
                   hold("K3", FC_K, "no-key AND model", 218, 218), hold("K4", FC_K, "no-key AND api", 218, 218),
                   hold("K5", FC_K, "S", 218, 218), hold("H", POP_H0, "S", 247, 247),
                   hold("E1", FC_W[1], "keyed AND model AND api", 100, "1..1903"), hold("E2", FC_W[1], "status 200", 123, "1..2139"),
                   hold("W", FC_W[0], "no-key", 2, 2), "W %s S: 0" % hstr(FC_W[0]),
                   hold("W", FC_W[1], "no-key", 236, 236), "W %s S: 233" % hstr(FC_W[1]),
                   hold("W", FC_W[2], "no-key", 255, 255), "W %s S: 252" % hstr(FC_W[2]),
                   "O %s buckets %s (sum 236, must read 236) -> HOLDS" % (hstr(FC_W[1]), " ".join(str(c) for c in b05))]

        rc, o = run(prefam, Fake(fam_set()), ["famcount", out("fred")])
        row("F-RED", dict(exit=rc, verdict=len(fverdict(cl_body(o)[1])), written=os.path.exists(out("fred"))),
            dict(exit=2, verdict=0, written=False))

        f = Fake(fam_set()); rc, o = run(new, f, ["famcount", out("fis")])
        lab, body = cl_body(o)
        row("F-IS", dict(exit=rc, labelled=lab, steps=fsteps(body), onset=[l for l in body if l.startswith("onset:")],
                         verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=0, labelled=True, steps=f_steps, onset=onset,
                 verdict=["verdict: IS -- the 05:00Z rise is the api-401-with-model family (S 233 of 236 at 05:00Z, 252 of 255 at 06:00Z)"],
                 count_requests=40, events_requests=0))

        f = Fake(fam_set(s05=3, s06=4)); rc, o = run(new, f, ["famcount", out("fnot")])
        lab, body = cl_body(o)
        row("F-NOT", dict(exit=rc, labelled=lab, onset=[l for l in body if l.startswith("onset:")], verdict=fverdict(body),
                          count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=0, labelled=True, onset=onset,
                 verdict=["verdict: NOT -- the 05:00Z rise is not the api-401-with-model family (S 3 of 236 at 05:00Z, 4 of 255 at 06:00Z)"],
                 count_requests=40, events_requests=0))

        f = Fake(fam_set(s05=120)); rc, o = run(new, f, ["famcount", out("fmid")])
        lab, body = cl_body(o)
        row("F-MID", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, verdict=["verdict: OUTSIDE (between the bands, or the two hours disagree; S 120 of 236 at 05:00Z, "
                                  "252 of 255 at 06:00Z)" + tail], count_requests=40, events_requests=0))

        f = Fake(fam_set(), ignore_ops=("starts_with",)); rc, o = run(new, f, ["famcount", out("fanchor")])
        lab, body = cl_body(o)
        row("F-ANCHOR", dict(exit=rc, off=[l for l in fsteps(body) if "-> OFF" in l], windows=len([l for l in body if l.startswith("W ")]),
                             verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, off=["K4 %s no-key AND api: 696 (must read 218) -> OFF: stop; nothing after it is queried" % hstr(FC_K)],
                 windows=0, verdict=stopped("K4"), count_requests=6, events_requests=0))

        f = Fake(fam_set(s05=3, s06=4, e1=0)); rc, o = run(new, f, ["famcount", out("fera")])
        lab, body = cl_body(o)
        row("F-ERA", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, verdict=stopped("E1"), count_requests=15, events_requests=0))

        f = Fake(fam_set(k05=235)); rc, o = run(new, f, ["famcount", out("fstable")])
        lab, body = cl_body(o)
        row("F-STABLE", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, verdict=stopped("W " + hstr(FC_W[1])), count_requests=21, events_requests=0))

        b30 = FC_W[1] + timedelta(minutes=30)
        f = Fake(fam_set(), adjust={(ms(b30), ms(b30 + timedelta(minutes=5))): -1}); rc, o = run(new, f, ["famcount", out("fsum")])
        lab, body = cl_body(o)
        row("F-SUM", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, verdict=stopped("O"), count_requests=40, events_requests=0))

        f = Fake(fam_set(), blind_hours=(ms(FC_W[2]),)); rc, o = run(new, f, ["famcount", out("fblind")])
        lab, body = cl_body(o)
        row("F-BLIND", dict(exit=rc, blind=[l for l in body if l.startswith("BLIND:")], verdict=fverdict(body),
                            count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=6, blind=["BLIND: W %s no-key never reached ABR 1 (3 attempt(s)); F1 does not run on it." % hstr(FC_W[2])],
                 verdict=[], count_requests=27, events_requests=0))

        # Q rows (F1 FAMILY HOURS, registration f522149ae7ac): count queries only, every line labelled, the reader's clock
        # pinned so the window is QH_FIRST..QH_END (N = 91, 6 + 3N = 279 count queries). Every row checks its exit, its
        # verdict line (which names a stop step) and its count requests, never its exit alone; every row 0 events requests.
        def qrun(mod, fake, name):
            keep = mod.last_closed_hour
            mod.last_closed_hour = lambda: QH_END
            try:
                return run(mod, fake, ["famhours", out(name)])
            finally:
                mod.last_closed_hour = keep

        qh = lambda *a: datetime(*a, tzinfo=timezone.utc)
        starts = lambda body, *p: [l for l in body if l.startswith(p)]
        q_full = lambda n: "NIGHT %d/23, DAWN 2/2, DAY 33/33, DUSK 3/3 IN" % n
        q_start = ["START: 2026-11-22T06:00Z (the first hour inside the NIGHT or DAY band)"]
        q_before = lambda bad: ["before START: 30 hour(s) read; onset hour 2026-11-22T05:00Z S 203; not QUIET (other than the "
                                "onset hour): %s" % bad]
        q_steps = [hold("K1", FC_K, "no-key", 232, 232), hold("K2", FC_K, "no-key AND 401", 218, 218),
                   hold("K3", FC_K, "no-key AND model", 218, 218), hold("K4", FC_K, "no-key AND api", 218, 218),
                   hold("M1", FC_K, "S", 218, 218), hold("M2", POP_H0, "S", 247, 247),
                   hold("M3", FC_W[1], "S", 203, 203), hold("M4", FC_W[2], "S", 248, 248)]
        q_info = ["window 2026-11-21T00:00Z to 2026-11-24T18:00Z: 91 hour(s); 279 count queries",
                  "bands (about = the owner's number +/-12%, rounded outward): NIGHT 60 52..68; DAWN 110 96..124; "
                  "DAY 240 211..269; DUSK 213 187..239",
                  "C-POP: 24 hour(s) checked, every S at most the population's no-key -> HOLDS",
                  "dropped (BLIND): none"]
        q_classes = ["class NIGHT (about 60, band 52..68): 23 graded, IN 23, HIGH 0, LOW 0",
                     "class DAWN (about 110, band 96..124): 2 graded, IN 2, HIGH 0, LOW 0",
                     "class DAY (about 240, band 211..269): 33 graded, IN 33, HIGH 0, LOW 0",
                     "class DUSK (about 213, band 187..239): 3 graded, IN 3, HIGH 0, LOW 0"]
        q_days = ["day 2026-11-22: NIGHT 6/6, DAWN 0/0, DAY 11/11, DUSK 1/1 IN",
                  "day 2026-11-23: NIGHT 11/11, DAWN 1/1, DAY 11/11, DUSK 1/1 IN",
                  "day 2026-11-24: NIGHT 6/6, DAWN 1/1, DAY 11/11, DUSK 1/1 IN"]
        q_rest = ["never read before: NIGHT 19/19, DAWN 2/2, DAY 21/21, DUSK 1/1 IN", "HIGH and LOW hours: none",
                  "QUIET runs after START: none"]
        q_prefixes = ("window ", "bands ", "C-POP:", "dropped ")

        rc, o = qrun(prefh, Fake(qh_set()), "qred")
        row("Q-RED", dict(exit=rc, verdict=len(fverdict(cl_body(o)[1])), written=os.path.exists(out("qred"))),
            dict(exit=2, verdict=0, written=False))

        f = Fake(qh_set()); rc, o = qrun(new, f, "qfollows")
        lab, body = cl_body(o)
        row("Q-FOLLOWS", dict(exit=rc, labelled=lab, steps=starts(body, "K1 ", "K2 ", "K3 ", "K4 ", "M1 ", "M2 ", "M3 ", "M4 "),
                              info=starts(body, *q_prefixes), start=starts(body, "START:"), before=starts(body, "before START:"),
                              classes=starts(body, "class "), days=starts(body, "day "),
                              rest=starts(body, "never read before:", "HIGH and LOW", "QUIET runs"), hours=len(starts(body, "hour ")),
                              verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=0, labelled=True, steps=q_steps, info=q_info, start=q_start, before=q_before("none"), classes=q_classes,
                 days=q_days, rest=q_rest, hours=87,
                 verdict=["verdict: FOLLOWS -- the hours follow the lead (START 2026-11-22T06:00Z; %s)" % q_full(23)],
                 count_requests=279, events_requests=0))

        wrong = qh_night_from_22z()
        f = Fake(qh_set({h: 247 for h in wrong})); rc, o = qrun(new, f, "qwrong")
        lab, body = cl_body(o)
        row("Q-WRONG", dict(exit=rc, night=starts(body, "class NIGHT"), high=len(starts(body, "HIGH 2")), verdict=fverdict(body),
                            count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=0, night=["class NIGHT (about 60, band 52..68): 23 graded, IN 4, HIGH 19, LOW 0"], high=19,
                 verdict=["verdict: DOES NOT FOLLOW -- the lead is wrong in NIGHT (START 2026-11-22T06:00Z; %s)" % q_full(4)],
                 count_requests=279, events_requests=0))

        f = Fake(qh_set({h: 120 for i, h in enumerate(wrong) if i % 3 == 0})); rc, o = qrun(new, f, "qmid")
        lab, body = cl_body(o)
        row("Q-MID", dict(exit=rc, night=starts(body, "class NIGHT"), verdict=fverdict(body), count_requests=f.count_requests,
                          events_requests=f.events_requests),
            dict(exit=3, night=["class NIGHT (about 60, band 52..68): 23 graded, IN 16, HIGH 7, LOW 0"],
                 verdict=["verdict: OUTSIDE (no class under 50%%, but not every class has 2 or more graded hours at 80%% or "
                          "more; %s)%s" % (q_full(16), tail)], count_requests=279, events_requests=0))

        f = Fake(qh_set(), ignore_ops=("starts_with",)); rc, o = qrun(new, f, "qanchor")
        lab, body = cl_body(o)
        row("Q-ANCHOR", dict(exit=rc, off=[l for l in body if "-> OFF" in l], hours=len(starts(body, "hour ")),
                             verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, off=["K4 %s no-key AND api: 696 (must read 218) -> OFF: stop; nothing after it is queried" % hstr(FC_K)],
                 hours=0, verdict=stopped("K4"), count_requests=6, events_requests=0))

        f = Fake(qh_set({FC_W[1]: 202})); rc, o = qrun(new, f, "qstable")
        lab, body = cl_body(o)
        row("Q-STABLE", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, verdict=stopped("M3"), count_requests=15, events_requests=0))

        f = Fake(qh_set({qh(2026, 11, 22, 1): 3})); rc, o = qrun(new, f, "qpop")
        lab, body = cl_body(o)
        row("Q-POP", dict(exit=rc, off=[l for l in body if "-> OFF" in l], verdict=fverdict(body),
                          count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, off=["C-POP 2026-11-22T01:00Z: S 3 above the population's no-key 1 -> OFF: stop; nothing after it is "
                              "queried"], verdict=stopped("C-POP 2026-11-22T01:00Z"), count_requests=96, events_requests=0))

        f = Fake(qh_set({qh(2026, 11, 21, 10): 30})); rc, o = qrun(new, f, "qpre")
        lab, body = cl_body(o)
        row("Q-PRE", dict(exit=rc, start=starts(body, "START:"), before=starts(body, "before START:"), verdict=fverdict(body),
                          count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, start=q_start, before=q_before("2026-11-21T10:00Z S 30"),
                 verdict=["verdict: OUTSIDE (an hour before START is not QUIET)" + tail], count_requests=279, events_requests=0))

        f = Fake(qh_set(), blind_hours=(ms(qh(2026, 11, 23, 3)),)); rc, o = qrun(new, f, "qblind1")
        lab, body = cl_body(o)
        row("Q-BLIND-1", dict(exit=rc, dropped=starts(body, "dropped ", "hour 2026-11-23T03:00Z"), verdict=fverdict(body),
                              count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=0, dropped=["hour 2026-11-23T03:00Z BLIND (dropped)", "dropped (BLIND): 1 hour(s): 2026-11-23T03:00Z"],
                 verdict=["verdict: FOLLOWS -- the hours follow the lead (START 2026-11-22T06:00Z; "
                          "NIGHT 22/22, DAWN 2/2, DAY 33/33, DUSK 3/3 IN)"], count_requests=279, events_requests=0))

        b5 = [qh(2026, 11, 23, 1), qh(2026, 11, 23, 2), qh(2026, 11, 23, 3), qh(2026, 11, 23, 8), qh(2026, 11, 23, 20)]
        f = Fake(qh_set(), blind_hours=[ms(h) for h in b5]); rc, o = qrun(new, f, "qblind5")
        lab, body = cl_body(o)
        row("Q-BLIND-5", dict(exit=rc, dropped=starts(body, "dropped "), verdict=fverdict(body),
                              count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, dropped=["dropped (BLIND): 5 hour(s): " + " ".join(hstr(h) for h in b5)],
                 verdict=["verdict: OUTSIDE (5 hour(s) BLIND, more than 4)" + tail], count_requests=279, events_requests=0))

        f = Fake(qh_set(), blind_hours=(ms(FC_K),)); rc, o = qrun(new, f, "qblindk")
        lab, body = cl_body(o)
        row("Q-BLIND-K", dict(exit=rc, blind=starts(body, "BLIND:"), verdict=fverdict(body),
                              count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=6, blind=["BLIND: K1 %s no-key never reached ABR 1 (3 attempt(s)); F1 does not run on it." % hstr(FC_K)],
                 verdict=[], count_requests=3, events_requests=0))

        # PG rows (F1 POP-GAP, registration 7ee3a7a57822; its Q- names collide with FAMILY HOURS' Q-ANCHOR, so PG-): count
        # queries only, every line labelled, the reader's clock pinned to PG_CLOCK. 54 count queries on a full run. Every
        # row checks its exit, its verdict line and its count requests; every row 0 events requests.
        def pgrun(mod, fake, name):
            keep = mod.last_closed_hour
            mod.last_closed_hour = lambda: PG_CLOCK
            try:
                return run(mod, fake, ["popgap", out(name)])
            finally:
                mod.last_closed_hour = keep

        pgh = lambda hr: datetime(2026, 11, 24, hr, tzinfo=timezone.utc)
        day = [h for h in PG_STOP if 6 <= h.hour <= 16]
        own = ("verdict: THE FAMILY'S OWN CHANGE, (a)-(c) -- T above 0 and K at least 50% of the controls' median in all 15 "
               "stopped hours; sub-reading ")
        n_low = ("N-LOW: N at most 50 in every stopped hour -- keyless asking stopped: (a), or (b) now keyed; "
                 "(c) excluded")
        n_high = ("N-HIGH: N at least 150 in every DAY-class stopped hour -- the device still asks with NO key and no longer "
                  "matches the family: (c), or (b) through a firmware or path change; (a) excluded")
        pg_steps = ["ANCHOR-PRE %s: T 3695, N 36 (must read 3695 / 36) -> HOLDS" % hstr(POP_L),
                    "ANCHOR-POST %s: T 5553, N 249 (must read 5553 / 249) -> HOLDS" % hstr(POP_H0),
                    "S-02 2026-11-24T02:00Z S: 68 (must read 68) -> HOLDS", "S-03 2026-11-24T03:00Z S: 8 (must read 8) -> HOLDS"]
        pg_info = ["window 2026-11-24T02:00Z to 2026-11-24T20:00Z: 19 hour(s); controls 2026-11-24T02:00Z 2026-11-24T03:00Z "
                   "2026-11-24T19:00Z 2026-11-24T20:00Z; the stop 2026-11-24T04:00Z to 2026-11-24T18:00Z; 54 count queries",
                   "controls: K 200, 200, 200, 200; median K 200.0; floor (50%) 100.0",
                   "the stop: T 194..194, N 14..14, K 180..180",
                   "S after the stop (grades nothing): 2026-11-24T19:00Z 0, 2026-11-24T20:00Z 0"]
        pg_prefixes = ("window ", "controls:", "the stop:", "S after ")

        rc, o = pgrun(prepg, Fake(pg_set()), "pgred")
        row("PG-RED", dict(exit=rc, verdict=len(fverdict(cl_body(o)[1])), written=os.path.exists(out("pgred"))),
            dict(exit=2, verdict=0, written=False))

        f = Fake(pg_set({h: (0, 0, 0) for h in PG_STOP})); rc, o = pgrun(new, f, "pggap")
        lab, body = cl_body(o)
        row("PG-GAP", dict(exit=rc, stop=starts(body, "the stop:"), verdict=fverdict(body), count_requests=f.count_requests,
                           events_requests=f.events_requests),
            dict(exit=0, stop=["the stop: T 0..0, N 0..0, K 0..0"],
                 verdict=["verdict: LOG GAP, (d) -- T reads 0 in all 15 stopped hours and above 0 in all 4 controls"],
                 count_requests=54, events_requests=0))

        f = Fake(pg_set()); rc, o = pgrun(new, f, "pgnlow")
        lab, body = cl_body(o)
        row("PG-OWN-NLOW", dict(exit=rc, labelled=lab, steps=starts(body, "ANCHOR-", "S-0"), info=starts(body, *pg_prefixes),
                                hours=len(starts(body, "hour ")), verdict=fverdict(body), count_requests=f.count_requests,
                                events_requests=f.events_requests),
            dict(exit=0, labelled=True, steps=pg_steps, info=pg_info, hours=19, verdict=[own + n_low], count_requests=54,
                 events_requests=0))

        f = Fake(pg_set({h: (180, 0, 240) for h in day})); rc, o = pgrun(new, f, "pgnhigh")
        lab, body = cl_body(o)
        row("PG-OWN-NHIGH", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests,
                                 events_requests=f.events_requests),
            dict(exit=0, verdict=[own + n_high], count_requests=54, events_requests=0))

        f = Fake(pg_set({h: (180, 0, 100) for h in day})); rc, o = pgrun(new, f, "pgnsplit")
        lab, body = cl_body(o)
        row("PG-OWN-NSPLIT", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests,
                                  events_requests=f.events_requests),
            dict(exit=0, verdict=[own + "NOT SEPARATED"], count_requests=54, events_requests=0))

        part = [h for h in PG_STOP if h.hour <= 10]
        f = Fake(pg_set({h: (0, 0, 0) for h in part})); rc, o = pgrun(new, f, "pgpartial")
        lab, body = cl_body(o)
        row("PG-PARTIAL", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests,
                               events_requests=f.events_requests),
            dict(exit=3, verdict=["verdict: OUTSIDE (7 of 15 stopped hours read T 0: %s)%s" % (
                " ".join(hstr(h) for h in part), tail)], count_requests=54, events_requests=0))

        f = Fake(pg_set({pgh(12): (80, 0, 14)})); rc, o = pgrun(new, f, "pgcollapse")
        lab, body = cl_body(o)
        row("PG-COLLAPSE", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests,
                                events_requests=f.events_requests),
            dict(exit=3, verdict=["verdict: OUTSIDE (K under the floor in 1 stopped hour(s): 2026-11-24T12:00Z)" + tail],
                 count_requests=54, events_requests=0))

        f = Fake(pg_set({pgh(20): (0, 0, 0)})); rc, o = pgrun(new, f, "pgctl0")
        lab, body = cl_body(o)
        row("PG-CTL0", dict(exit=rc, verdict=fverdict(body), count_requests=f.count_requests,
                            events_requests=f.events_requests),
            dict(exit=3, verdict=["verdict: OUTSIDE (a control hour reads T 0: 2026-11-24T20:00Z)" + tail],
                 count_requests=54, events_requests=0))

        f = Fake(pg_set(post_n=250)); rc, o = pgrun(new, f, "pganchor")
        lab, body = cl_body(o)
        row("PG-ANCHOR", dict(exit=rc, off=[l for l in body if "-> OFF" in l], hours=len(starts(body, "hour ")),
                              verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, off=["ANCHOR-POST %s: T 5553, N 250 (must read 5553 / 249) -> OFF: stop; nothing after it is "
                              "queried" % hstr(POP_H0)], hours=0, verdict=stopped("ANCHOR-POST"), count_requests=4,
                 events_requests=0))

        f = Fake(pg_set(), blind_hours=(ms(POP_L),)); rc, o = pgrun(new, f, "pgblinda")
        lab, body = cl_body(o)
        row("PG-BLIND-A", dict(exit=rc, blind=starts(body, "BLIND:"), verdict=fverdict(body), count_requests=f.count_requests,
                               events_requests=f.events_requests),
            dict(exit=6, blind=["BLIND: ANCHOR-PRE T never reached ABR 1 (3 attempt(s)); F1 does not run on it."],
                 verdict=[], count_requests=3, events_requests=0))

        f = Fake(pg_set(), blind_hours=(ms(pgh(10)),)); rc, o = pgrun(new, f, "pgblindw")
        lab, body = cl_body(o)
        row("PG-BLIND-W", dict(exit=rc, blind=[l for l in body if "BLIND" in l and l.startswith("hour ")],
                               verdict=fverdict(body), count_requests=f.count_requests, events_requests=f.events_requests),
            dict(exit=3, blind=["hour 2026-11-24T10:00Z BLIND: stop; nothing after it is queried"],
                 verdict=stopped("BLIND 2026-11-24T10:00Z"), count_requests=29, events_requests=0))

        print("rows %d, as predicted %d, OFF %d" % (len(rows), len(rows) - off, off))
        return 0 if off == 0 else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
