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
    status: 200, totalBytes: r.bytes.length, toleranceMm: pkg.toleranceMm, water: water.map((w) => w[0]![0]), roads: roads.length,
    roadPts: roads.map((x) => x.pts.length), roadEnds: roads.map((x) => x.pts[x.pts.length - 1]![0]), dropped: `${p["dropped.water"]} / ${p["dropped.roads"]}`, eps: p["epsilon.m"],
  };
}

describe("MB, §6's budget", () => {
  it("MB1: A at 812 fits as built", async () => expect(await mb("A", 812)).toMatchObject({ status: 200, totalBytes: 812, toleranceMm: 100_584, water: rings(1), roads: 7, dropped: "0 / 0", eps: "100.584" }));
  it("MB2: A at 811 doubles ε, then drops ring 1", async () => expect(await mb("A", 811)).toMatchObject({ status: 200, totalBytes: 796, toleranceMm: 201_168, water: rings(2), roads: 7, dropped: "1 / 0", eps: "100.584" }));
  it("MB3: A at 667 drops every ring and the 3 km secondary", async () => {
    const r = await mb("A", 667);
    expect(r).toMatchObject({ status: 200, totalBytes: 657, toleranceMm: 201_168, water: [], roads: 6, dropped: "9 / 1", eps: "100.584" });
    // which 6: the three primaries (10, 11, 12 km) and the secondaries of 4, 5 and 6 km -- the 3 km one is the one dropped
    expect(r.roadEnds).toEqual([10, 11, 12, 4, 5, 6].map(roadEnd));
  });
  it("MB4: A at 623 is too large: primaries are never dropped", async () => expect(await mb("A", 623)).toEqual({ status: 422 }));
  it("MB5: A at 624 keeps only the primaries", async () => expect(await mb("A", 624)).toMatchObject({ status: 200, totalBytes: 624, toleranceMm: 201_168, water: [], roads: 3, dropped: "9 / 4", eps: "100.584" }));
  it("MB6: B at 841 fits at 2ε", async () => expect(await mb("B", 841)).toMatchObject({ status: 200, totalBytes: 839, toleranceMm: 201_168, water: rings(1), roads: 9, roadPts: [2, 2, 2, 2, 2, 2, 2, 2, 3], dropped: "0 / 0", eps: "100.584" }));
  it("MB7: B at 838 drops ring 1 at 2ε", async () => expect(await mb("B", 838)).toMatchObject({ status: 200, totalBytes: 823, toleranceMm: 201_168, water: rings(2), roads: 9, roadPts: [2, 2, 2, 2, 2, 2, 2, 2, 3], dropped: "1 / 0", eps: "100.584" }));
  it("MB8a: A8 at 1,322 fits: the TERRAIN body is not counted", async () => expect(await mb("A8", 1322)).toMatchObject({ status: 200, totalBytes: 1346, toleranceMm: 100_584, water: rings(1), roads: 7, dropped: "0 / 0", eps: "100.584" }));
  it("MB8b: A8 at 1,321 doubles ε, then drops ring 1", async () => expect(await mb("A8", 1321)).toMatchObject({ status: 200, totalBytes: 1330, toleranceMm: 201_168, water: rings(2), roads: 7, dropped: "1 / 0", eps: "100.584" }));
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
