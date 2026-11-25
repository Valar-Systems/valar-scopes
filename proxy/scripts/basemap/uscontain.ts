// Design (a)'s "wholly inside the US" test (docs/basemap-dense-code.md, PARITY step 2; pinned 6f7e14013016, ported byte for byte below
// this header): ONE module, used by the sea ingest and by C-BORDER-S's test, so the code under test is the code that ships.
// A lake (its parts, each an outer ring, closed) is wholly inside the US when EVERY part has every vertex inside one US part and, unless
// mode is "vertices", no edge of the part properly crosses an edge of that US part. The US parts' edges are bucketed on a 0.5-degree grid.
import { type Pt, inRing } from "./rectclip";
export type Container = { parts: Pt[][]; idx: Array<Map<string, Array<[Pt, Pt]>>> };
const GRID = 0.5;
const cellsOf = (a: Pt, b: Pt, f: (key: string) => void) => {
  for (let gx = Math.floor(Math.min(a[0], b[0]) / GRID); gx <= Math.floor(Math.max(a[0], b[0]) / GRID); gx++)
    for (let gy = Math.floor(Math.min(a[1], b[1]) / GRID); gy <= Math.floor(Math.max(a[1], b[1]) / GRID); gy++) f(`${gx},${gy}`);
};
export function container(parts: Pt[][]): Container {
  return { parts, idx: parts.map((P) => { const m = new Map<string, Array<[Pt, Pt]>>(); for (let k = 0; k < P.length - 1; k++) { const a = P[k]!, b = P[k + 1]!; cellsOf(a, b, (key) => { (m.get(key) ?? m.set(key, []).get(key)!).push([a, b]); }); } return m; }) };
}
const orient = (a: Pt, b: Pt, c: Pt) => Math.sign((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
const crossesProp = (a: Pt, b: Pt, c: Pt, d: Pt) => orient(a, b, c) * orient(a, b, d) < 0 && orient(c, d, a) * orient(c, d, b) < 0;
export function whollyInside(C: Container, outers: Pt[][], mode: "full" | "vertices" = "full"): boolean {
  for (const ring of outers) {
    const host = C.parts.findIndex((P) => ring.every((v) => inRing(v, P)));
    if (host < 0) return false;
    if (mode === "vertices") continue;
    const idx = C.idx[host]!;
    for (let k = 0; k < ring.length - 1; k++) {
      const a = ring[k]!, b = ring[k + 1]!; let hit = false;
      cellsOf(a, b, (key) => { if (!hit) for (const [c, d] of idx.get(key) ?? []) if (crossesProp(a, b, c, d)) { hit = true; break; } });
      if (hit) return false;
    }
  }
  return true;
}
