#!/usr/bin/env python3
"""F1-R / F1-R-1's plant (docs/proxy-bill.md step 5): the readers against a FAKE Workers Observability API.

Synthetic data only, no network, no token: each reader is loaded from a temp copy beside a stub token_file.py, and its
`post` is replaced by the fake's, which RECORDS every event it returns. So the fetched counts below are the fake's
record, one instrument for the old reader and the new.

  python -X utf8 -I tools/bill/colo_occupancy_plant.py <old reader .py> <new reader .py>

The old reader is the one F1-R replaces (blob af55507b9ad7). Rows, in the registered order: the fake's own controls,
R-RED, R-PRE, R-CLEAN, R-BACKSTOP, R-SCAN, R-ABR-RED (a byte-asserted sabotage), R-ABR, R-ABR-ABSENT, R-ABR-RECOVER.
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


def scan_set():
    t = H + timedelta(minutes=1)
    h1, h2 = t - timedelta(hours=1), t - timedelta(hours=2)
    return [e for e in hour_h(False) if e["$metadata"]["id"] != "e008"] + [
        ev(10, h1, evt="req", colo="ZZ1", dev=EXAMPLE_ID),
        ev(11, h1, evt="req", colo="ZZ1", devl="lbl-a"),
        ev(12, h1, evt="req", colo="ZZ2", devl="lbl-c"),
        ev(13, h2, evt="req", colo="ZZ1", dev=EXAMPLE_ID)]


class Fake:
    """The API's documented shapes: timeframe; eq/exists/DOES_NOT_EXIST leaves; filterCombination and groups; events
    paging (limit, offset); calculations count. preflight: per-attempt {"abr": n or None (omitted), "count": n or None
    (the true count)} for a count query carrying `dev exists`."""

    def __init__(self, events, ignore_filters=False, counts_zero=False, preflight=None):
        self.events = sorted(events, key=lambda e: e["$metadata"]["id"])
        self.ignore, self.counts_zero, self.preflight = ignore_filters, counts_zero, list(preflight or [])
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
        inwin = [e for e in self.events if tf["from"] <= e["timestamp"] < tf["to"]]
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
    if len(argv) != 2:
        print(__doc__)
        return 2
    tmp = tempfile.mkdtemp(prefix="f1r-plant-")
    try:
        os.makedirs(os.path.join(tmp, "tools", "bill"))
        with open(os.path.join(tmp, "tools", "token_file.py"), "w", encoding="utf-8") as fh:
            fh.write('def token(purpose):\n    return "plant-token"\n')
        paths = {}
        for name, src in (("old_reader", argv[0]), ("new_reader", argv[1])):
            paths[name] = os.path.join(tmp, "tools", "bill", name + ".py")
            shutil.copyfile(src, paths[name])
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
        old, new, sbr = (load(paths[n], n) for n in ("old_reader", "new_reader", "sab_reader"))

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

        f = Fake(scan_set()); rc, o = run(new, f, ["scan", HIS])
        lines = [l.strip() for l in o.splitlines()]
        row("R-SCAN", dict(exit=rc, counts=f.count_requests, events_requests=f.events_requests, returned=len(f.returned),
                           rows=[l for l in lines if " dev " in l and " devl " in l],
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

        print("rows %d, as predicted %d, OFF %d" % (len(rows), len(rows) - off, off))
        return 0 if off == 0 else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
