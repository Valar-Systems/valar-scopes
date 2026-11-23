#!/usr/bin/env python3
"""F1-R / F1-R-1's plant (docs/proxy-bill.md step 5): the readers against a FAKE Workers Observability API.

Synthetic data only, no network, no token: each reader is loaded from a temp copy beside a stub token_file.py, and its
`post` is replaced by the fake's, which RECORDS every event it returns. So the fetched counts below are the fake's
record, one instrument for the old reader and the new.

  python -X utf8 -I tools/bill/colo_occupancy_plant.py <old reader .py> <new reader .py> <pre-recording reader .py> <pre-classify reader .py> <pre-population reader .py>

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


class Fake:
    """The API's documented shapes: timeframe; eq/exists/DOES_NOT_EXIST leaves; filterCombination and groups; events
    paging (limit, offset); calculations count. preflight: per-attempt {"abr": n or None (omitted), "count": n or None
    (the true count)} for a count query carrying `dev exists`."""

    def __init__(self, events, ignore_filters=False, counts_zero=False, preflight=None, blind_hours=()):
        self.events = sorted(events, key=lambda e: e["$metadata"]["id"])
        self.ignore, self.counts_zero, self.preflight = ignore_filters, counts_zero, list(preflight or [])
        self.blind_hours = set(blind_hours)      # F1 POPULATION: hour starts (ms) whose count queries answer at ABR 100
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
    if len(argv) != 5:
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
                          ("precl_reader", argv[3]), ("prepop_reader", argv[4])):
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
        old, new, sbr, prerec, precl, srcr, prepop = (load(paths[n], n) for n in (
            "old_reader", "new_reader", "sab_reader", "prerec_reader", "precl_reader", "src_reader", "prepop_reader"))

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

        print("rows %d, as predicted %d, OFF %d" % (len(rows), len(rows) - off, off))
        return 0 if off == 0 else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
