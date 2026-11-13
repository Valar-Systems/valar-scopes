# R8's parity step, the PYTHON side: the pinned reference's own functions (p2-render.py cca7d5c16db2, its definitions only, executed
# after its sha256 is checked), over the synthetics' KB1 inputs; per view the sha256 of the drawn mask and of the reference mask, and
# the diff / island counts. Prints JSON.   python -I r8/fill-parity.py <pinned p2-render.py> <input json> ...  (pairs "plant=clean")
import hashlib, json, sys
ref_path = sys.argv[1]
src = open(ref_path, "rb").read()
# the pinned reference (#662's rule), or the CONTROL's: the same renderer with the pre-#662 crossing (p2-render-oldfill.py)
assert hashlib.sha256(src).hexdigest()[:12] in ("cca7d5c16db2", "06a74487f91e"), "not a pinned file"
text = src.decode("utf-8"); cut = text.index("\nmode = sys.argv[1]")
g = {}; exec(compile(text[:cut], ref_path, "exec"), g)
fill_ring, mapper, union, np = g["fill_ring"], g["mapper"], g["union"], g["np"]
h = lambda m: hashlib.sha256(m.astype(np.uint8).tobytes()).hexdigest()[:16]
out = {}
for arg in sys.argv[2:]:
    plant, _, clean = arg.partition("=")
    d = json.load(open(plant)); ref = json.load(open(clean or plant))
    sea = [d["water"][i] for i in d["seaIdx"]]; q = ref["q"][0]
    rows = []
    for rv in (162.0, 81.0, 40.5):
        for rot in range(0, 360, 45):
            f = mapper(0.0, 0.0, d["radiusMm"], 0.0, 0.0, rv, rot)
            drawn = union(sea, f); outer = fill_ring(*f(q["outer"])); refm = outer.copy()
            for hh in q["holes"]: refm ^= fill_ring(*f(hh))
            rows.append([rv, rot, int((drawn ^ refm).sum()), int((outer & ~refm).sum()), h(drawn), h(refm)])
    out[arg] = rows
print(json.dumps(out))
