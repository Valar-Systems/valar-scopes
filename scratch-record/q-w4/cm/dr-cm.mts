// SCRATCH, read-only: the code PR's memory and run child (docs/basemap-dense-code.md: CM-0/CM-P/CM-M and the memory rows, SEA-MEM, R-D2,
// R-KMSP). dr-p2m.mts (7814abed9746) with three changes and nothing else in the instrument:
//   (1) the builder is a tree mk-cm.mjs made from a COMMIT: "cp" (the code PR's, its own phase hook `buildHooks.phase` set here) or
//       "main" (the merge-base's, its seven SCRATCH lines calling globalThis.__mem). Both hooks get the same function.
//   (2) the plant is held by THIS child, not by a builder knob (the code PR has none): <plant MiB> bytes, filled with 5, made after the
//       baseline and before the build; at the first phase call that is not "cell" (the first sample past the cell loop) it is read
//       back (first + last byte, so 10) and dropped BEFORE that sample is taken. CM-P's and SEA-MEM's control use 8.
//   (3) mode "nocap8" (R-KMSP's comparison build): epsMm x 8 and capBytes = Number.MAX_SAFE_INTEGER, so rung 0 holds no matter the bytes.
// The controls (C1, C2', C3), the settled sampler, the phases, the per-step peaks (a "d2" call starts a step) are dr-p2m's, unchanged.
// Prints ONE line: "RESULT " + JSON, counts only.
//   (from wt-main/proxy) node --expose-gc --import tsx ../../cm/dr-cm.mts <cp|main> <sea|orig> <AP> <plant MiB> <client|nocap8>
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const [tree, which, ap, plantArg, mode] = process.argv.slice(2);
const fail = (why: string, code = 2): never => { console.log(`RESULT ${JSON.stringify({ ap, error: why })}`); process.exit(code); };
if (tree !== "cp" && tree !== "main") fail("argv[2] must be cp or main");
const DIRS = which === "sea" ? ["../../sea-obj"] : which === "orig" ? ["../../bm-obj", "../../lake-obj", "../../zap-obj"] : fail("argv[3] must be sea or orig");
const AIR: Record<string, [string, string]> = {
  KBOS: ["42.36", "-71.01"], KJFK: ["40.64", "-73.78"], KLGA: ["40.78", "-73.87"], KEWR: ["40.69", "-74.17"], MIDTOWN: ["40.75", "-73.99"], KMIA: ["25.79", "-80.29"], KSFO: ["37.62", "-122.38"],
  KSEA: ["47.45", "-122.31"], KORD: ["41.98", "-87.90"], KDTW: ["42.21", "-83.35"], KMSP: ["44.88", "-93.22"], KIND: ["39.72", "-86.29"], KJAC: ["43.61", "-110.74"],
  BUFFALO: ["42.886", "-78.879"], ROUSES: ["44.994", "-73.365"], ZAPATA: ["26.91", "-99.27"],
};
const plantMiB = Number(plantArg ?? "x");
if (!Number.isInteger(plantMiB) || plantMiB < 0 || plantMiB > 64) fail("plant MiB must be an integer 0..64");
if (mode !== "client" && mode !== "nocap8") fail("argv[6] must be client or nocap8");
const gc = (globalThis as any).gc as (() => void) | undefined;
if (!gc) fail("no gc()", 3);
const T = new URL(`./${tree}/proxy/src/basemap/`, import.meta.url);
const Bm = await import(new URL("build.ts", T).href), Fm = await import(new URL("format.ts", T).href), Gm = await import(new URL("budget.ts", T).href);
const Qm = await import(new URL("query.ts", T).href), Rm = await import(new URL("frame.ts", T).href);
if (Bm.READ_AHEAD !== 6) fail(`READ_AHEAD is ${Bm.READ_AHEAD}`);
if (tree === "cp" && !(Bm.buildHooks && "phase" in Bm.buildHooks)) fail("the code PR's tree has no buildHooks");
if (tree === "main" && Bm.buildHooks) fail("main's tree has a buildHooks export");
if (!AIR[ap!]) fail(`unknown point ${ap}`); // after the imports, so a refused point proves the tree resolved
const properPairs = Rm.properPairs as ((q: Array<[number, number]>) => unknown[]) | undefined;
const QUERY = "r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242";
const MiB = 1024 * 1024;
let missing = 0;
const store = { async get(key: string) { const rel = key.replace(/^bm\/v1\/(?:[0-9a-f]{12}\/)?/, ""); for (const d of DIRS) { try { return new Uint8Array(readFileSync(join(d, rel))); } catch { /* the next */ } } missing++; return null; } };
const raw = () => { gc!(); const m = process.memoryUsage(); return m.heapUsed + m.arrayBuffers; };
let unsettled = 0;
function sample(): number {
  let prev = raw(), calls = 1;
  while (calls < 4) { const x = raw(); calls++; if (Math.abs(x - prev) <= 0.1 * MiB) return x; prev = x; }
  unsettled++; return prev;
}
const at32 = (x: number) => Math.abs(x - 32) <= 1, at0 = (x: number) => Math.abs(x) <= 1;
function controls(): [number, number] {
  const a = sample(); let hold: Uint8Array | null = new Uint8Array(32 * MiB).fill(1); const b = sample();
  hold = null;
  const e = sample();
  ((): void => { const dead = new Uint8Array(32 * MiB).fill(2); if (dead[0] !== 2) throw new Error("unreachable"); })();
  const f = sample();
  return [(b - a) / MiB, (f - e) / MiB];
}
let slot: Uint8Array | null = null;
function put(): void { slot = new Uint8Array(32 * MiB).fill(3); }
function drop(): void { slot = null; }
function c2prime(): [number, number] { const a = sample(); put(); const b = sample(); drop(); const c = sample(); return [(b - a) / MiB, (c - a) / MiB]; }

const [lat, lon] = AIR[ap!]!;
const p = Qm.parseQuery(new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&${QUERY}`));
if (!p.ok) fail(p.code);
const q = { lat0Micro: p.q.lat0Micro, lon0Micro: p.q.lon0Micro, rKm: p.q.rKm, epsMm: (mode === "nocap8" ? 8 : 1) * p.q.epsMm, mask: p.q.mask, levels: p.q.levels };
const opts: { commit: string; capBytes?: number } = { commit: "0".repeat(40) };
if (mode === "nocap8") opts.capBytes = Number.MAX_SAFE_INTEGER;

let peak = NaN, peakTag = "-";
const phases: Record<string, number> = {}, stepPeaks: number[] = [NaN];
const [c1, c3] = controls(); const [pl, pr] = c2prime(); const ctl = [c1, pl, pr, c3];
const base = sample();
let plant: Uint8Array | null = null, plantMade = 0, plantRead = -1;
const hookFn = (tag: string) => {
  if (plant && tag !== "cell") { plantRead = plant[0]! + plant[plant.length - 1]!; plant = null; }
  if (tag === "d2") stepPeaks.push(NaN);
  const x = (sample() - base) / MiB, k = stepPeaks.length - 1;
  if (!(x <= stepPeaks[k]!)) stepPeaks[k] = x;
  if (!(x <= peak)) { peak = x; peakTag = tag; }
  if (!(x <= phases[tag]!)) phases[tag] = x;
};
(globalThis as any).__mem = hookFn;
if (tree === "cp") Bm.buildHooks.phase = hookFn;
if (plantMiB > 0) { plant = new Uint8Array(plantMiB * MiB).fill(5); plantMade = plant.length; }
const t0 = Date.now();
const r = await Bm.build(store, q, opts);
const ms = Date.now() - t0;
(globalThis as any).__mem = undefined;
if (tree === "cp") Bm.buildHooks.phase = null;
const plantHeldToEnd = plant !== null; plant = null;
{ const x = (sample() - base) / MiB; if (!(x <= peak)) { peak = x; peakTag = "end"; } phases.end = x; }
const ctlOk = at32(ctl[0]!) && at32(ctl[1]!) && at0(ctl[2]!) && at0(ctl[3]!);
const out: any = { ap, tree, which, mode, plantMiB, plantMade, plantRead, plantHeldToEnd, ctl, ctlOk, unsettled, missing, peak, peakTag, phases, stepPeaks, ms,
  ok: r.ok, status: r.ok ? 200 : r.status, code: r.ok ? null : (r.code ?? null) };
if (!r.ok && r.status === 422) { out.rung = r.ladder.rung; out.nt = r.ladder.nt; }
if (r.report) out.report = { radii: r.report.radii, refusedBuilt: r.report.refusedBuilt, refused: r.report.refused ?? null };
if (r.ok) {
  const pkg = Fm.decode(r.bytes) as any;
  const wr = (pkg.sections.find((s: any) => s.type === 1)?.rings ?? []) as any[];
  const cr = properPairs ? wr.map((ring) => properPairs(ring.map((pt: any) => [pt[0], pt[1]])).length) : null;
  const prov = pkg.sections.find((s: any) => s.text)?.text, provText: string = prov ? (typeof prov === "string" ? prov : new TextDecoder().decode(prov)) : "";
  Object.assign(out, { rung: r.ladder.rung, sha: createHash("sha256").update(r.bytes).digest("hex").slice(0, 12), st: Fm.validate(r.bytes).status, ntb: Gm.nonTerrainBytes(pkg.sections), bytes: r.bytes.length,
    radiusMm: pkg.radiusMm, toleranceMm: pkg.toleranceMm, wRings: wr.length, wCross: cr ? cr.reduce((a: number, b: number) => a + b, 0) : null,
    prov: Object.fromEntries(provText.split("\n").filter((l: string) => /^(dropped|collapsed)\./.test(l)).map((l: string) => [l.split(": ")[0], Number(l.split(": ")[1])])),
    provBytes: new TextEncoder().encode(provText).length });
}
console.log(`RESULT ${JSON.stringify(out)}`);
process.exit(0);
