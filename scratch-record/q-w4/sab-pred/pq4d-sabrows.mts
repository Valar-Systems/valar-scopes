// R12 (b)/(c)'s prototype-side instrument: every value the rewritten MB / MN / MW13 rows and the 14 SEA-CI rows assert, computed by the
// PINNED PROTOTYPE pq4d (never the code under test), each fixture in its own try/catch so a plant that throws marks only its own rows.
// The computations are pq4d-oldrows.mts's, pq4d-seaci.mts's (plant mode's fixed caps: provB 458, d2 600) and pq4d-xchk.mts's, plus
// T-PROV-b's in-test control (keepWater at epsilon x 2 and x 8). Its CONTROL is the clean run: every object must equal the frozen
// cp-out/oldrows-pq4d.json and cp-out/seaci-pq4d.json entries and the registered xchk values (pq4d-sabpredict.mjs checks it).
// Ruled settings only: Q4_LADDER=b Q4_D2=1 Q4_TP=1 Q4_PROV=1 Q4_SEA=1, no plant knob (refuses otherwise).
//   (from wt-main/proxy) Q4_... node --import tsx ../../pq4d-sabrows.mts
import { build, Q4, q4Out } from "./pq4d/src/basemap/build.ts";
import { type Pt, decode, validate } from "./pq4d/src/basemap/format.ts";
import { Q4P, Q4W, makeFrame, properPairs, q4Stats } from "./pq4d/src/basemap/frame.ts";
import { keepWater } from "./pq4d/src/basemap/layers.ts";
import { Q4S } from "./pq4d/src/basemap/sea.ts";
import { nonTerrainBytes } from "./pq4d/src/basemap/budget.ts";
import { parseQuery } from "./pq4d/src/basemap/query.ts";
import { type SynthDef, COMMIT, lonLatOf, mbStore, writeSynthStore } from "./pq4d/test/basemap-synth.ts";
import { SEACI, seaciDef, seaciQuery } from "./wt-code/proxy/test/basemap-seaci-fixtures.ts";

// KNOB MODE (SABROWS_KNOB=1): the ruled settings plus exactly ONE prototype knob plant, as pq4d-seaci.mts's plant mode -- the grader's check
// against the SEA-CI red sets registered from those knobs (3bf4b3c2), which it must reproduce before it predicts anything.
const ruled = Q4.ladder && Q4.d2 && Q4W.tp && Q4W.backstop && Q4P.prov && Q4S.on && !Q4S.plant;
const oneKnob = Q4.ladder && Q4.d2 && Q4W.tp && Q4S.on && ((Q4S.plant !== "" ? 1 : 0) + (!Q4W.backstop ? 1 : 0) + (!Q4P.prov ? 1 : 0)) === 1;
if (process.env.SABROWS_KNOB === "1" ? !oneKnob : !ruled) { console.log(JSON.stringify({ error: "not the ruled settings (or not exactly one knob in knob mode)" })); process.exit(2); }
const out: Record<string, unknown> = {};
const guard = async (key: string, f: () => Promise<unknown>) => { try { out[key] = await f(); } catch (e) { out[key] = { error: String((e as Error)?.message ?? e).slice(0, 200) }; } };

// --- pq4d-oldrows.mts, per row
const prov = (bytes: Uint8Array) => {
  const s = decode(bytes).sections.find((x) => x.type === 7) as { text: Uint8Array };
  return Object.fromEntries(new TextDecoder().decode(s.text).split("\n").map((l) => [l.slice(0, l.indexOf(": ")), l.slice(l.indexOf(": ") + 2)]));
};
const rows: Array<["A" | "B" | "A8", number]> = [["A", 812], ["A", 811], ["A", 667], ["A", 623], ["A", 624], ["B", 841], ["B", 838], ["A8", 1322], ["A8", 1321], ["A", 100], ["A", 795]]; // A@795: the PROPOSED MB9 (rung 3, the shortest secondary dropped), its values this run's own
for (const [which, cap] of rows) await guard(`${which}@${cap}`, async () => {
  const s = await mbStore(which);
  const r = await build(s.store, s.query, { commit: COMMIT, capBytes: cap });
  const steps = q4Out.steps;
  if (!r.ok) return { status: r.status, code: (r as { code?: string }).code, steps, rung: "ladder" in r ? r.ladder.rung : null, nt: "ladder" in r ? r.ladder.nt : null, sections: "ladder" in r ? r.ladder.sections : null };
  const pkg = decode(r.bytes), sec = (t: number) => pkg.sections.find((x) => x.type === t);
  const water = (sec(1) as { rings: Pt[][] }).rings, roads = (sec(3) as { roads: Array<{ cls: number; pts: Pt[] }> }).roads, p = prov(r.bytes);
  return {
    status: 200, kOk: validate(r.bytes).status, totalBytes: r.bytes.length, toleranceMm: pkg.toleranceMm, radiusMm: pkg.radiusMm, water: water.map((w) => w[0]![0]),
    roads: roads.length, roadPts: roads.map((x) => x.pts.length), roadEnds: roads.map((x) => x.pts[x.pts.length - 1]![0]),
    dropped: `${p["dropped.water"]} / ${p["dropped.roads"]}`, eps: p["epsilon.m"], rung: r.ladder.rung, nt: r.ladder.nt, steps,
    sections: r.ladder.sections, sectionCount: pkg.sections.length,
  };
});
await guard("MW13", async () => {
  const W = "https://example.invalid/water/w.zip", R = "https://example.invalid/roads/r.zip";
  const ll = (pts: Array<[number, number]>) => pts.map(([e, s]) => lonLatOf(0.37, 0.59, e, s));
  const covered: Array<[number, number]> = [];
  for (let h = 30; h <= 40; h++) for (let c = 50; c <= 60; c++) covered.push([h, c]);
  const def: SynthDef = {
    files: [{ layer: "water", url: W, sha256: "3".repeat(64), vintage: "2024" }, { layer: "roads", url: R, sha256: "4".repeat(64), vintage: "2024" }],
    features: [
      { kind: "water", url: W, record: 0, part: 0, attrs: { AWATER: 2_000_000 }, lonlat: ll([[1, 1], [3, 1], [3, 3], [1, 3], [1, 1]]) },
      { kind: "roads", url: R, record: 0, part: 0, attrs: { MTFCC: "S1100", FULLNAME: "Main Hwy" }, lonlat: ll([[-5, -2], [5, -2]]) },
    ],
    regions: [{ box: [0.3, 0.3, 0.9, 0.45], files: [W] }, { box: [0.3, 0.3, 0.9, 0.45], files: [R] }],
    covered,
  };
  const s = await writeSynthStore(def);
  let reads = 0;
  const counted = { get: async (k: string) => { reads++; return s.store.objects.get(k) ?? null; } };
  const p = parseQuery(new URL("https://x/api/v1/blipscope/basemap?lat=0.37&lon=0.59&r=97&eps=100584&layers=water,roads"));
  if (!p.ok) throw new Error("MW's query does not parse");
  const r = await build(counted, { lat0Micro: p.q.lat0Micro, lon0Micro: p.q.lon0Micro, rKm: p.q.rKm, epsMm: p.q.epsMm, mask: p.q.mask, levels: p.q.levels }, { commit: COMMIT, capBytes: 100 });
  return { status: r.ok ? 200 : r.status, code: r.ok ? null : (r as { code: string }).code, reads, steps: q4Out.steps };
});

// --- pq4d-seaci.mts's one(), per fixture, at plant mode's caps
const one = async (name: string, cap?: number) => {
  const fx = SEACI[name]!();
  const { store } = await writeSynthStore(seaciDef(fx, lonLatOf) as any);
  q4Stats.refused = 0;
  const r = await build(store, seaciQuery(fx), { commit: "0".repeat(40), ...(cap !== undefined ? { capBytes: cap } : {}) });
  const o: any = { name, cap: cap ?? null, status: r.ok ? 200 : r.status, code: r.ok ? null : (r as any).code, steps: q4Out.steps, refusedBuilt: q4Stats.refused };
  if (!r.ok) { if ("ladder" in r) o.rung = (r as any).ladder.rung; return o; }
  const pkg = decode(r.bytes) as any, water = ((pkg.sections.find((s: any) => s.type === 1)?.rings ?? []) as number[][][]);
  const provT = new TextDecoder().decode(pkg.sections.find((s: any) => s.type === 7).text);
  const pv = (k: string) => provT.split("\n").find((l) => l.startsWith(`${k}: `))?.slice(k.length + 2) ?? null;
  const sea = q4Out.sea, seaIdx: number[] = sea?.ringIdx ?? [];
  Object.assign(o, { kOk: validate(r.bytes).status, rung: r.ladder.rung, radiusMm: pkg.radiusMm, totalBytes: r.bytes.length, ntb: nonTerrainBytes(pkg.sections),
    rings: water.length, seaRings: seaIdx.length, crossings: water.reduce((a, g) => a + properPairs(g as any).length, 0),
    seaLines: provT.split("\n").filter((l) => /^(source|vintage|files|sha256|dropped|collapsed)\.sea: /.test(l)).length,
    collapsedWater: pv("collapsed.water"), droppedWater: pv("dropped.water"), refused: q4Out.refused,
    sea: sea ? { refused: sea.refused, collapsed: sea.collapsed, dropped: sea.dropped, islandsDropped: sea.islandsDropped, islandsSmall: sea.islandsSmall, bridges: sea.bridges,
      outside: sea.outside, drawn: sea.drawn, refused5: sea.refused5, repairs: sea.repairs, repairVertices: sea.repairVertices, fates: (sea.fates ?? []).map((x: any) => x.fate) } : null,
    sha12: Buffer.from(await crypto.subtle.digest("SHA-256", r.bytes)).toString("hex").slice(0, 12) });
  if (name === "edge2") o.edge = seaIdx.map((i) => [...new Set(water[i]!.filter((p) => p[0] === 0).map((p) => p[1]!))].sort((a, b) => a - b));
  return o;
};
for (const n of ["kb1", "kb1c", "edge2", "kb2cross", "tsea0-without", "tsea0-with", "bowtie", "xisland", "xisland-tall", "provA-both", "provA-square"]) await guard(`sea:${n}`, () => one(n));
await guard("sea:provB", () => one("provB", 458));
await guard("sea:d2", () => one("d2", 600));
await guard("sea:d2-control", () => one("d2-control", 600));

// --- pq4d-xchk.mts: the repair's counts (from the harness line, whose numbers are the counts T-SEALOG and XISLAND assert)
const countsOf = (line: string) => { const m = /rings ([\d,]*) restored (\d+) rounds (\d+)/.exec(line); return m ? { rings: m[1] === "" ? 0 : m[1]!.split(",").length, restored: Number(m[2]), rounds: Number(m[3]) } : { unparsed: line.slice(0, 80) }; };
for (const n of ["xisland", "xisland-tall"]) await guard(`xchk:${n}`, async () => {
  const fx = SEACI[n]!(), { store } = await writeSynthStore(seaciDef(fx, lonLatOf) as any);
  const r = await build(store, seaciQuery(fx), { commit: "0".repeat(40) });
  return r.ok ? { status: 200, repairs: ((q4Out.sea?.repairLog ?? []) as string[]).map(countsOf) } : { status: r.status, code: (r as any).code, stop: (r as any).why ? countsOf((r as any).why) : null };
});

// --- T-PROV-b's control, as the test computes it: keepWater at epsilon x 2 and x 8 over provB's water features
await guard("provB-ctl", async () => {
  const fx = SEACI["provB"]!(), feats = fx.features.filter((x) => x.kind === "water").map((x) => ({ file: 0, record: x.record, part: x.part, attrs: x.attrs,
    xy: Float64Array.from(x.pts.flatMap(([e, s]) => lonLatOf(0, 0, e, s))) }));
  const w2 = keepWater(makeFrame(0, 0, 162_000_000, 2 * 201_168), feats as any) as any, w8 = keepWater(makeFrame(0, 0, 162_000_000, 8 * 201_168), feats as any) as any;
  return { w2: w2.collapsed + w2.refused, w8: w8.collapsed + w2.refused };
});
console.log(JSON.stringify(out));
