// The diagnostic's TypeScript side: the port's mapper, the same rings and view, float64 bits.
import { readFileSync } from "node:fs";
import * as F from "../wt-code/proxy/test/basemap-fillring.ts";
const hx = (v: number) => { const b = new DataView(new ArrayBuffer(8)); b.setFloat64(0, v); return [...new Uint8Array(b.buffer)].map((x) => x.toString(16).padStart(2, "0")).join(""); };
const d = JSON.parse(readFileSync(process.argv[2]!, "utf8")), q = d.q[0], f = F.mapper(0.0, 0.0, d.radiusMm, 0.0, 0.0, 162.0, 45);
const DEG = Math.PI / 180.0, out: Record<string, unknown> = { cos: hx(Math.cos(45 * DEG)), sin: hx(Math.sin(45 * DEG)) };
for (const [name, ring] of [["outer", q.outer], ...q.holes.map((h: F.Pt[], i: number) => [`hole${i}`, h])] as Array<[string, F.Pt[]]>) {
  const [xs, ys] = f(ring); out[name] = xs.map((x, i) => [hx(x), hx(ys[i]!)]);
}
console.log(JSON.stringify(out));
