// R8's parity step, the TYPESCRIPT side: the port (wt-code/proxy/test/basemap-fillring.ts) over the same inputs, the same per-view
// sha256 prefixes and counts. PLANT=old swaps in the pre-#662 crossing (from the edge's FIRST endpoint) -- the control, which must differ.
//   node --import tsx r8/fill-parity.mts <input json> ...   (pairs "plant=clean")
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import * as F from "../wt-code/proxy/test/basemap-fillring.ts";
const h = (m: Uint8Array) => createHash("sha256").update(m).digest("hex").slice(0, 16);
let fill = F.fillRing;
if (process.env.PLANT === "old") {
  // the control: the crossing computed from each edge's first endpoint, as FillRing did before #662
  fill = (xs, ys, n = F.N) => {
    const m = new Uint8Array(n * n), len = xs.length; if (len < 3) return m;
    let ymin = Infinity, ymax = -Infinity; for (const y of ys) { if (y < ymin) ymin = y; if (y > ymax) ymax = y; }
    for (let y = Math.max(0, Math.floor(ymin) - 1); y < Math.min(n, Math.ceil(ymax) + 2); y++) {
      const py = y + 0.5, cx: number[] = [];
      for (let i = 0; i < len; i++) { const xa = xs[i]!, ya = ys[i]!, xb = xs[(i + 1) % len]!, yb = ys[(i + 1) % len]!; if ((ya > py) !== (yb > py)) cx.push(xa + (py - ya) * (xb - xa) / (yb - ya)); }
      cx.sort((a, b) => a - b);
      for (let k = 0; k < cx.length - 1; k += 2) { const lo = Math.max(Math.ceil(cx[k]! - 0.5), 0), hi = Math.min(Math.ceil(cx[k + 1]! - 0.5), n); for (let x = lo; x < hi; x++) m[y * n + x] = 1; }
    }
    return m;
  };
}
const union = (rings: F.Pt[][], f: (r: F.Pt[]) => [number[], number[]]) => { const m = new Uint8Array(F.N * F.N); for (const r of rings) { const [xs, ys] = f(r), g = fill(xs, ys); for (let i = 0; i < m.length; i++) m[i]! |= g[i]!; } return m; };
const out: Record<string, unknown[]> = {};
for (const arg of process.argv.slice(2)) {
  const [plant, clean] = arg.split("=");
  const d = JSON.parse(readFileSync(plant!, "utf8")), ref = JSON.parse(readFileSync(clean ?? plant!, "utf8"));
  const sea: F.Pt[][] = d.seaIdx.map((i: number) => d.water[i]), q = ref.q[0];
  const rows: unknown[] = [];
  if (process.env.PLANT !== "old") { // the port's own entry point, as KB1 calls it
    for (const v of F.kb1Views(sea, q, d.radiusMm, 162)) rows.push([v.rv, v.rot, v.diff, v.island, h(v.drawn), h(v.ref)]);
    out[arg] = rows; continue;
  }
  for (const rv of [162.0, 81.0, 40.5]) for (let rot = 0; rot < 360; rot += 45) {
    const f = F.mapper(0.0, 0.0, d.radiusMm, 0.0, 0.0, rv, rot);
    const drawn = union(sea, f), [ox, oy] = f(q.outer), outer = fill(ox, oy), refm = outer.slice();
    for (const hh of q.holes) { const [hx, hy] = f(hh), g = fill(hx, hy); for (let i = 0; i < refm.length; i++) refm[i]! ^= g[i]!; }
    let diff = 0, island = 0; for (let i = 0; i < refm.length; i++) { diff += drawn[i]! ^ refm[i]!; island += outer[i]! & (refm[i]! ^ 1); }
    rows.push([rv, rot, diff, island, h(drawn), h(refm)]);
  }
  out[arg] = rows;
}
console.log(JSON.stringify(out));
