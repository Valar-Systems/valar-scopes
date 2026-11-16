# SCRATCH (map-server): step8_26.pred Amendment 5c, I2's red control plant. kb5-plant.py 11b99bf9fb47's move, without its assertion that
# the vertex is run 10's registered ring 38 vertex 1: in <rings>, the first vertex inside the view (2..238 px) is moved 1 px at right
# angles to the edge it ends (vertex v-1 to v), along that edge's left-hand normal. Writes <moved rings>. Landing: the moved file differs
# from the original in that one vertex's line alone, and the move is 1 px. Prints the ring and vertex indices and the landing only.
#   python -I mirror/i2plant26.py <rings> <moved rings>
import json, math, sys
src, dst = sys.argv[1], sys.argv[2]
L = open(src).read().split("\n"); n = int(L[0]); i = 1; first = None
for r in range(n):
    m = int(L[i]); i += 1
    for v in range(m):
        x, y = map(float, L[i + v].split())
        if first is None and 2 <= x <= 238 and 2 <= y <= 238: first = (r, v, i + v, i, m)
    i += m
if first is None:
    print(json.dumps({"error": "no vertex in view"})); sys.exit(3)
r, v, line, start, m = first
x1, y1 = map(float, L[line].split()); x0, y0 = map(float, L[start + (v - 1) % m].split())
dx, dy = x1 - x0, y1 - y0; ln = math.hypot(dx, dy); nx, ny = -dy / ln, dx / ln
M = list(L); M[line] = f"{x1 + nx!r} {y1 + ny!r}"
open(dst, "w").write("\n".join(M))
B = open(dst).read().split("\n")
diff = [k for k in range(max(len(L), len(B))) if (L[k] if k < len(L) else None) != (B[k] if k < len(B) else None)]
ok = diff == [line] and abs(math.hypot(nx, ny) - 1.0) < 1e-9
print(json.dumps({"ring": r, "vertex": v, "landing": {"lines_differing": len(diff), "only_that_vertex": diff == [line], "ok": ok}}))
sys.exit(0 if ok else 1)
