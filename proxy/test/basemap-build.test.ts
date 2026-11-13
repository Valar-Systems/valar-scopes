// MB, ME3, ME7 (docs/basemap-route.md A1.7): build.ts over the synthetic stores of basemap-synth.ts (committed before
// build.ts, A2.25). Each test is named by its case id; scripts/sabotage-basemap.mjs grades its exact sets from them.
import { describe, expect, it } from "vitest";
import { type BuildResult, build } from "../src/basemap/build";
import { type Pt, decode, validate } from "../src/basemap/format";
import { COMMIT, mbStore, me3Store, me7Store } from "./basemap-synth";

type Built = Extract<BuildResult, { ok: true }>;
const ok = (r: BuildResult): Built => { if (!r.ok) throw new Error(`not built: ${r.status} ${r.code}`); return r; };
const prov = (bytes: Uint8Array) => {
  const s = decode(bytes).sections.find((x) => x.type === 7) as { text: Uint8Array };
  return Object.fromEntries(new TextDecoder().decode(s.text).split("\n").map((l) => [l.slice(0, l.indexOf(": ")), l.slice(l.indexOf(": ") + 2)]));
};
/** Ring i's first vertex quantises at E = 25 + 5(i - 1) km (A1.7's fixture), written out, not taken from the builder. */
const ringX = (i: number) => Math.floor((25 + 5 * (i - 1)) * 1e6 / 97_000_000 * 32767 + 0.5);
const rings = (from: number) => [...Array(10 - from).keys()].map((k) => ringX(from + k));
/** A road of length L km runs east from E = -70 km, so its last vertex quantises at E = L - 70, written out. */
const roadEnd = (km: number) => Math.floor((km - 70) * 1e6 / 97_000_000 * 32767 + 0.5);

async function mb(which: "A" | "B" | "A8", cap: number) {
  const s = await (mbStore(which));
  const r = await build(s.store, s.query, { commit: COMMIT, capBytes: cap });
  if (!r.ok) return { status: r.status };
  const pkg = decode(r.bytes), sec = (t: number) => pkg.sections.find((x) => x.type === t);
  const water = (sec(1) as { rings: Pt[][] }).rings, roads = (sec(3) as { roads: Array<{ cls: number; pts: Pt[] }> }).roads, p = prov(r.bytes);
  expect(validate(r.bytes).status).toBe("kOk");
  return {
    status: 200, totalBytes: r.bytes.length, toleranceMm: pkg.toleranceMm, radiusMm: pkg.radiusMm, radii: r.report.radii, water: water.map((w) => w[0]![0]), roads: roads.length,
    roadPts: roads.map((x) => x.pts.length), roadEnds: roads.map((x) => x.pts[x.pts.length - 1]![0]), dropped: `${p["dropped.water"]} / ${p["dropped.roads"]}`, eps: p["epsilon.m"],
  };
}

// MB and MN under Q-W4's ladder (rungs 0-7, then D2): the values are the pinned prototype's on these same fixtures, frozen before these
// rows were edited (docs/basemap-dense-code.md, "The old ladder's test rows, re-frozen"; pq4d-oldrows.mts ff6351759648 on
// map-server/scratch-record 8c86cab778d6), never taken from the code under test.
describe("MB, §6's budget", () => {
  it("MB1: A at 812 fits as built", async () => expect(await mb("A", 812)).toMatchObject({ status: 200, totalBytes: 812, toleranceMm: 100_584, water: rings(1), roads: 7, dropped: "0 / 0", eps: "100.584" }));
  it("MB2: A at 811 fits at rung 2: water at ε × 8 collapses ring 1 (collapsed, not dropped)", async () => expect(await mb("A", 811)).toMatchObject({ status: 200, totalBytes: 796, toleranceMm: 201_168, water: rings(2), roads: 7, dropped: "0 / 0", eps: "100.584" }));
  it("MB3: A at 667 drops every secondary, then rings 2-7 smallest first: rung 4", async () => {
    const r = await mb("A", 667);
    expect(r).toMatchObject({ status: 200, totalBytes: 656, toleranceMm: 201_168, water: rings(8), roads: 3, dropped: "6 / 4", eps: "100.584" });
    // which 3: the primaries (10, 11, 12 km) -- primaries are never dropped
    expect(r.roadEnds).toEqual([10, 11, 12].map(roadEnd));
  });
  it("MB4: A at 623 does not fit at 97 km even with every secondary and ring dropped, so D2 builds it at 49.664 km", async () =>
    expect(await mb("A", 623)).toMatchObject({ status: 200, totalBytes: 623, toleranceMm: 201_168, radiusMm: 49_664_000, radii: [97, 77.6, 62.08, 49.664], water: [26391, 29690], roads: 0, dropped: "2 / 0", eps: "100.584" }));
  it("MB5: A at 624 keeps only the primaries: rung 4", async () => expect(await mb("A", 624)).toMatchObject({ status: 200, totalBytes: 624, toleranceMm: 201_168, radii: [97], water: [], roads: 3, dropped: "8 / 4", eps: "100.584" }));
  it("MB6: B at 841 fits at 2ε", async () => expect(await mb("B", 841)).toMatchObject({ status: 200, totalBytes: 839, toleranceMm: 201_168, water: rings(1), roads: 9, roadPts: [2, 2, 2, 2, 2, 2, 2, 2, 3], dropped: "0 / 0", eps: "100.584" }));
  it("MB7: B at 838 fits at rung 2: water at ε × 8 collapses ring 1", async () => expect(await mb("B", 838)).toMatchObject({ status: 200, totalBytes: 823, toleranceMm: 201_168, water: rings(2), roads: 9, roadPts: [2, 2, 2, 2, 2, 2, 2, 2, 3], dropped: "0 / 0", eps: "100.584" }));
  it("MB8a: A8 at 1,322 fits: the TERRAIN body is not counted", async () => expect(await mb("A8", 1322)).toMatchObject({ status: 200, totalBytes: 1346, toleranceMm: 100_584, water: rings(1), roads: 7, dropped: "0 / 0", eps: "100.584" }));
  it("MB8b: A8 at 1,321 fits at rung 2: water at ε × 8 collapses ring 1", async () => expect(await mb("A8", 1321)).toMatchObject({ status: 200, totalBytes: 1330, toleranceMm: 201_168, water: rings(2), roads: 7, dropped: "0 / 0", eps: "100.584" }));
});

// MN (docs/basemap-dense.md, step 0): what §6's ladder measured, carried on build()'s 200 and 422 for the builder's
// bm_build line. A's figures come from MB1-MB5, already on main: as built 812; at 2ε still 812 (its 2-vertex lines do
// not simplify, and the header's ε field is fixed-width); every ring dropped over 667 (MB3); every secondary too, 624.
async function ml(which: "A" | "B", cap: number) {
  const s = await mbStore(which);
  return build(s.store, s.query, { commit: COMMIT, capBytes: cap });
}
const tooLarge = (r: BuildResult) => { if (r.ok || r.status !== 422) throw new Error(`not a 422: ${r.ok ? 200 : r.status}`); return r; };
const sumOf = (b: object) => Object.values(b as Record<string, number>).reduce((a, x) => a + x, 0);

describe("MN, step 0's ladder", () => {
  it("MN1: A at 100 is a 422 after D2's 7 steps; the ruled ladder records no nt, and its layer bytes are the last rung tried's", async () => {
    const r = tooLarge(await ml("A", 100));
    expect([r.ladder.rung, r.ladder.nt]).toEqual([-1, []]);
    expect(r.report.radii).toEqual([97, 77.6, 62.08, 49.664, 39.7312, 31.78496, 25.427968]);
    expect(r.ladder.sections).toEqual({ water: 0, rivers: 0, roads: 0, peaks: 0, places: 0, provenance: 511 });
  });
  it("MN2: A at 812 fits as built: rung 0, and no nt on a 200", async () => {
    const r = ok(await ml("A", 812));
    expect([r.ladder.rung, r.ladder.nt]).toEqual([0, []]);
  });
  it("MN3: A at 811 fits with water at ε × 8: rung 2", async () => expect(ok(await ml("A", 811)).ladder.rung).toBe(2));
  it("MN4: A at 624 drops every secondary and every ring it can: rung 4", async () => expect(ok(await ml("A", 624)).ladder.rung).toBe(4));
  it("MN5: B at 841 fits at 2ε: rung 1", async () => expect(ok(await ml("B", 841)).ladder.rung).toBe(1));
  it("MN6: the header, every directory entry and the layers' bytes sum to the final non-terrain bytes", async () => {
    // A and B have no terrain, so a package's non-terrain bytes are all of its bytes (RD20)
    let count624 = 0;
    for (const [w, cap] of [["A", 812], ["A", 811], ["A", 624], ["B", 841]] as const) {
      const r = ok(await ml(w, cap));
      const n = decode(r.bytes).sections.length;
      if (w === "A" && cap === 624) count624 = n;
      expect(32 + 16 * n + sumOf(r.ladder.sections), `${w} at ${cap}`).toBe(r.bytes.length);
    }
    expect(count624).toBeGreaterThan(0); // CONTROL: the 624 package was read
    // a 422's layer bytes are its last rung tried at its last D2 step: A at 100's is PROVENANCE's alone
    const t = tooLarge(await ml("A", 100));
    expect(sumOf(t.ladder.sections), "A at 100").toBe(511);
  });
});

describe("ME, the encoder through build()", () => {
  it("ME3: a vertex at (E, S) = (R, -R) quantises to (32767, -32767) and the package is kOk", async () => {
    const s = await me3Store();
    const r = ok(await build(s.store, s.query, { commit: COMMIT }));
    expect(validate(r.bytes).status).toBe("kOk");
    const roads = (decode(r.bytes).sections.find((x) => x.type === 3) as { roads: Array<{ pts: Pt[] }> }).roads;
    expect(roads.flatMap((x) => x.pts).some(([a, b]) => a === 32767 && b === -32767)).toBe(true);
  });
  it("ME7: build() orders the levels itself: types 1-5, 6, 6, 7, levels 1,000,000 then 2,000,000 mm", async () => {
    const s = await me7Store();
    const r = ok(await build(s.store, s.query, { commit: COMMIT }));
    expect(validate(r.bytes).status).toBe("kOk");
    const secs = decode(r.bytes).sections;
    expect(secs.map((x) => x.type)).toEqual([1, 2, 3, 4, 5, 6, 6, 7]);
    expect(secs.filter((x) => x.type === 6).map((x) => (x as { levelRadiusMm: number }).levelRadiusMm)).toEqual([1_000_000, 2_000_000]);
  });
});
