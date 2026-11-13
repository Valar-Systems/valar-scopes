// The sea ingest's hand-written rectangle clip (docs/basemap-dense-code.md, PARITY: pinned a39c8762a6ef, ported byte for byte below this
// header; PARITY matched the dry run over all 2,584 cells with it): a Weiler-Atherton specialised to an axis-aligned rectangle. Coordinates
// are [lon, lat] in degrees. No dependency.
//   1. each ring is classed against the CLOSED rectangle: wholly inside (touching allowed), or crossing it (cut into inside chains);
//   2. a crossing ring, rotated to start at a vertex strictly outside, is walked edge by edge with Liang-Barsky; an inside portion that
//      starts on the boundary opens a chain and one that ends on it closes it; boundary points are snapped exactly onto their plane;
//   3. outers are oriented clockwise and holes counter-clockwise, so the polygon's interior is always on the right of travel; chains are
//      stitched by walking the rectangle's boundary CLOCKWISE from each exit to the next entry, passing corners;
//   4. no chain at all: the rectangle is the piece if its centre is inside the outer and inside no hole;
//   5. holes wholly inside go to the piece that contains one of their vertices strictly inside the rectangle;
//   6. pieces of zero area are dropped (the dry run drops `p.area <= 0`).
export type Pt = [number, number];
export interface Piece { outer: Pt[]; holes: Pt[][] }
export interface Rect { x0: number; y0: number; x1: number; y1: number }

const signed2 = (r: Pt[]) => { let s = 0; for (let i = 0; i < r.length; i++) { const a = r[i]!, b = r[(i + 1) % r.length]!; s += a[0] * b[1] - b[0] * a[1]; } return s; };
export const area = (r: Pt[]) => Math.abs(signed2(r)) / 2;
const open = (r: Pt[]): Pt[] => (r.length > 1 && r[0]![0] === r[r.length - 1]![0] && r[0]![1] === r[r.length - 1]![1] ? r.slice(0, -1) : r.slice());
const cw = (r: Pt[]) => (signed2(r) > 0 ? r.slice().reverse() : r);   // clockwise: signed area < 0
const ccw = (r: Pt[]) => (signed2(r) < 0 ? r.slice().reverse() : r);
const outside = (p: Pt, R: Rect) => p[0] < R.x0 || p[0] > R.x1 || p[1] < R.y0 || p[1] > R.y1;

/** Point in ring, crossing number (half-open in y). */
export function inRing(p: Pt, r: Pt[]): boolean {
  let c = false;
  for (let i = 0, j = r.length - 1; i < r.length; j = i++) {
    const [xi, yi] = r[i]!, [xj, yj] = r[j]!;
    if ((yi > p[1]) !== (yj > p[1]) && p[0] < ((xj - xi) * (p[1] - yi)) / (yj - yi) + xi) c = !c;
  }
  return c;
}

/** Liang-Barsky on the closed rectangle: the inside part [t0, t1] of a -> b, with each end snapped onto the plane that cut it. */
function lb(a: Pt, b: Pt, R: Rect): { t0: number; t1: number; s: Pt; e: Pt } | null {
  const dx = b[0] - a[0], dy = b[1] - a[1];
  let t0 = 0, t1 = 1, p0 = -1, p1 = -1;
  const P = [-dx, dx, -dy, dy], Q = [a[0] - R.x0, R.x1 - a[0], a[1] - R.y0, R.y1 - a[1]];
  for (let k = 0; k < 4; k++) {
    const pk = P[k]!, qk = Q[k]!;
    if (pk === 0) { if (qk < 0) return null; continue; }
    const r = qk / pk;
    if (pk < 0) { if (r > t1) return null; if (r > t0) { t0 = r; p0 = k; } }
    else { if (r < t0) return null; if (r < t1) { t1 = r; p1 = k; } }
  }
  const at = (t: number, plane: number, end: Pt): Pt => {
    if (plane < 0) return [end[0], end[1]];
    const p: Pt = [a[0] + t * dx, a[1] + t * dy];
    if (plane === 0) p[0] = R.x0; else if (plane === 1) p[0] = R.x1; else if (plane === 2) p[1] = R.y0; else p[1] = R.y1;
    return p;
  };
  return { t0, t1, s: at(t0, p0, a), e: at(t1, p1, b) };
}

/** An edge lying ON one side of the rectangle (both ends on the same side line). PD-9's fix, recorded before it was applied
 *  (e5beb78cef9e): such an edge counts as OUTSIDE when chains are built, so a ring flush with a side yields chains, and a flush hole
 *  becomes a notch in the outer, as GEOS makes it. */
const onSide = (a: Pt, b: Pt, R: Rect) => (a[0] === R.x0 && b[0] === R.x0) || (a[0] === R.x1 && b[0] === R.x1) || (a[1] === R.y0 && b[1] === R.y0) || (a[1] === R.y1 && b[1] === R.y1);

/** The ring's inside chains, or "inside" (wholly inside the closed rectangle, touching it only at points), or [] (no inside part). */
function chainsOf(ring: Pt[], R: Rect): Pt[][] | "inside" {
  const n = ring.length;
  let k = -1;   // the walk starts at a vertex strictly outside, or at the end of an edge lying on a side
  for (let i = 0; i < n; i++) if (outside(ring[i]!, R) || onSide(ring[(i - 1 + n) % n]!, ring[i]!, R)) { k = i; break; }
  if (k < 0) return "inside";
  const out: Pt[][] = [];
  let chain: Pt[] | null = null;
  for (let m = 0; m < n; m++) {
    const a = ring[(k + m) % n]!, b = ring[(k + m + 1) % n]!;
    if (onSide(a, b, R)) { if (chain && chain.length >= 2) out.push(chain); chain = null; continue; }
    const c = lb(a, b, R);
    if (!c) continue;
    if (!chain) chain = [c.s];
    const last = chain[chain.length - 1]!;
    if (last[0] !== c.e[0] || last[1] !== c.e[1]) chain.push(c.e);
    if (c.t1 < 1) { if (chain.length >= 2) out.push(chain); chain = null; }
  }
  if (chain && chain.length >= 2) out.push(chain);
  return out;
}

/** Clockwise position along the rectangle's boundary, in [0, 4): up the west edge, east along the north, down the east, west along the south. */
function bpos(p: Pt, R: Rect): number {
  const w = R.x1 - R.x0, h = R.y1 - R.y0;
  if (p[0] === R.x0) return (p[1] - R.y0) / h;
  if (p[1] === R.y1) return 1 + (p[0] - R.x0) / w;
  if (p[0] === R.x1) return 2 + (R.y1 - p[1]) / h;
  return (3 + (R.x1 - p[0]) / w) % 4;
}
const CORNERS = (R: Rect): Array<[number, Pt]> => [[1, [R.x0, R.y1]], [2, [R.x1, R.y1]], [3, [R.x1, R.y0]], [4, [R.x0, R.y0]]];

function stitch(chains: Pt[][], R: Rect): Pt[][] {
  const ent = chains.map((c, i) => ({ i, s: bpos(c[0]!, R) })), used = new Array<boolean>(chains.length).fill(false), rings: Pt[][] = [];
  const corners = CORNERS(R);
  for (let start = 0; start < chains.length; start++) {
    if (used[start]) continue;
    const ring: Pt[] = []; let cur = start, guard = 0;
    for (;;) {
      if (++guard > chains.length + 4) throw new Error("stitch did not close");
      used[cur] = true; ring.push(...chains[cur]!);
      const xs = bpos(chains[cur]![chains[cur]!.length - 1]!, R);
      let best = -1, bd = Infinity;
      for (const e of ent) { const d = (((e.s - xs) % 4) + 4) % 4; if (d < bd || (d === bd && e.i === start)) { bd = d; best = e.i; } }
      // the corners passed between the exit and that entry, in CLOCKWISE order from the exit (sorted by their distance d from it)
      const passed = corners.map(([cs, cp]) => ({ d: (((cs - xs) % 4) + 4) % 4, cp })).filter((c) => c.d > 0 && c.d < bd).sort((a, b) => a.d - b.d);
      for (const c of passed) ring.push([c.cp[0], c.cp[1]]);
      if (best === start || used[best]) { if (best !== start) throw new Error("stitch reached a used chain"); break; }
      cur = best;
    }
    rings.push(ring);
  }
  return rings;
}

/** One source polygon (outer + holes, any orientation, closed or open) clipped to one rectangle. */
export function clipPolygon(outerIn: Pt[], holesIn: Pt[][], R: Rect): Piece[] {
  const outer = cw(open(outerIn)), holes = holesIn.map((h) => ccw(open(h)));
  let bx0 = Infinity, by0 = Infinity, bx1 = -Infinity, by1 = -Infinity;
  for (const [x, y] of outer) { if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y; }
  if (bx1 <= R.x0 || bx0 >= R.x1 || by1 <= R.y0 || by0 >= R.y1) return [];
  const hb = holes.map((h) => { let a = Infinity, b = Infinity, c = -Infinity, d = -Infinity; for (const [x, y] of h) { if (x < a) a = x; if (x > c) c = x; if (y < b) b = y; if (y > d) d = y; } return [a, b, c, d] as const; });
  const chains: Pt[][] = [], insideHoles: Pt[][] = [], containing: Pt[][] = [];
  const oc = chainsOf(outer, R);
  const pieces: Piece[] = [];
  if (oc === "inside") pieces.push({ outer, holes: [] }); else chains.push(...oc);
  holes.forEach((h, i) => {
    const [a, b, c, d] = hb[i]!;
    if (c <= R.x0 || a >= R.x1 || d <= R.y0 || b >= R.y1) return;
    const hc = chainsOf(h, R);
    if (hc === "inside") insideHoles.push(h); else if (hc.length) chains.push(...hc); else containing.push(h);
  });
  if (chains.length) for (const r of stitch(chains, R)) pieces.push({ outer: r, holes: [] });
  else if (oc !== "inside") {
    const c: Pt = [(R.x0 + R.x1) / 2, (R.y0 + R.y1) / 2];
    if (inRing(c, outer) && !containing.some((h) => inRing(c, h))) pieces.push({ outer: [[R.x0, R.y0], [R.x0, R.y1], [R.x1, R.y1], [R.x1, R.y0]], holes: [] });
  }
  for (const h of insideHoles) {
    const v = h.find((p) => p[0] > R.x0 && p[0] < R.x1 && p[1] > R.y0 && p[1] < R.y1) ?? h[0]!;
    const host = pieces.find((pc) => inRing(v, pc.outer));
    if (host) host.holes.push(h);
  }
  return pieces.filter((pc) => area(pc.outer) - pc.holes.reduce((s, h) => s + area(h), 0) > 1e-15);
}

/** The dry run's island rule: keep a hole whose km2 (deg2 x 111^2 x cos(its own centroid latitude), floored at 0.01) is >= min. */
export function holeKm2(h: Pt[]): number {
  const r = open(h); let a2 = 0, cy = 0;
  for (let i = 0; i < r.length; i++) { const [x0, y0] = r[i]!, [x1, y1] = r[(i + 1) % r.length]!; const cr = x0 * y1 - x1 * y0; a2 += cr; cy += (y0 + y1) * cr; }
  const A = a2 / 2, yc = cy / (6 * A);
  return Math.abs(A) * 111.0 * 111.0 * Math.max(Math.cos((yc * Math.PI) / 180), 0.01);
}
