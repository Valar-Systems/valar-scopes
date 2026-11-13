// SCRATCH, Q-W4's prototype pq4d: mk-pq4c.mjs (351a3fa2c4f5) plus the SEA PATH behind Q4_SEA=1 (docs/basemap-dense-code.md, #652, part 2; the 04:12:32Z ruling). Before that, mk-pq4c's own header: SCRATCH, Q-W4's prototype pq4c: mk-pq4b.mjs (4161a87b369d) plus Q4_PROV and D2's step count (docs/basemap-dense-code.md, #652; the 22:25Z and 22:38Z rulings). Before that, mk-pq4b's own header: the prototype with the difference instrument's plant (Q4_PLANT_MIB; the orchestrator's 22:13Z ruling; docs/basemap-dense-code.md, #652): the pinned builder (525cf7d1725d's
// proxy/src/basemap/, from git, plus proxy/src/wx/crop.ts for the query parser) with the instrument's seven hooks and the code PR's changes
// as knobs. Every edit is anchored on a line that must occur exactly once; anything else refuses.
//   Q4_LADDER=b  A+B, then P1 (the owner's B): the copy's p1 path, verbatim, with its variant switch replaced by this knob
//   Q4_D2=1      D2 inside the builder: the whole build again at r x 0.8^k (k = 1..6) while it answers 422
//   Q4_TP=1      tp on every water ring, then the backstop (a ring still crossing is refused: ring null, so collapsed.water counts it);
//                Q4_BACKSTOP=0 turns the backstop off (the plant's control only)
// With every knob unset, the code paths are the pinned builder's (QW4-0 tests that by running).
//   node mk-pq4.mjs <repo> <pinned sha> <out dir> <dnw frame.ts>
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const [repo, P, out, dnwFrame] = process.argv.slice(2);
const die = (m) => { console.log(`REFUSED: ${m}`); process.exit(2); };
if (!repo || !P || !out || !dnwFrame) die("usage");
const git = (...a) => execFileSync("git", ["-C", repo, ...a], { encoding: "buffer", maxBuffer: 1 << 26 });
const once = (s, a, tag) => { const n = s.split(a).length - 1; if (n !== 1) die(`${tag}: anchor found ${n} times`); };
const after = (s, a, ins, tag) => { once(s, a, tag); return s.replace(a, a + ins); };
const before = (s, a, ins, tag) => { once(s, a, tag); return s.replace(a, ins + a); };
const swap = (s, a, b, tag) => { once(s, a, tag); return s.replace(a, b); };

const files = git("ls-tree", "--name-only", `${P}:proxy/src/basemap`).toString().trim().split("\n");
mkdirSync(join(out, "src", "basemap"), { recursive: true }); mkdirSync(join(out, "src", "wx"), { recursive: true });
const src = {};
for (const f of files) { const b = git("show", `${P}:proxy/src/basemap/${f}`); if (b.includes(13)) die(`${f} has CR`); src[f] = b.toString("utf8"); }
writeFileSync(join(out, "src", "wx", "crop.ts"), git("show", `${P}:proxy/src/wx/crop.ts`));

// ---- build.ts: the seven hooks (as mk-hooks.mjs), then the Q-W4 knobs
let b = src["build.ts"];
b = after(b, "  const f1 = makeFrame(q.lat0Micro, q.lon0Micro, rMm, q.epsMm), f2 = makeFrame(q.lat0Micro, q.lon0Micro, rMm, EPS_STEP * q.epsMm);\n", "  const MEM = (globalThis as any).__mem as ((t: string) => void) | undefined; // SCRATCH hook\n", "hook f1");
b = after(b, "          if (ft.kind === \"water\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft);\n        }\n", "        MEM?.(\"cell\"); // SCRATCH hook\n", "hook cell");
b = after(b, "      } else bitsBin = b;\n", "      MEM?.(\"tile\"); // SCRATCH hook\n", "hook tile");
b = before(b, "  let peaks: Array<{ q: Pt; elevM: number; name: string }> = [];\n", "  MEM?.(\"tiles\"); // SCRATCH hook\n", "hook tiles");
b = before(b, "  // the tiles read: their files (by RD17's /9/<x>/<y>.png tail) and their source bits (the credits)\n", "  MEM?.(\"terrain\"); // SCRATCH hook\n", "hook terrain");
b = after(b, "  const at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;\n", "  if (L4) at[4 * q.epsMm] = content(L4); if (L8) at[8 * q.epsMm] = content(L8); // SCRATCH Q-W4\n  MEM?.(\"layers\"); // SCRATCH hook\n", "hook layers + Q4 at");
b = after(b, "  const fitted = fitBudget(q.epsMm, opts.capBytes ?? CAP_BYTES, (tol) => at[tol]!, assemble, ladder);\n", "  MEM?.(\"ladder\"); // SCRATCH hook\n", "hook ladder");
b = after(b, "export const MASK = { water: 1, rivers: 2, roads: 4, peaks: 8, places: 16, terrain: 32 } as const;\n", "export const Q4 = { ladder: process.env.Q4_LADDER === \"b\", d2: process.env.Q4_D2 === \"1\", plantMiB: Number(process.env.Q4_PLANT_MIB ?? \"0\") }; // SCRATCH Q-W4 knobs\nexport const q4Plant = { bytes: 0, check: 0 }; // SCRATCH Q-W4: the difference instrument's plant, as the builder made it\n", "Q4 const");
b = before(b, "      for await (const b of readInOrder(store, cells.map((c) => keys.cell(c.key)))) {\n", "      let PLANT: Uint8Array | null = Q4.plantMiB > 0 ? new Uint8Array(Q4.plantMiB * 1048576).fill(7) : null; // SCRATCH Q-W4 plant\n      if (PLANT) q4Plant.bytes = PLANT.length;\n", "plant on");
b = before(b, "      // RD-P6: a layer's files read are those of the regions whose box meets the square's\n", "      if (PLANT) { q4Plant.check = PLANT[0]! + PLANT[PLANT.length - 1]!; PLANT = null; } // SCRATCH Q-W4 plant: held to the end of the cell loop\n", "plant off");
b = after(b, "  const L1 = new LayerSet(f1), L2 = new LayerSet(f2);\n", "  const L4 = Q4.ladder ? new LayerSet(makeFrame(q.lat0Micro, q.lon0Micro, rMm, 4 * q.epsMm)) : null, L8 = Q4.ladder ? new LayerSet(makeFrame(q.lat0Micro, q.lon0Micro, rMm, 8 * q.epsMm)) : null; // SCRATCH Q-W4\n", "Q4 sets");
b = after(b, "          if (ft.kind === \"water\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft);\n", "          if (L4 && (ft.kind === \"rivers\" || ft.kind === \"roads\")) L4.add(ft.kind, ft); // SCRATCH Q-W4\n          if (L8 && ft.kind === \"water\") L8.add(ft.kind, ft); // SCRATCH Q-W4\n", "Q4 cell adds");
b = swap(b, "export async function build(store: StoreReader, q: BuildQuery, opts: { commit: string; capBytes?: number }): Promise<BuildResult> {\n", "async function buildOnce(store: StoreReader, q: BuildQuery, opts: { commit: string; capBytes?: number }): Promise<BuildResult> {\n", "D2 rename");
b = after(b, "class StoreGap extends Error {}\n", [
  "",
  "/** SCRATCH Q-W4: D2 inside the builder. While the build answers 422, the whole build again at r x 0.8^k (k = 1..6: 129.6 .. 42.47 km",
  " *  from 162), each radius taken as Q-DR3 took it (Math.round(r_mm x 0.8^k) / 1e6). Past the last, the 422 stands (Q-DP3). */",
  "export async function build(store: StoreReader, q: BuildQuery, opts: { commit: string; capBytes?: number }): Promise<BuildResult> {",
  "  let r = await buildOnce(store, q, opts);",
  "  if (!Q4.d2 || r.ok || r.status !== 422) return r;",
  "  for (let k = 1; k <= 6; k++) {",
  "    (globalThis as any).__mem?.(\"d2\"); // SCRATCH hook: a D2 step begins",
  "    r = await buildOnce(store, { ...q, rKm: Math.round(q.rKm * 1e6 * 0.8 ** k) / 1e6 }, opts);",
  "    if (r.ok || r.status !== 422) return r;",
  "  }",
  "  return r;",
  "}",
  "",
].join("\n"), "D2 wrapper");
src["build.ts"] = b;

// ---- budget.ts: the copy's A+B and P1 path (dn/src/basemap/budget.ts, Q-DR3's), its variant switch replaced by Q4_LADDER=b
let g = src["budget.ts"];
g = after(g, "  if (s) return done(1, { content, toleranceMm: tol, dropped: d, sections: s });\n", [
  "",
  "  if (process.env.Q4_LADDER === \"b\") { // SCRATCH Q-W4: A+B, then P1 (the owner's B); the copy's p1 path, verbatim",
  "    // A+B (#620): water at 8 x the query's epsilon; then secondary roads shortest first; then water smallest first.",
  "    const dropSeq = (c0: C, tolH: number, rA: number, rB: number): Fitted<C> | null => {",
  "      d.water = 0; d.roads = 0;",
  "      const roads0 = c0.roads ?? [];",
  "      const sec0 = roads0.map((r, i) => [r, i] as const).filter(([r]) => r.cls === 2).map(([, i]) => i);",
  "      const order0 = dropOrder(sec0.map((i) => roadLength(roads0[i]!.pts))).map((k) => sec0[k]!);",
  "      const noRoads = (k: number): C => { const gone = new Set(order0.slice(0, k)); return { ...c0, roads: roads0.filter((_, i) => !gone.has(i)) }; };",
  "      const r0 = smallestFit(order0.length, (k) => { d.roads = k; return fits(noRoads(k), tolH); });",
  "      if (r0) { d.roads = r0.k; return done(rA, { content: noRoads(r0.k), toleranceMm: tolH, dropped: d, sections: r0.got }); }",
  "      d.roads = order0.length;",
  "      const c1 = noRoads(order0.length), rings0 = c1.water ?? [];",
  "      const w0 = dropOrder(rings0.map(ringArea2));",
  "      const noWater0 = (k: number): C => { const gone = new Set(w0.slice(0, k)); return { ...c1, water: rings0.filter((_, i) => !gone.has(i)) }; };",
  "      const w1 = smallestFit(rings0.length, (k) => { d.water = k; return fits(noWater0(k), tolH); });",
  "      if (w1) { d.water = w1.k; return done(rB, { content: noWater0(w1.k), toleranceMm: tolH, dropped: d, sections: w1.got }); }",
  "      return null;",
  "    };",
  "    const water8 = make(toleranceMm * 8).water;",
  "    const c2 = { ...content, water: water8 } as C;",
  "    s = fits(c2, tol);",
  "    if (s) return done(2, { content: c2, toleranceMm: tol, dropped: d, sections: s });",
  "    const tol4 = toleranceMm * 4;",
  "    const c4 = (): C => { const m4 = make(tol4) as any; return { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } } as C; };",
  "    const at4 = (): Fitted<C> | null => {",
  "      d.water = 0; d.roads = 0;",
  "      const a = c4(), s4 = fits(a, tol4);",
  "      if (s4) return done(5, { content: a, toleranceMm: tol4, dropped: d, sections: s4 });",
  "      return dropSeq(a, tol4, 6, 7);",
  "    };",
  "    let got: Fitted<C> | null = dropSeq(c2, tol, 3, 4);",
  "    if (!got) got = at4();",
  "    if (got) return got;",
  "    if (ladder) { ladder.rung = -1; ladder.nt = []; ladder.sections = layerBytes(last); }",
  "    return null;",
  "  }",
  "",
].join("\n"), "budget B");
src["budget.ts"] = g;

// ---- frame.ts: tp and properPairs (dnw's, Q-WATER's cost read), then the backstop
let fr = src["frame.ts"];
const dnw = readFileSync(dnwFrame, "utf8").replace(/\r/g, "");
const tail = dnw.slice(dnw.indexOf("// ------------------------------------------------------------------------------------------------ SCRATCH: Q-WATER's cost read"));
if (tail.length < 1000 || !tail.includes("export function simplifyRingTP") || !tail.includes("export function properPairs")) die("dnw's tp block not found");
fr += "\n" + tail.replace(/export function waterVP[\s\S]*$/, "") + [
  "// SCRATCH Q-W4: tp, then the backstop. A ring still crossing itself (a proper crossing, ring_stats()'s sense; a bridge's coincident",
  "// edges are contacts, not crossings) is refused: [] here, so LayerSet stores it as null and collapsed.water counts it.",
  "export const Q4W = { tp: process.env.Q4_TP === \"1\", backstop: process.env.Q4_BACKSTOP !== \"0\" };",
  "export const q4Stats = { refused: 0 };",
  "export function waterQ4(ringKm: Pt[], epsM: number, rMm: number): Pt[] {",
  "  const q = simplifyRingTP(ringKm, epsM, rMm);",
  "  if (Q4W.backstop && properPairs(q).length) { q4Stats.refused++; return []; }",
  "  return q;",
  "}",
  "",
].join("\n");
src["frame.ts"] = fr;

// ---- layers.ts: the water branch takes tp + the backstop under Q4_TP=1
let ly = src["layers.ts"];
ly = swap(ly, "  simplifyRing, toFrameKm, within3km,\n", "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4,\n", "layers import");
ly = swap(ly, "        const q = dedupe(simplifyRing(clipped, f.epsM).map((p) => quantise(p, f.rMm)), true);\n",
  "        const q = Q4W.tp ? waterQ4(clipped, f.epsM, f.rMm) : dedupe(simplifyRing(clipped, f.epsM).map((p) => quantise(p, f.rMm)), true); // SCRATCH Q-W4\n", "layers water");
src["layers.ts"] = ly;

// ---- SCRATCH Q-W4, pq4c: Q4_PROV=1 (the orchestrator's 22:25Z ruling 2 and 22:38Z ruling C), and D2's step count (ruling 3).
//   A ring the backstop refuses is counted apart from collapsed (frame.ts returns the REFUSED marker; LayerSet counts it as refused).
//   Rungs 2 and 5 take collapsed.water, and the refusals, from the ε x 8 set they draw, not from the ε x 2 set.
//   The build reports q4Out: the drawn set's refusals (refused) and D2's steps; q4Stats.refused stays the count across every set built.
//   With Q4_PROV unset every added path is inert, so QW4-1's packages must come back byte for byte (the generator's control).
fr = src["frame.ts"];
fr = swap(fr, "export const q4Stats = { refused: 0 };\n",
  "export const q4Stats = { refused: 0 };\nexport const Q4P = { prov: process.env.Q4_PROV === \"1\" }; // SCRATCH Q-W4 (C)\nexport const REFUSED: Pt[] = []; // SCRATCH Q-W4 (C): the marker a refusal returns under Q4_PROV\n", "frame Q4P");
fr = swap(fr, "  if (Q4W.backstop && properPairs(q).length) { q4Stats.refused++; return []; }\n",
  "  if (Q4W.backstop && properPairs(q).length) { q4Stats.refused++; return Q4P.prov ? REFUSED : []; } // SCRATCH Q-W4 (C)\n", "frame refusal");
src["frame.ts"] = fr;

ly = src["layers.ts"];
ly = swap(ly, "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4,\n", "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4, Q4P, REFUSED,\n", "layers import (C)");
ly = swap(ly, "        this.water.push({ ...keyOf(ft), ring: q.length < 3 ? null : q });\n",
  "        this.water.push({ ...keyOf(ft), ring: q.length < 3 ? null : q, refused: q === REFUSED } as any); // SCRATCH Q-W4 (C)\n", "layers push (C)");
ly = swap(ly, "    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring).length };\n",
  "    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring && !(Q4P.prov && (r as any).refused)).length, refused: recs.filter((r) => (r as any).refused).length } as any; // SCRATCH Q-W4 (C)\n", "layers waterRings (C)");
src["layers.ts"] = ly;

let bd = src["build.ts"];
bd = after(bd, "export const q4Plant = { bytes: 0, check: 0 }; // SCRATCH Q-W4: the difference instrument's plant, as the builder made it\n",
  "export const q4Out = { refused: -1, steps: 0 }; // SCRATCH Q-W4 (C, ruling 3): the drawn set's refusals and D2's step count, as bm_build would carry them\n", "build q4Out");
bd = swap(bd, "    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; }\n",
  "    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; (c as any).refusedW = (w as any).refused; } // SCRATCH Q-W4 (C)\n", "build content (C)");
bd = before(bd, "  if (!fitted) return { ok: false, status: 422, code: \"too_large\", ladder };\n",
  "  q4Out.refused = fitted ? ((fitted.content as any).refusedW ?? 0) : -1; // SCRATCH Q-W4 (C): the set the package draws\n", "build q4Out.refused");
bd = swap(bd, "  let r = await buildOnce(store, q, opts);\n", "  q4Out.steps = 1; let r = await buildOnce(store, q, opts); // SCRATCH Q-W4 (ruling 3): steps counted\n", "build steps 1");
bd = swap(bd, "    r = await buildOnce(store, { ...q, rKm: Math.round(q.rKm * 1e6 * 0.8 ** k) / 1e6 }, opts);\n",
  "    q4Out.steps++; r = await buildOnce(store, { ...q, rKm: Math.round(q.rKm * 1e6 * 0.8 ** k) / 1e6 }, opts); // SCRATCH Q-W4 (ruling 3)\n", "build steps k");
src["build.ts"] = bd;

let bg = src["budget.ts"];
bg = swap(bg, "    const c2 = { ...content, water: water8 } as C;\n",
  "    const m8 = make(toleranceMm * 8) as any; // SCRATCH Q-W4 (C): under Q4_PROV the drawn set's counts travel with its rings\n" +
  "    const c2 = (process.env.Q4_PROV === \"1\" ? { ...content, water: water8, collapsed: { ...(content as any).collapsed, water: m8.collapsed?.water }, refusedW: m8.refusedW } : { ...content, water: water8 }) as C;\n", "budget c2 (C)");
bg = swap(bg, "    const c4 = (): C => { const m4 = make(tol4) as any; return { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } } as C; };\n",
  "    const c4 = (): C => { const m4 = make(tol4) as any; return (process.env.Q4_PROV === \"1\" ? { ...m4, water: water8, collapsed: { ...m4.collapsed, water: m8.collapsed?.water }, refusedW: m8.refusedW } : { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } }) as C; }; // SCRATCH Q-W4 (C)\n", "budget c4 (C)");
src["budget.ts"] = bg;

// ---- SCRATCH Q-W4 part 2, pq4d: the SEA PATH behind Q4_SEA=1 (docs/basemap-dense-code.md, #652; the 04:12:32Z ruling).
//   sea.ts (pq4d-sea.part.ts, verbatim) holds the path. store.ts gains kind sea = 6 (attrs: area, src); provenance.ts gains the sea
//   layer after water; LayerSet keeps sea parts and draws them into its water rings; the builder routes sea features to the water sets
//   (eps, x2, x8) and reports the drawn set's sea counts, the pieces the ladder dropped, the sea's package bytes and every piece's fate.
//   With Q4_SEA unset, and on a store with no sea feature, every added path is inert (pq4d's own control, registered with part 2).
src["sea.ts"] = readFileSync(new URL("./pq4d-sea.part.ts", import.meta.url), "utf8").replace(/\r/g, "");
files.push("sea.ts");
// the 06:35:32Z ruling (KB8): frame.ts gains the edge-locked simplify (pq4d-frame-lock.part.ts, verbatim), before waterQ4
fr = src["frame.ts"];
fr = before(fr, "export function waterQ4(ringKm: Pt[], epsM: number, rMm: number): Pt[] {\n",
  readFileSync(new URL("./pq4d-frame-lock.part.ts", import.meta.url), "utf8").replace(/\r/g, ""), "frame waterQ4Locked");
src["frame.ts"] = fr;

let st = src["store.ts"];
st = swap(st, 'export type FeatureKind = "water" | "rivers" | "roads" | "peaks" | "places";\n',
  'export type FeatureKind = "water" | "rivers" | "roads" | "peaks" | "places" | "sea"; // SCRATCH Q-W4 part 2: sea, kind 6\n', "store kind type");
st = swap(st, "const KIND_CODE: Record<FeatureKind, number> = { water: 1, rivers: 2, roads: 3, peaks: 4, places: 5 };\n",
  "const KIND_CODE: Record<FeatureKind, number> = { water: 1, rivers: 2, roads: 3, peaks: 4, places: 5, sea: 6 }; // SCRATCH Q-W4 part 2\n", "store KIND_CODE");
st = swap(st, 'const KIND_OF: Record<number, FeatureKind> = { 1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places" };\n',
  'const KIND_OF: Record<number, FeatureKind> = { 1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places", 6: "sea" }; // SCRATCH Q-W4 part 2\n', "store KIND_OF");
st = after(st, '  else if (f.kind === "peaks") { nums.push(Number(f.attrs["feature_id"])); t(f.attrs["feature_name"]); }\n',
  '  else if (f.kind === "sea") { nums.push(Number(f.attrs["area"])); t(f.attrs["src"]); } // SCRATCH Q-W4 part 2\n', "store encode sea");
st = after(st, "    : kind === \"peaks\" ? { feature_id: nums[0]!, feature_name: texts[0]! }\n",
  "    : kind === \"sea\" ? { area: nums[0]!, src: texts[0]! } // SCRATCH Q-W4 part 2\n", "store decode sea");
src["store.ts"] = st;

let pv = src["provenance.ts"];
pv = swap(pv, 'export const LAYER_ORDER = ["water", "rivers", "roads", "peaks", "places", "population", "terrain"] as const;\n',
  'export const LAYER_ORDER = ["water", "sea", "rivers", "roads", "peaks", "places", "population", "terrain"] as const; // SCRATCH Q-W4 part 2: + sea\n', "provenance LAYER_ORDER");
src["provenance.ts"] = pv;

ly = src["layers.ts"];
ly = swap(ly, "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4, Q4P, REFUSED,\n} from \"./frame\";\n",
  "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4, Q4P, REFUSED,\n} from \"./frame\";\nimport { type SeaCounts, type SeaOut, type SeaPart, newSeaCounts, seaRings, Q4S } from \"./sea\"; // SCRATCH Q-W4 part 2\n", "layers import sea");
ly = swap(ly, 'export type LayerKind = "water" | "rivers" | "roads" | "peaks" | "places";\n',
  'export type LayerKind = "water" | "rivers" | "roads" | "peaks" | "places" | "sea"; // SCRATCH Q-W4 part 2\n', "layers LayerKind");
ly = after(ly, "  private places: Array<Key & Place> = [];\n",
  "  private sea: SeaPart[] = []; // SCRATCH Q-W4 part 2: sea parts, unclipped, in the frame\n  seaN: SeaCounts = newSeaCounts(); seaOut: SeaOut[] | null = null; // SCRATCH Q-W4 part 2\n", "layers sea fields");
ly = before(ly, "      case \"rivers\": {\n",
  "      case \"sea\": { // SCRATCH Q-W4 part 2: kept whole; seaRings() clips, simplifies, bridges and draws them\n" +
  "        this.sea.push({ file: ft.file, record: ft.record, part: ft.part, km: openRing(lonLat(ft.xy)).map(([x, y]) => toFrameKm(f, x, y)),\n" +
  "          lock: openRing(lonLat(ft.xy)).map(([x, y]) => Math.abs(x - Math.round(x)) < 1e-9 || Math.abs(y - Math.round(y)) < 1e-9) }); // KB8 (06:35:32Z): on its cell's boundary\n" +
  "        return;\n      }\n", "layers case sea");
ly = swap(ly, "    const recs = [...this.water].sort(bySource);\n    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring && !(Q4P.prov && (r as any).refused)).length, refused: recs.filter((r) => (r as any).refused).length } as any; // SCRATCH Q-W4 (C)\n",
  "    if (Q4S.on && this.seaOut === null) this.seaOut = seaRings(this.sea, this.f, this.seaN); // SCRATCH Q-W4 part 2\n" +
  "    const seaRecs = (this.seaOut ?? []).flatMap((s) => (s.ring ? [{ file: s.file, record: s.record, part: 0, ring: s.ring }, ...(s.plantedRings ?? []).map((r, i) => ({ file: s.file, record: s.record, part: i + 1, ring: r }))] : []));\n" +
  "    const recs = [...this.water, ...seaRecs].sort(bySource);\n" +
  "    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring && !(Q4P.prov && (r as any).refused)).length, refused: recs.filter((r) => (r as any).refused).length,\n" +
  "      sea: { n: this.seaN, fates: this.seaOut ?? [], set: new Set(seaRecs.map((r) => r.ring)) } } as any; // SCRATCH Q-W4 (C), part 2\n", "layers waterRings sea");
src["layers.ts"] = ly;

bd = src["build.ts"];
bd = after(bd, "import { type Box, CURRENT_KEY, type StoreIndex, boxMeets, covered, decodeCell, decodeTerrariumPng, storeKeys, tileBitsOf } from \"./store\";\n",
  "import { Q4S, SeaRepairStop, ringBytes } from \"./sea\"; // SCRATCH Q-W4 part 2; KB4 (B')\n", "build import sea");
bd = swap(bd, "export const q4Out = { refused: -1, steps: 0 }; // SCRATCH Q-W4 (C, ruling 3): the drawn set's refusals and D2's step count, as bm_build would carry them\n",
  "export const q4Out = { refused: -1, steps: 0, sea: null as any }; // SCRATCH Q-W4 (C, ruling 3; part 2's sea): what bm_build would carry\n", "build q4Out sea");
bd = swap(bd, "  const kinds = ([\"water\", \"rivers\", \"roads\", \"peaks\", \"places\"] as const).filter((k) => want(MASK[k]));\n",
  "  const kinds = [...([\"water\", \"rivers\", \"roads\", \"peaks\", \"places\"] as const).filter((k) => want(MASK[k])), ...(Q4S.on && want(MASK.water) ? [\"sea\" as const] : [])]; // SCRATCH Q-W4 part 2\n", "build kinds sea");
bd = swap(bd, "          if (ft.kind === \"water\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft);\n",
  "          if (ft.kind === \"water\" || ft.kind === \"sea\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft); // SCRATCH Q-W4 part 2: + sea\n", "build L2 sea");
bd = swap(bd, "          if (L8 && ft.kind === \"water\") L8.add(ft.kind, ft); // SCRATCH Q-W4\n",
  "          if (L8 && (ft.kind === \"water\" || ft.kind === \"sea\")) L8.add(ft.kind, ft); // SCRATCH Q-W4, part 2: + sea\n", "build L8 sea");
bd = swap(bd, "    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; (c as any).refusedW = (w as any).refused; } // SCRATCH Q-W4 (C)\n",
  "    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; (c as any).refusedW = (w as any).refused; (c as any).seaW = (w as any).sea; if ((w as any).sea?.fates.length) (c.collapsed as any).sea = (w as any).sea.n.collapsed; } // SCRATCH Q-W4 (C), part 2\n", "build content sea");
bd = after(bd, "  q4Out.refused = fitted ? ((fitted.content as any).refusedW ?? 0) : -1; // SCRATCH Q-W4 (C): the set the package draws\n",
  "  if (Q4S.on) { // SCRATCH Q-W4 part 2: the drawn set's sea, the pieces the ladder dropped, the sea's bytes, every piece's fate\n" +
  "    const sw = fitted ? (fitted.content as any).seaW : null, set: Set<unknown> = sw?.set ?? new Set(), fin: unknown[] = fitted ? ((fitted.content as any).water ?? []) : [];\n" +
  "    const present = new Set(fin.filter((r) => set.has(r)));\n" +
  "    const fates = (sw?.fates ?? []).map((s: any) => ({ file: s.file, record: s.record, fate: s.fate === \"drawn\" && !present.has(s.ring) ? \"dropped\" : s.fate, q: s.q }));\n" +
  "    const ringIdx = fin.flatMap((r, i) => (set.has(r) ? [i] : [])); // the sea rings' positions in the package's water section\n" +
  "    q4Out.sea = { ...(sw?.n ?? {}), dropped: fates.filter((x: any) => x.fate === \"dropped\").length, bytes: [...present].reduce((a: number, r: any) => a + ringBytes(r), 0), fates, ringIdx };\n" +
  "  }\n", "build q4Out.sea");
bd = before(bd, "      if (want(MASK.places)) for (const l of [\"places\", \"population\"] as const) read[l] = manifest.files.filter((f) => f.layer === l).map((f) => fileRef(f.url));\n",
  "      if (Q4S.on && want(MASK.water)) { const s = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === \"sea\").sort().map(fileRef); if (s.length) read.sea = s; } // SCRATCH Q-W4 part 2 (05:11:57Z ruling): the sea's files read (RD-P6), only when one is met\n", "build read.sea");
// KB4 (B') (registered at cf2f65300e89): a sea repair that cannot finish STOPS the build: 500 with its code, never a partial package.
bd = swap(bd, "  | { ok: false; status: 422; code: \"too_large\"; ladder: Ladder };\n",
  "  | { ok: false; status: 422; code: \"too_large\"; ladder: Ladder }\n  | { ok: false; status: 500; code: \"sea_repair_failed\" | \"sea_repair_limit\" | \"sea_repair_state\"; why: string }; // SCRATCH KB4 (B')\n", "build result 500");
bd = swap(bd, "  const at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;\n  if (L4) at[4 * q.epsMm] = content(L4); if (L8) at[8 * q.epsMm] = content(L8); // SCRATCH Q-W4\n",
  "  let at: Record<number, Content>;\n  try { // SCRATCH KB4 (B'): a sea repair that cannot finish stops the build here\n" +
  "    at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;\n" +
  "    if (L4) at[4 * q.epsMm] = content(L4); if (L8) at[8 * q.epsMm] = content(L8); // SCRATCH Q-W4\n" +
  "  } catch (e) { if (e instanceof SeaRepairStop) return { ok: false, status: 500, code: e.code, why: e.message }; throw e; }\n", "build content stop");
src["build.ts"] = bd;

bg = src["budget.ts"];
bg = swap(bg, "    const c2 = (process.env.Q4_PROV === \"1\" ? { ...content, water: water8, collapsed: { ...(content as any).collapsed, water: m8.collapsed?.water }, refusedW: m8.refusedW } : { ...content, water: water8 }) as C;\n",
  "    const c2 = (process.env.Q4_PROV === \"1\" ? { ...content, water: water8, collapsed: { ...(content as any).collapsed, water: m8.collapsed?.water, sea: m8.collapsed?.sea }, refusedW: m8.refusedW, seaW: m8.seaW } : { ...content, water: water8 }) as C; // SCRATCH Q-W4 part 2: + sea\n", "budget c2 sea");
bg = swap(bg, "    const c4 = (): C => { const m4 = make(tol4) as any; return (process.env.Q4_PROV === \"1\" ? { ...m4, water: water8, collapsed: { ...m4.collapsed, water: m8.collapsed?.water }, refusedW: m8.refusedW } : { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } }) as C; }; // SCRATCH Q-W4 (C)\n",
  "    const c4 = (): C => { const m4 = make(tol4) as any; return (process.env.Q4_PROV === \"1\" ? { ...m4, water: water8, collapsed: { ...m4.collapsed, water: m8.collapsed?.water, sea: m8.collapsed?.sea }, refusedW: m8.refusedW, seaW: m8.seaW } : { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } }) as C; }; // SCRATCH Q-W4 (C), part 2: + sea\n", "budget c4 sea");
src["budget.ts"] = bg;

for (const f of files) writeFileSync(join(out, "src", "basemap", f), src[f]);
console.log(`pq4 written: ${files.length} basemap files + wx/crop.ts; build.ts +${src["build.ts"].split("\n").length - git("show", `${P}:proxy/src/basemap/build.ts`).toString().split("\n").length} lines`);
// ---- SCRATCH Q-W4, pq4c: the synthetic-store module and its import closure, byte for byte from the pinned commit, so the fixture
// stores (QW4-3's bowtie, T-SEA0's sea) use the pinned encoders. basemap-synth.ts's "../src/basemap/..." resolves to this tree's own.
mkdirSync(join(out, "test"), { recursive: true }); mkdirSync(join(out, "scripts", "basemap"), { recursive: true });
for (const [from, to] of [["proxy/test/basemap-synth.ts", "test/basemap-synth.ts"], ...["ingest-lib", "coverage", "tile-bits", "terrain-plan"].map((m) => [`proxy/scripts/basemap/${m}.ts`, `scripts/basemap/${m}.ts`])]) {
  const b = git("show", `${P}:${from}`);
  if (b.includes(13)) die(`${from} has CR`);
  writeFileSync(join(out, ...to.split("/")), b);
}
console.log("pq4c: + test/basemap-synth.ts and 4 scripts/basemap modules, from the pinned commit");
console.log("OK");
