// SEA-CI (docs/basemap-dense-code.md, "SEA-CI: the code PR's host tests, registered ..."): Q-W4's sea path, the counting rule "(C)", D2
// and the production repair log, over the synthetic stores of basemap-seaci-fixtures.ts. Every expected value here was frozen in that
// section BEFORE this file existed, read from the pinned prototype pq4d on these same fixtures -- never from the code under test. Each
// test is named by its registered id; scripts/sabotage-basemap.mjs grades its exact red sets from them.
import { describe, expect, it } from "vitest";
import { type BuildResult, build } from "../src/basemap/build";
import { decode, validate } from "../src/basemap/format";
import { type Pt, makeFrame, properPairs } from "../src/basemap/frame";
import { type SrcFeature, keepWater } from "../src/basemap/layers";
import { buildLine, seaRepairLines } from "../src/basemap/worker";
import { COMMIT, lonLatOf, writeSynthStore } from "./basemap-synth";
import { SEACI, seaciDef, seaciQuery } from "./basemap-seaci-fixtures";
import { kb1Views } from "./basemap-fillring";

type Built = Extract<BuildResult, { ok: true }>;
async function run(name: string, cap?: number): Promise<BuildResult> {
  const fx = SEACI[name]!();
  const { store } = await writeSynthStore(seaciDef(fx, lonLatOf));
  return build(store, seaciQuery(fx), { commit: COMMIT, ...(cap !== undefined ? { capBytes: cap } : {}) });
}
const ok = (r: BuildResult): Built => { if (!r.ok) throw new Error(`not built: ${r.status} ${r.code}`); return r; };
/** What a built package shows: its water rings, the sea's among them, proper crossings, its PROVENANCE lines. */
function facts(r: Built) {
  const pkg = decode(r.bytes), water = ((pkg.sections.find((s) => s.type === 1) as { rings: Pt[][] } | undefined)?.rings ?? []);
  const prov = new TextDecoder().decode((pkg.sections.find((s) => s.type === 7) as { text: Uint8Array }).text).split("\n");
  const pv = (k: string) => prov.find((l) => l.startsWith(`${k}: `))?.slice(k.length + 2);
  return { kOk: validate(r.bytes).status, radiusMm: pkg.radiusMm, rings: water.length, seaRings: r.report.sea!.ringIdx.length, water, ringIdx: r.report.sea!.ringIdx,
    crossings: water.reduce((a, g) => a + properPairs(g).length, 0), seaLines: prov.filter((l) => /^(source|vintage|files|sha256|dropped|collapsed)\.sea: /.test(l)).length,
    collapsedWater: pv("collapsed.water"), sea: r.report.sea! };
}
const STAT = { reads: 1, bytes: 2, readMs: 3, maxReadMs: 4 };
const atMost2 = (radii: number[]) => radii.length <= 2;
const decreasing = (radii: number[]) => radii.every((x, i) => i === 0 || x < radii[i - 1]!);

describe("SEA-CI: the sea's synthetic stores", () => {
  it("T-SEA0: a store with no sea layer draws no sea, and PROVENANCE reads no sea layer", async () => {
    const f = facts(ok(await run("tsea0-without")));
    expect([f.kOk, f.seaRings, f.seaLines]).toEqual(["kOk", 0, 0]);
    expect(f.rings).toBe(1); // CONTROL: the store's water square is drawn, so the package was read
  });
  it("T-SEA0c: the same store with the sea layer draws its ring and reads the layer's six lines", async () => {
    const f = facts(ok(await run("tsea0-with")));
    expect([f.seaRings, f.seaLines]).toEqual([1, 6]);
  });
  it("KB2: no water ring of any package crosses itself (bridges are contacts)", async () => {
    for (const n of ["kb1", "kb1c", "kb2cross", "edge2", "tsea0-with"]) {
      const f = facts(ok(await run(n)));
      expect(f.rings, n).toBeGreaterThan(0); // CONTROL: there were rings to test
      expect(f.crossings, n).toBe(0);
    }
  });
  it("BRIDGES: each piece's islands join it, one drawn sea ring per piece", async () => {
    const want: Record<string, [number, number]> = { kb1: [3, 1], kb1c: [3, 1], kb2cross: [2, 1], edge2: [0, 2], "tsea0-with": [3, 1] };
    for (const [n, [bridges, seaRings]] of Object.entries(want)) {
      const f = facts(ok(await run(n)));
      expect([f.sea.bridges, f.seaRings], n).toEqual([bridges, seaRings]);
    }
  });
  it("EDGE2: two pieces on one meridian keep the same quantised edge vertices (the edge lock)", async () => {
    const f = facts(ok(await run("edge2")));
    const sets = f.ringIdx.map((i) => [...new Set(f.water[i]!.filter((p) => p[0] === 0).map((p) => p[1]))].sort((a, b) => a - b));
    expect(sets).toEqual([[405, 4045], [405, 4045]]);
  });
  it("KB7C: a piece the backstop refuses is counted lost, and a normal piece is not", async () => {
    const b = facts(ok(await run("bowtie")));
    expect([b.sea.lost, b.sea.refused, b.sea.fates.map((x) => x.fate), b.rings]).toEqual([1, 1, ["refused"], 0]);
    const k = facts(ok(await run("kb1")));
    expect(k.sea.lost).toBe(0);
  });
  it("XISLAND: two rings that cross after simplifying are repaired (2 vertices, 2 rounds) and drawn", async () => {
    const f = facts(ok(await run("xisland")));
    expect([f.sea.repairs, f.sea.repairVertices, f.sea.repairCounts]).toEqual([1, 2, [{ rings: 2, restored: 2, rounds: 2 }]]);
    expect([f.sea.fates.map((x) => x.fate), f.sea.refused5, f.crossings]).toEqual([["drawn"], 0, 0]);
  });
  it("XISLAND-TALL: a repair that cannot finish stops the build: 500 sea_repair_failed", async () => {
    const r = await run("xisland-tall");
    if (r.ok || r.status !== 500) throw new Error(`not a 500: ${r.ok ? 200 : r.status}`);
    expect([r.code, r.repair?.restored, r.repair?.rounds]).toEqual(["sea_repair_failed", 2, 3]);
  });
  // KB1 (registered at 667f7b6c before this row; R8's port, its parity with the pinned reference shown first): two rasters from ONE
  // runtime, so no cross-language bits enter it
  it("KB1: the bridged sea ring fills as its unbridged outer and islands by even-odd (FillRing's rule): 0 differing pixels in all 24 views", async () => {
    for (const [n, islands] of [["kb1", 20_818], ["kb1c", 15_388]] as const) {
      const r = ok(await run(n)), pkg = decode(r.bytes), s = r.report.sea!, f0 = s.fates[0];
      const water = ((pkg.sections.find((x) => x.type === 1) as { rings: Pt[][] } | undefined)?.rings ?? []);
      expect(f0?.fate, n).toBe("drawn");
      const v = kb1Views(s.ringIdx.map((i) => water[i]!), f0!.q!, pkg.radiusMm, 162);
      expect(v.map((x) => x.diff), n).toEqual(Array(24).fill(0));
      expect(v.reduce((a, x) => a + x.island, 0), `${n}: CONTROL, the reference holds the islands`).toBe(islands);
    }
  });
  it("REFUSED5: no step-5 refusal and no repair where none is needed", async () => {
    for (const n of ["kb1", "kb1c", "edge2", "kb2cross", "tsea0-with", "bowtie"]) {
      const s = ok(await run(n)).report.sea!;
      expect([s.refused5, s.repairs], n).toEqual([0, 0]);
    }
  });
});

describe("SEA-CI: the counting rule and D2", () => {
  it("T-PROV-a: the backstop's refusal is counted apart, and the package carries no trace of the refused ring", async () => {
    const both = ok(await run("provA-both")), sq = ok(await run("provA-square")), fb = facts(both), fs = facts(sq);
    expect([both.ladder.rung, both.report.radii.length, fb.rings, fb.crossings, both.report.refused, both.report.refusedBuilt, fb.collapsedWater]).toEqual([0, 1, 1, 0, 1, 3, "0"]);
    expect(buildLine(both, STAT, 5).refused).toBe(1);
    expect([fs.rings, sq.report.refused, sq.report.refusedBuilt, fs.collapsedWater]).toEqual([1, 0, 0, "0"]);
    expect(both.bytes).toEqual(sq.bytes);
  });
  it("T-PROV-b: past rung 1, collapsed.water is the drawn ε × 8 set's, not the ε × 2 set's", async () => {
    const r = ok(await run("provB", 458)), f = facts(r);
    expect([r.ladder.rung, f.collapsedWater, r.report.refused, r.report.refusedBuilt]).toEqual([2, "0", 0, 2]);
    // CONTROL: both wrong rules give another number. The ε × 2 set's water, and the drawn ε × 8 set's, built directly:
    const fx = SEACI["provB"]!(), feats: SrcFeature[] = fx.features.filter((x) => x.kind === "water").map((x) => ({ file: 0, record: x.record, part: x.part, attrs: x.attrs,
      xy: Float64Array.from(x.pts.flatMap(([e, s]) => lonLatOf(0, 0, e, s))) }));
    const w2 = keepWater(makeFrame(0, 0, 162_000_000, 2 * 201_168), feats), w8 = keepWater(makeFrame(0, 0, 162_000_000, 8 * 201_168), feats);
    expect(w2.collapsed + w2.refused, "the ε × 2 set's count, its refusal counted as a collapse").toBe(1);
    expect(w8.collapsed + w2.refused, "the drawn set's count with that refusal inside").toBe(1);
  });
  it("T-D2: a store that does not fit at 162 km and fits at 129.6 km lists both radii, in order", async () => {
    const r = ok(await run("d2", 600)), f = facts(r);
    expect([r.report.radii, f.radiusMm, buildLine(r, STAT, 5).steps]).toEqual([[162, 129.6], 129_600_000, 2]);
    expect([decreasing(r.report.radii), atMost2(r.report.radii)]).toEqual([true, true]);
  });
  it("T-D2c: the control, a store that needs a third step, fails the at-most-2 check", async () => {
    const r = ok(await run("d2-control", 600));
    expect(r.report.radii).toEqual([162, 129.6, 103.68]);
    expect([decreasing(r.report.radii), atMost2(r.report.radii)]).toEqual([true, false]);
  });
});

describe("SEA-CI: the production repair log", () => {
  const LINE = /^sea_repair rings \d+ restored \d+ rounds \d+$/;
  it("T-SEALOG: the sea_repair lines and bm_build carry counts only -- no record key, no point -- and still say something", async () => {
    const built = await run("xisland"), stopped = await run("xisland-tall");
    // the limit's stop, given directly (no synthetic store reaches 64 restored vertices without a plant): seaRepairLines() is the one
    // source of the lines, and its `why` carries the harness's full line, record key and point, which production must never log
    const limit: BuildResult = { ok: false, status: 500, code: "sea_repair_limit", why: "sea_repair 9/9 rings 0,1 restored 65 rounds 9 island 0.123,0.456",
      repair: { rings: 2, restored: 65, rounds: 9 } };
    for (const [r, what] of [[built, "repaired"], [stopped, "stopped"], [limit, "the limit"]] as const) {
      const lines = seaRepairLines(r), bm = JSON.stringify(buildLine(r, STAT, 5));
      expect(lines.length, what).toBe(1); // CONTROL: the line still says something
      expect(lines[0], what).toMatch(LINE);
      for (const l of [...lines, bm]) {
        expect(/\d\.\d/.test(l), `${what}: ${l}`).toBe(false);
        expect(/\d+\/\d+/.test(l), `${what}: ${l}`).toBe(false);
      }
    }
  });
});
