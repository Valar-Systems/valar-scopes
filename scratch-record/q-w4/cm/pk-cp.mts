// SCRATCH, read-only: the 5.6 package rows' code-PR child (docs/basemap-dense-code.md, "The code PR's package row, narrowed point by
// point"). dr-q4c3.mts (b02b5a66d240, QW4-C's child) with the builder taken from mk-cm.mjs's code-PR tree (cm/cp, from its commit) instead
// of a prototype, no knobs (it has none), and the water section's proper crossings added. The digest per section (PROVENANCE apart) is
// dr-q4c3's expression, unchanged, so the two children's digests compare. Prints ONE line: "RESULT " + JSON.
//   (from wt-main/proxy) node --import tsx ../../cm/pk-cp.mts <obj dir> <ap>
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const [objDir, ap] = process.argv.slice(2);
const fail = (why: string, code = 2) => { console.log(`RESULT ${JSON.stringify({ ap, which: "cp", error: why })}`); process.exit(code); };
const AIR: Record<string, [string, string]> = {
  KBOS: ["42.36", "-71.01"], KJFK: ["40.64", "-73.78"], KLGA: ["40.78", "-73.87"], KEWR: ["40.69", "-74.17"], MIDTOWN: ["40.75", "-73.99"], KMIA: ["25.79", "-80.29"], KSFO: ["37.62", "-122.38"],
  KSEA: ["47.45", "-122.31"], KORD: ["41.98", "-87.90"], KDTW: ["42.21", "-83.35"], KMSP: ["44.88", "-93.22"], KIND: ["39.72", "-86.29"], KJAC: ["43.61", "-110.74"],
};
const T = new URL("./cp/proxy/src/basemap/", import.meta.url);
const B = await import(new URL("build.ts", T).href), F = await import(new URL("format.ts", T).href), FR = await import(new URL("frame.ts", T).href);
const Q = await import(new URL("query.ts", T).href), BG = await import(new URL("budget.ts", T).href);
if (typeof B.build !== "function" || typeof F.decode !== "function" || typeof Q.parseQuery !== "function" || typeof FR.properPairs !== "function" || typeof BG.nonTerrainBytes !== "function") fail("the tree lacks an export");
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
const out: any = { ap, which: "cp", missing, ok: r.ok, status: r.ok ? 200 : r.status, report: r.report ?? null };
if (r.ok) {
  const pkg = F.decode(r.bytes) as any;
  const prov = pkg.sections.find((s: any) => s.text)?.text, provText = prov ? (typeof prov === "string" ? prov : new TextDecoder().decode(prov)) : "";
  const digests: Record<string, string> = {};
  pkg.sections.forEach((s: any, i: number) => { if (!s.text) digests[`${i}:${s.type}`] = sha12(JSON.stringify(s, (_k, v) => (ArrayBuffer.isView(v) ? Array.from(v as any) : v))); });
  const wr = (pkg.sections.find((s: any) => s.type === 1)?.rings ?? []) as any[];
  Object.assign(out, { st: F.validate(r.bytes).status, rung: r.ladder.rung, radiusMm: pkg.radiusMm, toleranceMm: pkg.toleranceMm, sha: sha12(r.bytes), digests,
    ntb: BG.nonTerrainBytes(pkg.sections), provBytes: new TextEncoder().encode(provText).length, prov: provText.split("\n"),
    wCross: wr.reduce((a: number, ring: any) => a + FR.properPairs(ring.map((pt: any) => [pt[0], pt[1]])).length, 0) });
}
console.log(`RESULT ${JSON.stringify(out)}`);
process.exit(0);
