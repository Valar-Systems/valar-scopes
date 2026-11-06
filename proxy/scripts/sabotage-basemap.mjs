// Sabotages for the basemap route's tests (docs/basemap-route.md A1.7: "the sabotage rule").
//
//   node scripts/sabotage-basemap.mjs            # every sabotage
//   node scripts/sabotage-basemap.mjs S-IBUS     # one
//
// For each: assert every anchor occurs EXACTLY once and that the file changed (a plant that did not take
// proves nothing); run the basemap tests; grade the set of failing CASES (the id each test name begins with)
// against the frozen set EXACTLY; restore the file from a byte copy and check its sha256. A clean baseline
// run comes first: with nothing planted, nothing may fail. Exit 0 only when every sabotage lands exactly.
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const LIB = "scripts/basemap/ingest-lib.ts";
const FORMAT = "src/basemap/format.ts";
const FRAME = "src/basemap/frame.ts";
const LAYERS = "src/basemap/layers.ts";
const TERRAIN = "src/basemap/terrain.ts";
const BUDGET = "src/basemap/budget.ts";
const QUERY = "src/basemap/query.ts";
const HANDLER = "src/basemap/handler.ts";
const BUILD = "src/basemap/build.ts";
const WORKER = "src/basemap/worker.ts";
const ATTRIBUTION = "../docs/basemap-attribution.md";
// Each sabotage runs against a test set: its files, and optionally a vitest -t filter (a family), with its own
// clean baseline. A filter narrows what is graded, so a sabotage graded under one says so in its output.
// "all" is a path filter, not a list, so a basemap test file added later is graded without anyone remembering to.
const SETS = {
  ingest: { files: ["test/basemap-ingest.test.ts"] },
  mv: { files: ["test/basemap-format.test.ts"], filter: "MV" },
  all: { files: ["test/basemap-"] },
};

/** name: { file, edits: [[anchor, replacement]], expect: [case ids] } -- the sets are A1.7's, frozen. */
const SABOTAGES = {
  "S-ISIZE": {
    file: LIB,
    edits: [["    if (got.length !== o.bytes.length || (await sha256Hex(got)) !== (await sha256Hex(o.bytes))) mismatched.push(o.key);\n",
             "    if (got.length !== o.bytes.length) mismatched.push(o.key); // SABOTAGE S-ISIZE: size only\n"]],
    // Re-registered by ruling (docs/basemap-a1.md, A1's first run, ~20:05Z), the miss kept: {MI7}. Under a sizes-only
    // verify, MI9's same-size corruption seals and bm:store is written, so MI9 is right to go red too.
    expect: ["MI7", "MI9"],
  },
  "S-ISEAL": {
    file: LIB,
    edits: [["  for (const o of objects) await bucket.put(o.key, o.bytes);\n  let v = await verifyStore(bucket, objects);\n",
             "  for (const o of objects) await bucket.put(o.key, o.bytes);\n  await bucket.put(currentKey, currentBody); // SABOTAGE S-ISEAL: sealed before verifying\n  let v = await verifyStore(bucket, objects);\n"]],
    expect: ["MI7"],
  },
  "S-INOTESORT": {
    file: LIB,
    edits: [['  for (let i = 1; i < notes.length; i++) if (byteCmp(notes[i - 1], notes[i]) >= 0) throw new Error("manifest notes must be sorted, in byte order");\n',
             '  notes.sort(byteCmp); // SABOTAGE S-INOTESORT: sorted for the caller instead of refused\n']],
    expect: ["MI4b"],
  },
  "S-IKEYS": {
    file: LIB,
    edits: [['  if (top !== "builder,files,predicates" && top !== "builder,files,notes,predicates") throw new Error(`manifest keys ${top}`);\n',
             '  void top; // SABOTAGE S-IKEYS: any top-level key accepted\n']],
    expect: ["MI4", "MI4b"],
  },
  "S-VCRC": {
    file: FORMAT, set: "mv",
    edits: [['    if (packageCrc(b) !== dv.getUint32(28, true)) throw new Bad("kCrc");\n',
             '    // SABOTAGE S-VCRC: the CRC check skipped\n']],
    expect: ["MV/PK-crc"],
  },
  "S-VSWAP": {
    file: FORMAT, set: "mv",
    edits: [['      if (e.off < expect) throw new Bad("kOverlap");\n      if (e.off > expect) throw new Bad("kGap");\n',
             '      if (e.off < expect) throw new Bad("kGap"); // SABOTAGE S-VSWAP\n      if (e.off > expect) throw new Bad("kOverlap");\n']],
    expect: ["MV/PK-gap", "MV/PK-overlap"],
  },
  // A12.15's two counts (A2.44, A2.45): each check removed, graded against every basemap family.
  "S-VTERRCOUNT": {
    file: FORMAT, set: "all",
    edits: [['      if (count !== 1) throw new Bad("kTerrainCount");\n',
             '      // SABOTAGE S-VTERRCOUNT: TERRAIN\'s count not checked\n']],
    expect: ["MV/PK-terrain-count"],
  },
  "S-VPROVCOUNT": {
    file: FORMAT, set: "all",
    edits: [['      if (count !== lines) throw new Bad("kProvenanceCount");\n',
             '      // SABOTAGE S-VPROVCOUNT: PROVENANCE\'s count not checked\n']],
    expect: ["MV/PK-prov-count"],
  },
  "S-IBUS": {
    file: LIB,
    edits: [['    case "roads": return a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200";\n',
             '    case "roads": return (a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200") && !/(^|[^A-Za-z0-9])Bus($|[^A-Za-z0-9])/.test(a["FULLNAME"] ?? ""); // SABOTAGE S-IBUS\n']],
    expect: ["MI1"],
  },
  // A2.5's MR sabotages and A1.7's S-ECLAMP, graded against every basemap family.
  "S-RLBRECOMP": {
    file: FRAME, set: "all",
    edits: [["  const P: Pt = u0 === 0 ? a : [x0 + u0 * dx, y0 + u0 * dy];\n  const Q: Pt = u1 === 1 ? b : [x0 + u1 * dx, y0 + u1 * dy];\n",
             "  const P: Pt = [x0 + u0 * dx, y0 + u0 * dy]; // SABOTAGE S-RLBRECOMP\n  const Q: Pt = [x0 + u1 * dx, y0 + u1 * dy];\n"]],
    expect: ["MR1"],
  },
  "S-RXCUR": {
    file: FRAME, set: "all",
    edits: [["      if (inside(cur)) { if (!inside(prev)) out.push(cross(prev, cur)); out.push(cur); }\n      else if (inside(prev)) out.push(cross(prev, cur));\n",
             "      if (inside(cur)) { if (!inside(prev)) out.push(cross(cur, prev)); out.push(cur); } // SABOTAGE S-RXCUR\n      else if (inside(prev)) out.push(cross(cur, prev));\n"]],
    expect: ["MR2"],
  },
  "S-RORDER": {
    file: FRAME, set: "all",
    edits: [["  for (const [axis, sign] of [[0, -1], [0, 1], [1, -1], [1, 1]] as const) {\n",
             "  for (const [axis, sign] of [[1, -1], [1, 1], [0, -1], [0, 1]] as const) { // SABOTAGE S-RORDER: N, S, W, E\n"]],
    expect: ["MR2"],
  },
  "S-RNOCLOSE": {
    file: FRAME, set: "all",
    edits: [["  if (ring) while (out.length > 1 && out[out.length - 1]![0] === out[0]![0] && out[out.length - 1]![1] === out[0]![1]) out.pop();\n",
             "  void ring; // SABOTAGE S-RNOCLOSE\n"]],
    expect: ["MR3"],
  },
  "S-RGE": {
    file: FRAME, set: "all",
    edits: [["    if (at >= 0 && best > epsM) {", "    if (at >= 0 && best >= epsM) { // SABOTAGE S-RGE\n"]],
    expect: ["MR4"],
  },
  "S-RHYPOT": {
    file: FRAME, set: "all",
    edits: [["export const planarDist = (ex: number, ey: number) => Math.sqrt(ex * ex + ey * ey);\n",
             "export const planarDist = (ex: number, ey: number) => Math.hypot(ex, ey); // SABOTAGE S-RHYPOT\n"]],
    expect: ["MR7"],
  },
  "S-ECLAMP": {
    file: FRAME, set: "all",
    edits: [["    if (n < -QMAX || n > QMAX) throw new VertexRange(`vertex_range: ${n}`);\n",
             "    if (n < -QMAX || n > QMAX) return n < 0 ? -QMAX : QMAX; // SABOTAGE S-ECLAMP\n"]],
    expect: ["ME4"],
  },
  "S-RZERO": {
    file: FRAME, set: "all",
    edits: [["  return s < 0;\n", "  return s <= 0; // SABOTAGE S-RZERO\n"]],
    expect: ["MR5"],
  },
  // A1.7's ML sabotages and A2.5's S-R0M, graded against every basemap family. S-SQUARELEN was frozen as {ML6}
  // (A1.7), ran {ML6, MR6} at aee95a4 as A2.13 predicted, and is re-registered as {ML6, MR6} by ruling (A2.16).
  "S-R0M": {
    file: LAYERS, set: "all",
    edits: [["  const ranked = [...length].filter(([, m]) => m > 0).sort(", "  const ranked = [...length].sort( // SABOTAGE S-R0M\n"]],
    expect: ["MR6"],
  },
  "S-BUSSUB": {
    file: LAYERS, set: "all",
    edits: [["export const isBusRoute = (fullname: string | undefined) => fullname !== undefined && BUS.test(fullname);\n",
             "export const isBusRoute = (fullname: string | undefined) => fullname !== undefined && fullname.includes(\"Bus\"); // SABOTAGE S-BUSSUB\nvoid BUS;\n"]],
    expect: ["ML7"],
  },
  "S-SQUARELEN": {
    file: LAYERS, set: "all",
    // anchor moved with layers.ts's per-feature LayerSet (A2.24); the plant is unchanged: the clipped length in the square
    edits: [["        const lengthM = inDiscLengthM(km, this.f.rKm);\n",
             "        void inDiscLengthM; // SABOTAGE S-SQUARELEN: the clipped length in the square\n" +
             "        const lengthM = clipLine(km, this.f.rKm).reduce((s, run) => s + run.slice(1).reduce((t, p, i) => t + planarDist(p[0] - run[i]![0], p[1] - run[i]![1]) * 1000, 0), 0);\n"]],
    expect: ["ML6", "MR6"],
  },
  "S-DEDUPALL": {
    file: LAYERS, set: "all",
    edits: [["    if (kept.some((k) => within3km(c.p[0] - k.p[0], c.p[1] - k.p[1]))) continue;\n",
             "    if (ranked.slice(0, ranked.indexOf(c)).some((k) => within3km(c.p[0] - k.p[0], c.p[1] - k.p[1]))) continue; // SABOTAGE S-DEDUPALL\n"]],
    expect: ["ML11"],
  },
  "S-DEDUPLT": {
    file: FRAME, set: "all",
    edits: [["export const within3km = (ex: number, ey: number) => planarDist(ex, ey) <= 3.0;\n",
             "export const within3km = (ex: number, ey: number) => planarDist(ex, ey) < 3.0; // SABOTAGE S-DEDUPLT\n"]],
    expect: ["ML11b"],
  },
  "S-PLACESORT": {
    file: LAYERS, set: "all",
    edits: [["  return places;\n", "  return places.sort((a, b) => planarDist(a.q[0], a.q[1]) - planarDist(b.q[0], b.q[1])); // SABOTAGE S-PLACESORT\n"]],
    expect: ["ML13"],
  },
  "S-RIVERTIE": {
    file: LAYERS, set: "all",
    edits: [["b[1] - a[1] || byteCmp(a[0], b[0])", "b[1] - a[1] /* SABOTAGE S-RIVERTIE: a stable sort, so source order */"]],
    expect: ["ML4"],
  },
  // A1.7's MT sabotages. S-TNOMARGIN waits for MB (A2.17), as S-RZERO waited for MR5.
  "S-TALIGN": {
    file: TERRAIN, set: "all",
    edits: [["      const px = x - 2 + b, py = y - 2 + a;\n", "      const px = x - 1 + b, py = y - 1 + a; // SABOTAGE S-TALIGN\n"]],
    expect: ["MT-impulse", "MT-corner"],
  },
  "S-TPAD": {
    file: TERRAIN, set: "all",
    edits: [["      t += s(cx, cy);\n", "      t += cx !== px || cy !== py ? 0 : s(cx, cy); // SABOTAGE S-TPAD: zero padding\n"]],
    expect: ["MT-corner", "MT2a", "MT2b", "MT2c", "MT2d", "MT2e"],
  },
  "S-TROUND": {
    file: TERRAIN, set: "all",
    edits: [["[Math.floor(z9X(lon)), Math.floor(z9Y(lat))]", "[Math.round(z9X(lon)), Math.round(z9Y(lat))] /* SABOTAGE S-TROUND */"]],
    expect: ["MT4"],
  },
  "S-TCOS": {
    file: TERRAIN, set: "all",
    edits: [["export const mppOf = (lat0: number) => 156543.03 * Math.cos(lat0 * PI / 180) / 2 ** 9;\n",
             "export const mppOf = (lat0: number) => 156543.03 / 2 ** 9 + 0 * lat0; // SABOTAGE S-TCOS\n"]],
    expect: ["MT2e"],
  },
  // A1.7's MB sabotages, and S-TNOMARGIN as re-registered by ruling (A2.19): graded with build.ts (A2.26).
  "S-BTWICE": {
    file: BUDGET, set: "all",
    edits: [["export const EPS_STEP = 2;\n", "export const EPS_STEP = 4; // SABOTAGE S-BTWICE\n"]],
    expect: ["MB2", "MB3", "MB5", "MB6", "MB7", "MB8b"],
  },
  "S-DROPORDER": {
    file: BUDGET, set: "all",
    // anchors moved with fitBudget's bisection (A2.37); every plant below is unchanged in what it does
    edits: [["  const wOrder = dropOrder(rings.map(ringArea2));\n", "  const wOrder = dropOrder(rings.map((r) => -ringArea2(r))); // SABOTAGE S-DROPORDER: largest first\n"]],
    expect: ["MB2", "MB7", "MB8b"],
  },
  "S-NOCOUNT": {
    file: BUDGET, set: "all",
    // dropped.water never counts a drop, in every place the bisection sets it
    edits: [["const w = smallestFit(rings.length, (k) => { d.water = k; return", "const w = smallestFit(rings.length, (k) => { d.water = 0; /* SABOTAGE S-NOCOUNT */ return"],
            ["  if (w) { d.water = w.k; return", "  if (w) { d.water = 0; return"],
            ["  d.water = rings.length;\n", "  d.water = 0;\n"]],
    expect: ["MB2", "MB3", "MB5", "MB7", "MB8b"],
  },
  "S-ROADORDER": {
    file: BUDGET, set: "all",
    edits: [["  const rOrder = dropOrder(secondary.map((i) => roadLength(roads[i]!.pts))).map((k) => secondary[k]!);\n",
             "  const rOrder = dropOrder(secondary.map((i) => -roadLength(roads[i]!.pts))).map((k) => secondary[k]!); // SABOTAGE S-ROADORDER: longest first\n"]],
    expect: ["MB3"],
  },
  "S-PRIMARYDROP": {
    file: BUDGET, set: "all",
    edits: [["  if (r) { d.roads = r.k; return { content: withRoads(r.k), toleranceMm: tol, dropped: d, sections: r.got }; }\n  return null;\n",
             "  if (r) { d.roads = r.k; return { content: withRoads(r.k), toleranceMm: tol, dropped: d, sections: r.got }; }\n" +
             "  // SABOTAGE S-PRIMARYDROP: primaries droppable after the secondaries\n" +
             "  const base2 = withRoads(rOrder.length), r2 = base2.roads ?? [];\n" +
             "  const prim = r2.map((x, i) => [x, i] as const).filter(([x]) => x.cls === 1).map(([, i]) => i);\n" +
             "  const pOrder = dropOrder(prim.map((i) => roadLength(r2[i]!.pts))).map((k) => prim[k]!);\n" +
             "  const withPrim = (k: number): C => { const gone = new Set(pOrder.slice(0, k)); return { ...base2, roads: r2.filter((_, i) => !gone.has(i)) }; };\n" +
             "  const p = smallestFit(pOrder.length, (k) => { d.roads = rOrder.length + k; return fits(withPrim(k), tol); });\n" +
             "  if (p) { d.roads = rOrder.length + p.k; return { content: withPrim(p.k), toleranceMm: tol, dropped: d, sections: p.got }; }\n" +
             "  return null;\n"]],
    expect: ["MB4"],
  },
  "S-TERRCAP": {
    file: BUDGET, set: "all",
    edits: [["  for (const s of sections) if (s.type !== TYPE.TERRAIN) n += sectionSize(s);\n", "  for (const s of sections) n += sectionSize(s); // SABOTAGE S-TERRCAP: the TERRAIN body counted\n"]],
    expect: ["MB8a", "MB8b"],
  },
  "S-TNOMARGIN": {
    file: TERRAIN, set: "all",
    edits: [["  return [Math.floor(x0 / 256) - 1, Math.floor(y0 / 256) - 1, Math.floor(x1 / 256) + 1, Math.floor(y1 / 256) + 1];\n",
             "  return [Math.floor(x0 / 256), Math.floor(y0 / 256), Math.floor(x1 / 256), Math.floor(y1 / 256)]; // SABOTAGE S-TNOMARGIN\n"]],
    expect: ["MT3b", "MB8a", "MB8b"],
  },
  // A1.7's MQ sabotages (A2.28).
  "S-QROUND": {
    file: QUERY, set: "all",
    edits: [["  const c = quantise(lat, lon, r);\n",
             "  const he = (x: number) => { const v = x * 100, f = Math.floor(v), d = v - f; return (d > 0.5 ? f + 1 : d < 0.5 ? f : f % 2 === 0 ? f : f + 1) / 100; };\n" +
             "  const c = { ...quantise(lat, lon, r), lat: he(lat), lon: he(lon) }; // SABOTAGE S-QROUND: a local quantise, half to even\n"]],
    expect: ["MQ3"],
  },
  "S-QDIGEST": {
    file: QUERY, set: "all",
    edits: [["export const queryKey = (q: Query) => [q.lat2, q.lon2, q.rKm, q.epsMm, q.mask,", "export const queryKey = (q: Query) => [q.lat2, q.lon2, q.rKm, /* SABOTAGE S-QDIGEST: no epsMm */ q.mask,"]],
    expect: ["MQ10"],
  },
  "S-QSORT": {
    file: QUERY, set: "all",
    edits: [["    levels.sort((a, b) => a.radiusMm - b.radiusMm);\n", "    // SABOTAGE S-QSORT: the levels left as sent\n"]],
    expect: ["MQ8"],
  },
  // A1.7's MW sabotages, and the MP and MA1 controls with the exact sets registered in A2.31.
  "S-WNOVALIDATE": {
    file: HANDLER, set: "all",
    edits: [['  if (!/^[0-9a-f]{12}$/.test(storeId) || validate(bytes).status !== "kOk") return errorResponse(500, "builder_invalid");\n',
             '  if (!/^[0-9a-f]{12}$/.test(storeId)) return errorResponse(500, "builder_invalid"); void validate; // SABOTAGE S-WNOVALIDATE\n']],
    expect: ["MW15"],
  },
  "S-W304LATE": {
    file: HANDLER, set: "all",
    edits: [['    if (inm === etag) { log.bm = "304-known"; return new Response(null, { status: 304, headers: { etag, "cache-control": "no-cache" } }); }\n    const hit = await cache.match(cacheKey(etag));\n',
             '    const hit = await cache.match(cacheKey(etag)); // SABOTAGE S-W304LATE: no comparison before the builder\n']],
    // Re-registered (docs/basemap-branch-log.md), from A1.7's {MW10}: with no comparison, MK2's request is a hit and
    // 304-known is never logged. The anchor follows the labelled line. The original {MW10} stays on record there.
    expect: ["MK2", "MK6", "MW10"],
  },
  "S-WNOSTORECHECK": {
    file: BUILD, set: "all",
    edits: [['  if (hex(await crypto.subtle.digest("SHA-256", manifestBytes)).slice(0, 12) !== storeId) return unavailable("the manifest does not hash to current\'s store id");\n',
             '  void hex; // SABOTAGE S-WNOSTORECHECK: the store-id guard skipped\n']],
    expect: ["MW17"],
  },
  "S-PLOG": {
    file: HANDLER, set: "all",
    edits: [["  const q: Query = p.q;\n", "  const q: Query = p.q;\n  console.log(q.lat2); // SABOTAGE S-PLOG\n"]],
    // Re-registered before any run (docs/basemap-a1.md): MS8 also reads every line for the origin. {MP1} stays on record.
    expect: ["MP1", "MS8"],
  },
  "S-PWRITE": {
    file: WORKER, set: "all",
    // Re-anchored (docs/basemap-build-reads.md): the build line now hands r2Reader its stat. The set is unchanged.
    edits: [["    const r = await build(r2Reader(env.STORE, stat), q, { commit });\n", "    await env.STORE.put(\"sabotage\", \"x\"); // SABOTAGE S-PWRITE\n    const r = await build(r2Reader(env.STORE, stat), q, { commit });\n"]],
    expect: ["MP2"],
  },
  "S-PETAG": {
    file: QUERY, set: "all",
    edits: [["  return `\"bm1-${rev}-${storeId}-${await queryDigest(q)}\"`;\n", "  return `\"bm1-${rev}-${storeId}-${await queryDigest(q)}-${q.lat2}\"`; // SABOTAGE S-PETAG\n"]],
    expect: ["MP3", "MQ10", "MW9"],
  },
  "S-ANOTICE": {
    file: ATTRIBUTION, set: "all",
    edits: [["  - **SRTM's notice**, from SRTM's specification §3.15 b as joerd quotes it, with the year of production inserted: \"© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C.\"\n",
             "  - SABOTAGE S-ANOTICE: the notice deleted\n"]],
    // Re-registered by ruling (2026-10-30, docs/basemap-deploy.md), from {MA1}: MA2 reads the notice from this file by
    // design (both sides), so deleting it fails both rows. The original {MA1} and the miss stay on record.
    expect: ["MA1", "MA2"],
  },
  // MD4's own sabotage (A2.35): the processing text changed, the same length, with rev unchanged.
  "S-MREV": {
    file: "src/basemap/provenance.ts", set: "all",
    edits: [["4x4 mean of z9 pixels; ", "4x4 MEAN of z9 pixels; "]],
    expect: ["MA1", "MD4", "MG0b"],
  },
  // The D0 + D2 PR's four (docs/basemap-deploy.md): the builder's stamp check, the handler's commit check, and what
  // /healthz reports before and after a build.
  "S-WSTAMP": {
    file: WORKER, set: "all",
    edits: [['      if (!/^[0-9a-f]{40}$/.test(commit)) return json(503, { error: "builder_unstamped" }); // §2.4: never build unstamped\n',
             '      // SABOTAGE S-WSTAMP: the stamp not checked\n']],
    expect: ["MW18"],
  },
  "S-WNOCOMMIT": {
    file: HANDLER, set: "all",
    edits: [['  if (!/^[0-9a-f]{40}$/.test(commit)) return errorResponse(500, "builder_invalid");\n',
             '  // SABOTAGE S-WNOCOMMIT: the builder\'s commit not checked\n']],
    expect: ["MW19"],
  },
  "S-HMADEUP": {
    file: HANDLER, set: "all",
    edits: [["commit: lastBuild ? lastBuild.commit : null };", "commit: lastBuild ? lastBuild.commit : \"0\".repeat(40) }; // SABOTAGE S-HMADEUP"]],
    expect: ["MH2"],
  },
  // The D3 PR's (docs/basemap-deploy.md): the ETOPO1 credit dropped from the credits page.
  "S-ACREDIT": {
    file: "src/photolicense.ts", set: "all",
    edits: [["const TERRAIN_CREDITS: readonly string[] = CREDIT_BITS.map(([, text]) => text);",
             "const TERRAIN_CREDITS: readonly string[] = CREDIT_BITS.filter(([b]) => b !== 4).map(([, text]) => text); // SABOTAGE S-ACREDIT"]],
    expect: ["MA2"],
  },
  "S-HNOCACHE": {
    file: HANDLER, set: "all",
    edits: [["  return { rev: BASEMAP_REV, store: lastBuild ? lastBuild.storeId : null, commit: lastBuild ? lastBuild.commit : null };\n",
             "  return { rev: BASEMAP_REV, store: null, commit: null }; // SABOTAGE S-HNOCACHE\n"]],
    expect: ["MH3"],
  },
  // The MK rows' (docs/basemap-branch-log.md): each branch's label swapped for ANOTHER valid label, so MK6's set check
  // stays green and only the branch's own row (and MK6's "each label seen" control) can catch it; a label set before
  // the branch; and the label leaked into the dataset point.
  "S-MK304": {
    file: HANDLER, set: "all",
    edits: [['{ log.bm = "304-known"; return new Response(null, { status: 304,', '{ log.bm = "hit"; /* SABOTAGE S-MK304 */ return new Response(null, { status: 304,']],
    expect: ["MK2", "MK6"],
  },
  "S-MKHIT": {
    file: HANDLER, set: "all",
    edits: [['if (hit) { log.bm = "hit";', 'if (hit) { log.bm = "304-known"; /* SABOTAGE S-MKHIT */']],
    // Re-registered before any run (docs/basemap-a1.md): MS2's second request is a hit. {MK3, MK6} stays on record.
    expect: ["MK3", "MK6", "MS2"],
  },
  "S-MKKMISS": {
    file: HANDLER, set: "all",
    edits: [['    log.bm = "known-miss";\n', '    log.bm = "unknown-store"; // SABOTAGE S-MKKMISS\n']],
    // Re-registered before any run (docs/basemap-a1.md): MS3's 202 is labelled known-miss. {MK4, MK6} stays on record.
    expect: ["MK4", "MK6", "MS3"],
  },
  "S-MKUNK": {
    file: HANDLER, set: "all",
    edits: [['  } else log.bm = "unknown-store";\n', '  } else log.bm = "known-miss"; // SABOTAGE S-MKUNK\n']],
    expect: ["MK1", "MK6"],
  },
  "S-MKPRE": {
    file: HANDLER, set: "all",
    edits: [['  if (!env.BASEMAP) return errorResponse(503, "not_configured");\n', '  log.bm = "unknown-store"; // SABOTAGE S-MKPRE: labelled before any branch\n  if (!env.BASEMAP) return errorResponse(503, "not_configured");\n']],
    expect: ["MK5"],
  },
  "S-MKAE": {
    file: "src/metrics.ts", set: "all",
    edits: [['      blobs: [route, m.cache ?? "", m.upstream ?? "", m.model, m.dev ?? "", m.fw ?? ""],\n', '      blobs: [route, m.cache ?? "", m.upstream ?? "", m.model, m.dev ?? "", m.fw ?? "", ...(m.bm ? [m.bm] : [])], // SABOTAGE S-MKAE\n']],
    expect: ["MK7"],
  },
  // The BB rows' (docs/basemap-build-reads.md): the builder's read line. S-BB1's set holds BB2 too: the uncounted read
  // (`current`) is also one of no_coverage's three.
  "S-BB1": {
    file: WORKER, set: "all",
    edits: [["      stat.reads++;\n", "      if (key !== \"bm/v1/current\") stat.reads++; // SABOTAGE S-BB1: one read uncounted\n"]],
    expect: ["BB1", "BB2"],
  },
  "S-BB2": {
    file: WORKER, set: "all",
    edits: [["      console.log(JSON.stringify({ evt: \"bm_build\",", "      if (r.ok) console.log(JSON.stringify({ evt: \"bm_build\","]],
    expect: ["BB2"],
  },
  "S-BB3": {
    file: WORKER, set: "all",
    edits: [["...stat, wallMs: Date.now() - t0 }));", "...stat, wallMs: Date.now() - t0, lat0Micro: q.lat0Micro })); // SABOTAGE S-BB3"]],
    expect: ["BB3"],
  },
  // A1's (docs/basemap-a1.md): each plant as #551 froze it. S-MS1's and S-MS8's sets were corrected before any run:
  // KV's store id feeds every MS path but MS5 and MS9, and MS3 and MS6 read the package key back.
  "S-MS1": {
    file: HANDLER, set: "all",
    edits: [["  const id = mem ?? (pkg ? await storeFromKv(env) : null);\n", "  const id = mem; void storeFromKv; // SABOTAGE S-MS1: the KV read skipped\n"]],
    // Re-registered by ruling (~20:45Z), the miss kept: {MS1, MS2, MS3, MS4, MS6, MS7, MS8}. MS8's covered first request
    // teaches the isolate the store id, so its second request takes the known path without KV, and it stays green.
    expect: ["MS1", "MS2", "MS3", "MS4", "MS6", "MS7"],
  },
  "S-MS2": {
    file: HANDLER, set: "all",
    edits: [["      const kept = await pkg.get(pkgKey(id, digest));\n", "      const kept = null as R2ObjectBody | null; // SABOTAGE S-MS2: the stored package never read\n"]],
    expect: ["MS2", "MS6"],
  },
  "S-MS3": {
    file: HANDLER, set: "all",
    edits: [["      const quick = await Promise.race([work, sleep(basemapTiming.syncWaitMs).then(() => null)]);\n", "      const quick = await work; void sleep; // SABOTAGE S-MS3: always awaited, never raced\n"]],
    expect: ["MS3", "MS7"],
  },
  "S-MS4": {
    file: HANDLER, set: "all",
    edits: [["  if (Date.now() - since < basemapTiming.lockStaleMs) return false;\n", "  void since; return true; // SABOTAGE S-MS4: a held lock ignored\n"]],
    expect: ["MS4"],
  },
  "S-MS5": {
    file: HANDLER, set: "all",
    edits: [["  if (Date.now() - since < basemapTiming.lockStaleMs) return false;\n", "  if (since >= 0) return false; // SABOTAGE S-MS5: a stale lock treated as fresh\n"]],
    expect: ["MS5"],
  },
  "S-MS7": {
    file: HANDLER, set: "all",
    edits: [["      else if (id && (r.status === 404 || r.status === 422)) await pkg.put(", "      else if (id && false) await pkg.put( /* SABOTAGE S-MS7: errors never kept */"]],
    expect: ["MS7"],
  },
  "S-MS8": {
    file: HANDLER, set: "all",
    edits: [["      if (!(r instanceof Response)) await pkg.put(pkgKey(r.storeId, digest), r.bytes);\n", "      if (!(r instanceof Response)) await pkg.put(`${pkgKey(r.storeId, digest)}-${q.lat0Micro}`, r.bytes); // SABOTAGE S-MS8\n"]],
    expect: ["MS3", "MS6", "MS8"],
  },
  "S-MS9": {
    file: HANDLER, set: "all",
    edits: [["  const pkg = env.BASEMAP_PKG; // A1 only where bound (MS9)\n", "  const pkg = env.BASEMAP_PKG ?? ({} as R2Bucket); // SABOTAGE S-MS9: absence not checked\n"]],
    expect: ["MS9"],
  },
  "S-MX1": {
    file: BUILD, set: "all",
    edits: [["export const READ_AHEAD = 6;", "export const READ_AHEAD = 1; // SABOTAGE S-MX1: one at a time"]],
    expect: ["MX1"],
  },
  // Renamed by ruling from S-MI8 (~20:05Z): the row is MI9, since MI8 was already basemap-regions.test.ts's.
  "S-MI9": {
    file: LIB,
    edits: [["  for (const o of objects) await bucket.put(o.key, o.bytes);\n  let v = await verifyStore(bucket, objects);\n",
             "  for (const o of objects) await bucket.put(o.key, o.bytes);\n  if (kv) await kv.put(STORE_KV_KEY, \"000000000000\"); // SABOTAGE S-MI9: before the verify\n  let v = await verifyStore(bucket, objects);\n"]],
    expect: ["MI9"],
  },
  // The CONTROL on the missing-cases check below: a module that fails to load must grade BLIND, never "as predicted".
  "C-LOADFAIL": {
    file: LAYERS, set: "all",
    edits: [["export const isRiverName = ", "export const isRiverName = ((( // CONTROL C-LOADFAIL: a syntax error\n"]],
    expect: "BLIND",
  },
};

const sha = (b) => createHash("sha256").update(b).digest("hex");
// A1.7: MD3a and MD4 compare against the COMMITTED outputs, so they fire, correctly, on any change to builder output.
// They are ignored in a sabotage's grading unless that sabotage names them (A2.35).
const COMMITTED_OUTPUT = new Set(["MD3a", "MD4"]);
// A PK id may hold hyphens (A12.15's neg-terrain-count) and must end at the title's colon, so it is either whole or
// UNNAMED, never a shorter id that looks real (A2.46: the hyphen-less pattern graded MV/PK-terrain-count as MV/PK-terrain).
// BB<n> (docs/basemap-build-reads.md): the builder's read-line rows, named in #539's plan before this pattern knew them.
const caseOf = (title) => (/^(M[A-Z](?:\d+[a-z]?|-[a-z]+)|BB\d+|MV\/PK-[A-Za-z0-9-]+(?=:))/.exec(title) ?? [null, `UNNAMED(${title})`])[1];

// THE DUPLICATE-ID GUARD (ruled 2026-11-05 ~20:05Z, docs/basemap-a1.md). Grading is by case id, so two rows sharing an id
// grade as one -- A1's first run, where a new MI8 met basemap-regions.test.ts's MI8 and baseline[all] read 158 for 159.
// Before anything runs, every LITERAL test title under test/ is read; an id that names rows in two files, or two
// different "<ID>:" rows in one file, is refused (exit 2) with each duplicate printed. Sub-cases written "<ID> (a): ..."
// are one row. Titles built at run time (it.each, template expressions) are not seen: the baselines' case counts are what
// catch those, as they caught MI8.
function duplicateCaseIds(testDir = join(PROXY, "test")) {
  const byId = new Map();
  const TITLE = /\bit(?:\.each\([\s\S]*?\))?\(\s*(["'`])((?:M[A-Z](?:\d+[a-z]?|-[a-z]+)|BB\d+|MV\/PK-[A-Za-z0-9-]+)[^"'`]*)\1/g;
  for (const f of readdirSync(testDir).filter((n) => n.endsWith(".test.ts")).sort()) {
    for (const m of readFileSync(join(testDir, f), "utf8").matchAll(TITLE)) {
      const title = m[2], id = caseOf(title);
      if (id.startsWith("UNNAMED(")) continue;
      if (!byId.has(id)) byId.set(id, new Map());
      const files = byId.get(id);
      if (!files.has(f)) files.set(f, new Set());
      files.get(f).add(title);
    }
  }
  const dups = [];
  for (const [id, files] of byId) {
    if (files.size > 1) { dups.push(`${id}: in ${[...files.keys()].join(" and ")}`); continue; }
    const [[f, ts]] = [...files];
    const whole = [...ts].filter((t) => t.startsWith(`${id}:`));
    if (whole.length > 1) dups.push(`${id}: ${whole.length} rows in ${f}`);
  }
  return dups;
}
if (process.argv[2] === "--selftest-dups") {
  // The guard's sabotage (as ruled): a copy of a test file planted beside it must be refused, every duplicate naming the
  // copy; the copy is then removed and its absence checked. CONTROL first: the tree as it is holds no duplicate.
  const control = duplicateCaseIds();
  const copy = join(PROXY, "test", "basemap-zz-dupcopy.test.ts");
  writeFileSync(copy, readFileSync(join(PROXY, "test", "basemap-ingest.test.ts")));
  let planted;
  try { planted = duplicateCaseIds(); } finally { rmSync(copy, { force: true }); }
  const gone = !existsSync(copy);
  console.log(`dup selftest: control ${control.length} duplicate(s)${control.length ? `: ${control.join("; ")}` : ""}; planted copy -> ${planted.length} duplicate(s), all naming the copy: ${planted.length > 0 && planted.every((d) => d.includes("basemap-zz-dupcopy.test.ts"))}; copy removed: ${gone}`);
  process.exit(control.length === 0 && planted.length > 0 && planted.every((d) => d.includes("basemap-zz-dupcopy.test.ts")) && gone ? 0 : 1);
}
{
  const dups = duplicateCaseIds();
  if (dups.length) {
    console.log("RIG: duplicate case ids -- the driver grades by id, so each of these would grade as one row:");
    for (const d of dups) console.log(`  ${d}`);
    process.exit(2);
  }
}

// Every vitest run, and how many met a Workers runtime failure (ruled 2026-10-28, A2.12: "If the runtime failure
// recurs, report how often"). Printed at the end of every pass, zeros included. A run whose output carries the
// runtime's failure text is counted whether nothing ran or only some files did: both are instrument failures.
const tally = { runs: 0, runtimeAll: 0, runtimePartial: 0, maxTimeWait: 0, heldMs: 0 };
const RUNTIME_FAIL = /ERR_RUNTIME_FAILURE|The Workers runtime failed to start|Fallback service failed to fetch module/;

// THE PORT GATE (A2.14). Each vitest-pool-workers run opens hundreds of loopback connections for its module service,
// and each lingers in TIME_WAIT after it closes. Back-to-back runs filled Windows' 16,384 ephemeral ports (15,998
// counted right after a failing pass), and the runtime's connect was then refused. So a run starts only when the
// TIME_WAIT count is under TW_LIMIT -- polled, never slept for a fixed time -- and the count is printed per run, so a
// runtime failure at a LOW count would show that ports are not (or not only) the cause.
const TW_LIMIT = 8000;
function timeWaitCount() {
  if (process.platform !== "win32") return null;
  const r = spawnSync("powershell", ["-NoProfile", "-Command", "@(Get-NetTCPConnection -State TimeWait -ErrorAction SilentlyContinue).Count"], { encoding: "utf8" });
  const n = Number(r.stdout.trim());
  if (r.status !== 0 || !Number.isInteger(n)) throw new Error(`cannot read the TIME_WAIT count: ${r.stderr.slice(-300)}`);
  return n;
}
function holdForPorts() {
  const t0 = Date.now();
  let n = timeWaitCount();
  while (n !== null && n >= TW_LIMIT) {
    if (Date.now() - t0 > 600_000) throw new Error(`TIME_WAIT still ${n} after 10 minutes`);
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 5000);
    n = timeWaitCount();
  }
  tally.heldMs += Date.now() - t0;
  if (n !== null && n > tally.maxTimeWait) tally.maxTimeWait = n;
  return n;
}

function runTests(setName) {
  const set = SETS[setName];
  const tw = holdForPorts();
  const dir = mkdtempSync(join(tmpdir(), "bm-sab-"));
  const out = join(dir, "r.json");
  const args = ["vitest", "run", ...set.files, ...(set.filter ? ["-t", set.filter] : []), "--reporter=json", `--outputFile=${out}`];
  const r = spawnSync("npx", args, { cwd: PROXY, shell: true, encoding: "utf8" });
  tally.runs++;
  const runtime = RUNTIME_FAIL.test(`${r.stderr}\n${r.stdout}`);
  const at = tw === null ? "" : ` [TIME_WAIT ${tw} at start]`;
  let json;
  try { json = JSON.parse(readFileSync(out, "utf8")); } catch { json = null; }
  rmSync(dir, { recursive: true, force: true });
  const ran = json ? (json.testResults ?? []).length : 0;
  if (runtime) { if (ran) tally.runtimePartial++; else tally.runtimeAll++; }
  if (!json) return { blind: true, runtime, at, why: runtime ? "the Workers runtime failed" : (r.stderr || r.stdout || "").slice(-800) };
  if (runtime && !ran) return { blind: true, runtime, at, why: "the Workers runtime failed (0 test files ran)" };
  const failed = new Set(), seen = new Set();
  for (const f of json.testResults ?? []) for (const a of f.assertionResults ?? []) {
    if (a.status === "skipped" || a.status === "pending") continue; // outside the set's filter
    const id = caseOf(a.title); seen.add(id);
    if (a.status !== "passed") failed.add(id);
  }
  // A suite that failed to load has no assertion results: that is BLIND, never "nothing failed".
  if (seen.size === 0) return { blind: true, runtime, at, why: "no test results" };
  return { failed: [...failed].sort(), seen, files: ran, runtime, at };
}

function plant(name, src, edits) {
  let out = src;
  for (const [a, b] of edits) {
    const n = out.split(a).length - 1;
    if (n !== 1) throw new Error(`${name}: anchor found ${n} times, need exactly 1:\n${a}`);
    out = out.replace(a, b);
  }
  if (out === src) throw new Error(`${name}: the plant changed nothing`);
  return out;
}

const only = process.argv[2];
let ok = true;
const baselines = {};
/** Each set's clean baseline, once: with nothing planted, nothing in the set may fail. */
function baseline(setName) {
  if (baselines[setName]) return;
  const base = runTests(setName);
  if (base.blind || base.failed.length) { console.log(`BASELINE[${setName}] ${base.blind ? "BLIND: " + base.why : "FAILED: " + base.failed.join(",")}`); printTally(); process.exit(2); }
  const f = SETS[setName].filter;
  console.log(`baseline[${setName}${f ? `, -t ${f}` : ""}]: 0 failing cases (${base.seen.size} cases seen in ${base.files} files)${base.at}`);
  baselines[setName] = base.seen;
}
for (const [name, s] of Object.entries(SABOTAGES)) {
  if (only && only !== name) continue;
  const setName = s.set ?? "ingest";
  baseline(setName);
  const path = join(PROXY, s.file);
  const orig = readFileSync(path);
  const before = sha(orig);
  try {
    writeFileSync(path, plant(name, orig.toString("utf8"), s.edits));
    const r = runTests(setName);
    // Every case the baseline saw must be seen again: a file that failed to load drops its cases from `seen`
    // WITHOUT adding them to `failed`, and would otherwise grade as "nothing else failed".
    const missing = r.blind ? [] : [...baselines[setName]].filter((id) => !r.seen.has(id));
    const why = r.blind ? r.why : r.runtime ? `the Workers runtime failed partway (${missing.length} baseline cases not run)` : missing.length ? `${missing.length} baseline cases not run: ${missing.slice(0, 6).join(", ")}${missing.length > 6 ? ", ..." : ""}` : null;
    if (s.expect === "BLIND") {
      // A control's BLIND must come from its OWN plant: one the runtime also failed on cannot tell the two apart.
      if (r.runtime) { ok = false; console.log(`${name}: UNREADABLE, the runtime also failed (${why})${r.at}`); }
      else if (why) console.log(`${name}: AS PREDICTED, BLIND (${why})${r.at}`);
      else { ok = false; console.log(`${name}: OFF-PREDICTION, graded {${r.failed.join(", ")}} where BLIND was required${r.at}`); }
      continue;
    }
    const want = [...s.expect].sort();
    const setAside = r.failed.filter((id) => COMMITTED_OUTPUT.has(id) && !want.includes(id));
    r.failed = r.failed.filter((id) => !setAside.includes(id));
    if (setAside.length) r.at += ` [set aside, A1.7: ${setAside.join(", ")}]`;
    if (why) { ok = false; console.log(`${name}: BLIND (${why})${r.at}`); }
    else if (r.failed.join(",") === want.join(",")) console.log(`${name}: AS PREDICTED, failing {${r.failed.join(", ")}}${r.at}`);
    else { ok = false; console.log(`${name}: OFF-PREDICTION, failing {${r.failed.join(", ")}} against {${want.join(", ")}}${r.at}`); }
  } finally {
    writeFileSync(path, orig);
    if (sha(readFileSync(path)) !== before) { console.log(`${name}: RESTORE FAILED for ${s.file}`); process.exit(3); }
  }
}
printTally();
process.exit(ok ? 0 : 1);

function printTally() {
  console.log(`runs: ${tally.runs}; runtime failures: ${tally.runtimeAll} with nothing run, ${tally.runtimePartial} partway; ` +
    `highest TIME_WAIT at a run's start: ${tally.maxTimeWait} (gate ${TW_LIMIT}); held for ports: ${Math.round(tally.heldMs / 1000)} s`);
}
