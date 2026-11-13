# The diagnostic (registered 04:04Z, read-only): the pinned reference's mapper output, as float64 bits, for kb1's q outer and islands
# at (162 km, 45 deg). Prints JSON {ring: [[xhex, yhex], ...]} plus the cos/sin bits.
import hashlib, json, math, struct, sys
src = open(sys.argv[1], "rb").read(); assert hashlib.sha256(src).hexdigest()[:12] == "cca7d5c16db2"
text = src.decode(); g = {}; exec(compile(text[:text.index("\nmode = sys.argv[1]")], "ref", "exec"), g)
d = json.load(open(sys.argv[2])); q = d["q"][0]; f = g["mapper"](0.0, 0.0, d["radiusMm"], 0.0, 0.0, 162.0, 45)
hx = lambda v: struct.pack(">d", v).hex()
out = {"cos": hx(math.cos(math.radians(45))), "sin": hx(math.sin(math.radians(45)))}
for name, ring in [("outer", q["outer"])] + [(f"hole{i}", h) for i, h in enumerate(q["holes"])]:
    xs, ys = f(ring); out[name] = [[hx(x), hx(y)] for x, y in zip(xs, ys)]
print(json.dumps(out))
