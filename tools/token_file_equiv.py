#!/usr/bin/env python3
"""The before/after proof for the token readers (docs/token-file-python.md section 4). Each of the 21 readers runs
twice, from main's checkout (BEFORE) and this branch's (AFTER), under the SAME environment: a temporary HOME whose
.config/blipscope/tokens holds a planted file, and BLIPSCOPE_TOKEN_FILE naming an identical copy outside any git tree.
A runner stops each tool at its first network request, which it prints (URL, method, every header, the body), or
at its first exit. Nothing reaches the network: urlopen is replaced, every subprocess call is recorded and answered
with an empty result, never run (two of the readers drive dev deploys), the clock is frozen (SigV4 signs the time),
and sleeps return at once. The planted values are synthetic.

  python3 tools/token_file_equiv.py <BEFORE checkout> <AFTER checkout>     exit 0 every case as predicted, 1 otherwise
"""
import json
import os
import subprocess
import sys
import tempfile

PLANT = {"analytics-read": "PLANT-ANALYTICS-0001", "observability-read": "PLANT-OBS-0002",
         "r2-wx-read-id": "PLANTAKID000000000000", "r2-wx-read-secret": "PLANT-SECRET-0003",
         "records-device": "0123456789abcdef", "test-device": "beefbeefbeefbeef"}   # EXAMPLE ids, synthetic (the hook's allowlist)
A, B = "2026-11-12T00:00:00Z", "2026-11-12T01:00:00Z"
TOOLS = {  # path -> (argv after the script, extra env); OUT is replaced by a temporary path
    "tools/budget/budget_read.py": (["r", A, B, "silent", "rest", "OUT"], {}),
    "tools/budget/live_listeners.py": (["2026-11-12T21:00:00Z", "1"], {}),
    "tools/budget/logs_bytes.py": (["2026-11-12T21:00:00Z"], {}),
    "tools/wx/account_cost_read.py": (["read", "OUT"], {}),
    "tools/wx/classa_24h.py": (["2026-11-12T16:24:00Z"], {}),
    "tools/wx/classa_hourly.py": (["blipscope-wx", "2026-11-12T16:24:00Z"], {}),
    "tools/wx/cost_read.py": (["OUT"], {}),
    "tools/wx/frames_get.py": (["OUT"], {}),
    "tools/wx/latest_check.py": (["opuk"], {}),
    "tools/wx/logver.py": (["blipscope-wx-ingest-opuk", A, B], {}),
    "tools/wx/m_clock.py": (["opwe", "equiv", "s4"], {"M_PROXY": "OUT", "PLAN_S_WANT": ""}),
    "tools/wx/pd6_fleet.py": ([A, B], {}),
    "tools/wx/r2_ops.py": ([A, B], {}),
    "tools/wx/r3_5b.py": (["opuk"], {}),
    "tools/wx/r3_list.py": (["wx/opuk/frames/"], {}),
    "tools/wx/r3_minute_join.py": ([A, B], {}),
    "tools/wx/r3_reads.py": (["logs", A, B], {}),
    "tools/wx/r3p.py": (["lines", "blipscope-wx-ingest", A, B], {}),
    "tools/wx/s_clock.py": (["opuk", "equiv", "s4"], {"M_PROXY": "OUT", "PLAN_S_WANT": ""}),
    "tools/wx/s_reads.py": (["S1", A, B], {}),
    "tools/wx/share_range.py": ([A, B], {}),
}

RUNNER = r'''
import datetime as _dt, json, runpy, subprocess, sys, time, urllib.request
FROZEN = 1794500000.0
class Captured(BaseException):
    pass
def _urlopen(req, *a, **k):
    if isinstance(req, str):
        url, method, headers, data = req, "GET", {}, None
    else:
        url, method, data = req.full_url, req.get_method(), req.data
        headers = {k.lower(): v for k, v in req.header_items()}
    if isinstance(data, bytes):
        data = data.decode("utf-8", "replace")
    print("CAPTURED " + json.dumps({"url": url, "method": method, "headers": dict(sorted(headers.items())), "data": data}, sort_keys=True))
    sys.stdout.flush()
    raise Captured()
urllib.request.urlopen = _urlopen
def _run(args, *a, **k):
    text = k.get("text") or k.get("universal_newlines") or k.get("encoding")
    print("SUBPROCESS " + json.dumps([str(x) for x in (args if isinstance(args, (list, tuple)) else [args])]))
    return subprocess.CompletedProcess(args, 0, "" if text else b"", "" if text else b"")
def _never(*a, **k):
    raise Captured()
subprocess.run = _run
subprocess.Popen = _never
subprocess.call = lambda *a, **k: 0
subprocess.check_call = lambda *a, **k: 0
subprocess.check_output = lambda *a, **k: (b"" if not (k.get("text") or k.get("encoding")) else "")
time.time = lambda: FROZEN
time.sleep = lambda s: None
_gm, _lt, _sf = time.gmtime, time.localtime, time.strftime
time.gmtime = lambda s=None: _gm(FROZEN if s is None else s)
time.localtime = lambda s=None: _lt(FROZEN if s is None else s)
time.strftime = lambda f, t=None: _sf(f, _lt(FROZEN) if t is None else t)
class _F(_dt.datetime):
    @classmethod
    def now(cls, tz=None):
        return cls.fromtimestamp(FROZEN, tz)
    @classmethod
    def utcnow(cls):
        return cls.fromtimestamp(FROZEN, _dt.timezone.utc).replace(tzinfo=None)
    @classmethod
    def today(cls):
        return cls.fromtimestamp(FROZEN)
_dt.datetime = _F
tool = sys.argv[1]
sys.argv = [tool] + sys.argv[2:]
try:
    runpy.run_path(tool, run_name="__main__")
except Captured:
    sys.exit(0)
except SystemExit as e:
    print("EXIT " + json.dumps({"code": e.code if isinstance(e.code, int) or e.code is None else "message"}))
    sys.exit(3)
except BaseException as e:
    print("EXCEPTION " + type(e).__name__)
    sys.exit(4)
print("NO REQUEST")
'''


def outcome(stdout):
    """The first CAPTURED / EXIT / EXCEPTION / NO REQUEST line, and every SUBPROCESS line before it."""
    subs, first = [], None
    for line in stdout.splitlines():
        if line.startswith("SUBPROCESS "):
            subs.append(line)
        elif line.split(" ", 1)[0] in ("CAPTURED", "EXIT", "EXCEPTION", "NO"):
            first = line
            break
    return first or "NOTHING", subs


def run(root, tool, args, env_extra, tokens_body, d, case, home_in_git=False, unset=False):
    home = os.path.join(d, f"{case}-home")
    os.makedirs(os.path.join(home, ".config", "blipscope"), exist_ok=True)
    if home_in_git:
        os.makedirs(os.path.join(home, ".git"), exist_ok=True)
    with open(os.path.join(home, ".config", "blipscope", "tokens"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(tokens_body)
    copy = os.path.join(home, ".config", "blipscope", "tokens") if home_in_git else os.path.join(d, f"{case}-tf", "tokens")
    if not home_in_git:
        os.makedirs(os.path.dirname(copy), exist_ok=True)
        with open(copy, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(tokens_body)
    out = os.path.join(d, f"{case}-out")
    os.makedirs(out, exist_ok=True)
    env = {k: os.environ[k] for k in ("PATH", "SYSTEMROOT", "TEMP", "TMP", "COMSPEC") if k in os.environ}
    env.update({"HOME": home, "USERPROFILE": home, "PYTHONIOENCODING": "utf-8"})
    if not unset:
        env["BLIPSCOPE_TOKEN_FILE"] = copy
    env.update({k: (out if v == "OUT" else v) for k, v in env_extra.items()})
    runner = os.path.join(d, "runner.py")
    argv = [out if a == "OUT" else a for a in args]
    r = subprocess.run([sys.executable, "-I", runner, os.path.join(root, tool)] + argv, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", env=env, cwd=out, timeout=120)
    return outcome(r.stdout)


def main():
    before, after = sys.argv[1], sys.argv[2]
    good = "# planted\n" + "".join(f"{k}={v}\n" for k, v in PLANT.items())
    empty = "# planted: no keys at all\n"
    malformed = "BARE-LINE-NO-EQUALS\n" + good
    dup = good + "".join(f"{k}={v}-DUP\n" for k, v in PLANT.items())
    off, rows = 0, []
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "runner.py"), "w", encoding="utf-8") as fh:
            fh.write(RUNNER)
        for tool, (args, envx) in TOOLS.items():
            b1, _ = run(before, tool, args, envx, good, d, "e1b")
            a1, _ = run(after, tool, args, envx, good, d, "e1a")
            e1 = b1 == a1 and b1.startswith("CAPTURED") and any(v in b1 for v in PLANT.values())
            b2, _ = run(before, tool, args, envx, empty, d, "e2b")
            a2, _ = run(after, tool, args, envx, empty, d, "e2a")
            refuses = lambda o: o.startswith(("EXIT", "EXCEPTION"))
            e2 = refuses(b2) and refuses(a2)
            e3 = {}
            for case, body, kw in (("unset", good, {"unset": True}), ("malformed", malformed, {}), ("duplicate", dup, {}),
                                   ("in-git", good, {"home_in_git": True})):
                bb, _ = run(before, tool, args, envx, body, d, f"e3{case}b", **kw)
                aa, _ = run(after, tool, args, envx, body, d, f"e3{case}a", **kw)
                e3[case] = ("CAPTURED" if bb.startswith("CAPTURED") else bb.split(" ")[0]) + " -> " + \
                           ("CAPTURED" if aa.startswith("CAPTURED") else aa.split(" ")[0])
            ok = e1 and e2 and all(v.endswith("-> EXIT") for v in e3.values())
            off += not ok
            rows.append((tool, e1, e2, e3, ok, b1[:60] if not e1 else ""))
            print(f"{'as predicted' if ok else 'OFF':12s} {tool}: E1 {'identical' if e1 else 'DIFFERENT'}; E2 "
                  f"{'both refuse' if e2 else 'NOT both refuse: ' + b2.split(' ')[0] + '/' + a2.split(' ')[0]}; E3 " +
                  ", ".join(f"{k} {v}" for k, v in e3.items()))
            if not e1:
                print(f"    BEFORE {b1[:160]}\n    AFTER  {a1[:160]}")
    print(f"EQUIVALENCE: {len(TOOLS)} tools, {len(TOOLS) - off} as predicted, {off} off")
    return 0 if not off else 1


if __name__ == "__main__":
    sys.exit(main())
