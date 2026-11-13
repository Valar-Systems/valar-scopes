// SCRATCH, read-only: QW4-C0 and QW4-C1's child (docs/basemap-dense-code.md, #652, addendum part 1). ONE build per process of pq4b
// (mk-pq4b.mjs) or pq4c (mk-pq4c.mjs), chosen by argument and imported alone, at one of the 13 points, at the client's query, from the
// local store. No memory instrument: these rows grade bytes. The knobs come from the environment, read at import, and are reported
// as the modules read them. Prints ONE line: "RESULT " + JSON: the package's sha, a digest per section (PROVENANCE apart), and
// PROVENANCE's lines, which hold public source urls and sha256s and nothing else.
//   (from wt-main/proxy) [knobs] node --import tsx ../../dr-q4c3.mts <obj dir> <ap> <b|c>
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const [objDir, ap, which] = process.argv.slice(2);
const fail = (why: string, code = 2) => { console.log(`RESULT ${JSON.stringify({ ap, which, error: why })}`); process.exit(code); };
if (which !== "b" && which !== "c") fail("argv[4] must be b or c");
const AIR: Record<string, [string, string]> = {
  KBOS: ["42.36", "-71.01"], KJFK: ["40.64", "-73.78"], KLGA: ["40.78", "-73.87"], KEWR: ["40.69", "-74.17"], MIDTOWN: ["40.75", "-73.99"], KMIA: ["25.79", "-80.29"], KSFO: ["37.62", "-122.38"],
  KSEA: ["47.45", "-122.31"], KORD: ["41.98", "-87.90"], KDTW: ["42.21", "-83.35"], KMSP: ["44.88", "-93.22"], KIND: ["39.72", "-86.29"], KJAC: ["43.61", "-110.74"],
};
const T = which === "b" ? "./pq4b" : "./pq4c";
const B = await import(`${T}/src/basemap/build.ts`), F = await import(`${T}/src/basemap/format.ts`), FR = await import(`${T}/src/basemap/frame.ts`), Q = await import(`${T}/src/basemap/query.ts`);
const BG = await import(`${T}/src/basemap/budget.ts`);
if (typeof B.build !== "function" || typeof F.decode !== "function" || typeof Q.parseQuery !== "function" || !FR.q4Stats || typeof BG.nonTerrainBytes !== "function") fail(`${T} lacks an export`);
if (!AIR[ap!]) fail(`unknown point ${ap}`); // after the imports and exports, so a refused point proves both
const QUERY = "r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242";
let missing = 0;
const local = (key: string) => join(objDir!, key.replace(/^bm\/v1\/(?:[0-9a-f]{12}\/)?/, ""));
const store = { async get(key: string) { try { return new Uint8Array(readFileSync(local(key))); } catch { missing++; return null; } } };
const [lat, lon] = AIR[ap!]!;
const p = Q.parseQuery(new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&${QUERY}`));
if (!p.ok) fail(p.code);
const q = { lat0Micro: p.q.lat0Micro, lon0Micro: p.q.lon0Micro, rKm: p.q.rKm, epsMm: p.q.epsMm, mask: p.q.mask, levels: p.q.levels };
const r = await B.build(store, q, { commit: "0".repeat(40) });
const sha12 = (b: Uint8Array | string) => createHash("sha256").update(b).digest("hex").slice(0, 12);
const out: any = { ap, which, knobs: { ...B.Q4, ...FR.Q4W, ...(FR.Q4P ?? {}), env: { ladder: process.env.Q4_LADDER ?? "", d2: process.env.Q4_D2 ?? "", tp: process.env.Q4_TP ?? "", backstop: process.env.Q4_BACKSTOP ?? "", prov: process.env.Q4_PROV ?? "" } },
  missing, ok: r.ok, status: r.ok ? 200 : r.status, refused: B.q4Out?.refused ?? null, refusedBuilt: FR.q4Stats.refused, steps: B.q4Out?.steps ?? null };
if (r.ok) {
  const pkg = F.decode(r.bytes) as any;
  const prov = pkg.sections.find((s: any) => s.text)?.text, provText = prov ? (typeof prov === "string" ? prov : new TextDecoder().decode(prov)) : "";
  const digests: Record<string, string> = {};
  pkg.sections.forEach((s: any, i: number) => { if (!s.text) digests[`${i}:${s.type}`] = sha12(JSON.stringify(s, (_k, v) => (ArrayBuffer.isView(v) ? Array.from(v as any) : v))); });
  Object.assign(out, { st: F.validate(r.bytes).status, rung: r.ladder.rung, radiusMm: pkg.radiusMm, toleranceMm: pkg.toleranceMm, sha: sha12(r.bytes), digests,
    ntb: BG.nonTerrainBytes(pkg.sections), provBytes: new TextEncoder().encode(provText).length, prov: provText.split("\n") });
}
console.log(`RESULT ${JSON.stringify(out)}`);
process.exit(0);
