# SCRATCH: SEA-PKG, read on the code PR's builder as ported (docs/basemap-dense-code.md, d0): the 16 packages built by cp-run.mts on the
# prototype's sea store (sea-obj, store e9ef04165ba2), each against its frozen run-10 sha256. Predicted: byte-identical at all 16; any
# byte difference stops. Also prints run 10's SEA-FIG figures beside the code's (rung, radius, D2 steps, non-terrain bytes, the sea
# counts), read, not graded here. 4 builds at a time, one per process.
#   python -X utf8 -I cp-seapkg.py
import json, os, subprocess
from concurrent.futures import ThreadPoolExecutor
D = os.path.dirname(os.path.abspath(__file__)); P = os.path.join(D, "wt-main", "proxy")
RUN10 = {"KBOS": "f21d705d8b8a", "KJFK": "52ac9dc39de0", "KLGA": "feb6ef3bdf04", "KEWR": "dff28487eaf4", "MIDTOWN": "20a22bed5510", "KMIA": "1a338238fbc8",
         "KSFO": "4d03beecdb4b", "KSEA": "9bd5b34d6718", "KORD": "cb59bb06a506", "KDTW": "682208bf2fb2", "KMSP": "b694c01dc502", "KIND": "89a4195fa4be",
         "KJAC": "42c32cd8578e", "BUFFALO": "52fad5e317f3", "ROUSES": "afe7554245b8", "ZAPATA": "e2ddaa00ecaf"}
os.makedirs(os.path.join(D, "cp-out"), exist_ok=True)
def one(h):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("Q4_", "Q4H_", "DENSE_"))}
    x = subprocess.run(["node", "--import", "tsx", "../../cp-run.mts", "../../sea-obj", h, os.path.join(D, "cp-out", f"{h}-sea.bmpk")], cwd=P, env=env, capture_output=True, text=True, encoding="utf-8")
    lines = [l for l in x.stdout.splitlines() if l.startswith("RESULT ")]
    if len(lines) != 1: return h, {"error": f"exit {x.returncode}, {len(lines)} RESULT lines", "stderr_bytes": len(x.stderr), "stderr_tail": x.stderr[-800:]}
    return h, json.loads(lines[0][7:])
with ThreadPoolExecutor(4) as ex: res = dict(ex.map(one, list(RUN10)))
json.dump(res, open(os.path.join(D, "cp-out", "seapkg.json"), "w", encoding="utf-8"), indent=1)
same = 0
for h, want in RUN10.items():
    r = res[h]
    if "error" in r or not r.get("ok"): print(f"{h}: FAILED {json.dumps(r)[:900]}"); continue
    eq = r["sha"] == want; same += eq
    print(f"{h}: {'IDENTICAL' if eq else 'DIFFERS'} {r['sha']} (run 10 {want}); rung {r['rung']} {r['radiusKm']} km radii {r['radii']} ntb {r['ntb']} refusedBuilt {r['refusedBuilt']} refused {r['refused']} sea {r['sea']}")
print(f"SEA-PKG: {same} of {len(RUN10)} byte-identical to run 10" + (" -- AS PREDICTED" if same == len(RUN10) else " -- OFF: stop"))
