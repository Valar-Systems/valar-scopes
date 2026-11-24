// Q-W4's SEA-CI fixtures (docs/basemap-dense-code.md, part 2 and the code PR's port): pure data, no imports, so the SAME geometry is
// built by the tests here and by the scratch script that takes their expected values from the pinned prototype (pq4d), never from the
// code under test. Origin (0, 0); points are (E, S) km, east and SOUTH positive; the caller turns them into lon/lat with lonLatOf.
// Every store is the client's query at r 162 km, ε 201,168 mm, unless a fixture says otherwise.
//
// The first eight are the prototype's own synthetic cases (scratch dr-p2-synth.mts, run 6 to run 10), transcribed point for point:
//   kb1, kb1c, edge2, kb2cross, tsea0 (with / without the kb1 piece), bowtie (a sea piece), xisland (and its "tall" variant).
// Then QW4-3's bowtie WATER store (T-PROV-a), and two new fixtures registered with their values before any test asserts them:
//   provB  T-PROV-b: KMSP's shape -- a water bowtie whose fourth vertex D sits 1.0 km off the chord A-C: kept at ε and ε × 2 (where
//          the ring crosses itself and is REFUSED) and dropped by Douglas-Peucker at ε × 8 (1.609 km), leaving the valid triangle A-C-B,
//          drawn; and a 256-vertex circle that simplifies at each ε; at a cap that lands on rung 2 the package draws the ε × 8 set
//          (refused 0, collapsed 0) while the ε and ε × 2 sets each hold the refusal;
//   d2     T-D2: one water ring near the centre and ONE primary road at E 140..150 km, inside the 162 km square and outside the
//          129.6 km one; a primary is never dropped, so a cap under the 162 km package's floor and over the 129.6 km package fits at
//          129.6 km: radii [162, 129.6]. Its control adds a second primary at E 110..120 km, outside 103.68 km: three steps.
export type Pt = [number, number];
export interface SeaciFeature { kind: "water" | "roads" | "sea"; url: string; record: number; part: number; attrs: Record<string, string | number>; pts: Pt[] }
export interface SeaciFixture { features: SeaciFeature[]; mask: "water" | "water,roads"; rKm: number }

export const W_URL = "https://example.invalid/water/w.zip", S_URL = "https://example.invalid/sea/s.zip", R_URL = "https://example.invalid/roads/r.zip";
const sq = (e0: number, s0: number, e1: number, s1: number): Pt[] => [[e0, s0], [e1, s0], [e1, s1], [e0, s1], [e0, s0]];
const circ = (ce: number, cs: number, r: number, n = 256): Pt[] => {
  const p: Pt[] = Array.from({ length: n }, (_, k) => [ce + r * Math.cos((2 * Math.PI * k) / n), cs + r * Math.sin((2 * Math.PI * k) / n)] as Pt);
  return [...p, p[0]!];
};
const sea = (record: number, rings: Pt[][]): SeaciFeature[] => rings.map((pts, part) => ({ kind: "sea", url: S_URL, record, part, attrs: { area: 1, src: "synthetic" }, pts }));
const water = (record: number, pts: Pt[]): SeaciFeature => ({ kind: "water", url: W_URL, record, part: 0, attrs: { AWATER: 2_000_000 }, pts });
const primary = (record: number, pts: Pt[]): SeaciFeature => ({ kind: "roads", url: R_URL, record, part: 0, attrs: { MTFCC: "S1100", FULLNAME: `Primary ${record + 1}` }, pts });
const KB1 = sea(0, [sq(-100, -100, 200, 100), sq(-40, -40, -25, -25), sq(10, 5, 18, 13), sq(150, 0, 175, 20)]);
const XOUTER: Pt[] = [[0, 0], [20, 0], [20, 20], [14.5, 20], [14.5, 20.18], [5.5, 20.18], [5.5, 20], [0, 20], [0, 0]];

/** A fixture as a synthetic store's definition, exactly as the prototype's driver made its stores: the water file ALWAYS (so PROVENANCE
 *  reads the water layer even with no water feature), the sea and roads files when a feature uses them, every file's region the box
 *  +-170 km, coverage the origin's degree. `lonLatOf(lat0, lon0, e, s)` is the caller's (basemap-synth.ts). */
export function seaciDef(fx: SeaciFixture, lonLatOf: (lat0: number, lon0: number, e: number, s: number) => [number, number]) {
  const files = [{ layer: "water", url: W_URL, sha256: "3".repeat(64), vintage: "2024" }];
  if (fx.features.some((f) => f.kind === "sea")) files.push({ layer: "sea", url: S_URL, sha256: "6".repeat(64), vintage: "2022" });
  if (fx.features.some((f) => f.kind === "roads")) files.push({ layer: "roads", url: R_URL, sha256: "4".repeat(64), vintage: "2024" });
  const half = 170, box = [...lonLatOf(0, 0, -half, half), ...lonLatOf(0, 0, half, -half)] as [number, number, number, number];
  return {
    files, regions: files.map((f) => ({ box, files: [f.url] })), covered: [[0, 0]] as Array<[number, number]>,
    features: fx.features.map((f) => ({ kind: f.kind, url: f.url, record: f.record, part: f.part, attrs: f.attrs, lonlat: f.pts.map(([e, s]) => lonLatOf(0, 0, e, s)) })),
  };
}
/** The fixture's query: origin (0, 0), its radius, ε 201,168 mm, its layers (MASK: water 1, roads 4), no terrain levels. */
export const seaciQuery = (fx: SeaciFixture) => ({ lat0Micro: 0, lon0Micro: 0, rKm: fx.rKm, epsMm: 201_168, mask: fx.mask === "water" ? 1 : 1 | 4, levels: [] as Array<{ radiusMm: number; n: number }> });

export const SEACI: Record<string, () => SeaciFixture> = {
  kb1: () => ({ features: KB1, mask: "water", rKm: 162 }),
  kb1c: () => ({ features: sea(0, [sq(-100, -100, 200, 100), circ(-32.5, -32.5, 7.5), circ(14, 9, 4), circ(162.5, 10, 10)]), mask: "water", rKm: 162 }),
  edge2: () => ({ features: [...sea(0, [sq(0, 2, 20, 20)]), ...sea(1, [[[-20, 2], [0, 2], [0, 20], [-0.15, 20.5], [-20, 20.5], [-20, 2]]])], mask: "water", rKm: 162 }),
  kb2cross: () => ({ features: sea(0, [sq(-50, -50, 50, 50), sq(20, 20, 40, 40), sq(41, 44, 49, 46)]), mask: "water", rKm: 162 }),
  "tsea0-without": () => ({ features: [water(0, sq(-30, -30, -20, -20))], mask: "water", rKm: 162 }),
  "tsea0-with": () => ({ features: [water(0, sq(-30, -30, -20, -20)), ...KB1], mask: "water", rKm: 162 }),
  bowtie: () => ({ features: sea(0, [[[10, 10], [10, 30], [50, 10], [50, 50], [10, 10]]]), mask: "water", rKm: 162 }),
  xisland: () => ({ features: sea(0, [XOUTER, sq(6, 19.85, 14, 20.15)]), mask: "water", rKm: 162 }),
  "xisland-tall": () => ({ features: sea(0, [XOUTER, sq(6, 19.85, 14, 20.25)]), mask: "water", rKm: 162 }),
  // QW4-3's store (T-PROV-a): the bowtie A(10,10) D(10,30) C(50,10) B(50,50) and a 10 km square nothing touches; and the square alone
  "provA-both": () => ({ features: [water(0, [[10, 10], [10, 30], [50, 10], [50, 50], [10, 10]]), water(1, sq(-40, -40, -30, -30))], mask: "water", rKm: 162 }),
  "provA-square": () => ({ features: [water(1, sq(-40, -40, -30, -30))], mask: "water", rKm: 162 }),
  // T-PROV-b: the bowtie A(60,60) D(60,61) C(66,60) B(66,70), unequal lobes (an outer part: signed area -27 in (E, -S), QW4-3's sign),
  // edges D-C and B-A crossing near E 60.5; and a 256-vertex circle
  provB: () => ({ features: [water(0, [[60, 60], [60, 61], [66, 60], [66, 70], [60, 60]]), water(1, circ(-50, 40, 30))], mask: "water", rKm: 162 }),
  d2: () => ({ features: [water(0, sq(-5, -5, 5, 5)), primary(0, [[140, -10], [145, -10.4], [150, -10]])], mask: "water,roads", rKm: 162 }),
  "d2-control": () => ({ features: [water(0, sq(-5, -5, 5, 5)), primary(0, [[140, -10], [145, -10.4], [150, -10]]), primary(1, [[110, 20], [115, 20.4], [120, 20]])], mask: "water,roads", rKm: 162 }),
};
