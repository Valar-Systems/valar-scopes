// SCRATCH, read-only: SEA-FIG's build child (docs/basemap-dense-code.md, "SEA-FIG's grading fixed before it runs", 313add48). ONE build
// by the CODE PR's builder at one home over a local store directory, at the client's query, commit 40 zeros (run 10's convention), as
// cp-run.mts builds it. Prints ONE line, "RESULT " + JSON, counts and ids only: sha (12), rung, radius, D2's radii, non-terrain bytes,
// PROVENANCE's dropped.water, the water section's ring count, the sea's SeaCounts as the code reports them, every piece's
// (file, record, fate) in order, and ringIdx. No env knob is read: the code PR's builder has none.
//   (from wt-main/proxy) node --import tsx ../../sf-run.mts <store dir, relative to cwd> <HOME>
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { build } from "./wt-code/proxy/src/basemap/build.ts";
import { decode } from "./wt-code/proxy/src/basemap/format.ts";
import { nonTerrainBytes } from "./wt-code/proxy/src/basemap/budget.ts";
import { parseQuery } from "./wt-code/proxy/src/basemap/query.ts";
const [dir, home] = process.argv.slice(2);
const HOMES: Record<string, [string, string]> = {
  KBOS: ["42.36", "-71.01"], KJFK: ["40.64", "-73.78"], KLGA: ["40.78", "-73.87"], KEWR: ["40.69", "-74.17"], MIDTOWN: ["40.75", "-73.99"], KMIA: ["25.79", "-80.29"],
  KSFO: ["37.62", "-122.38"], KSEA: ["47.45", "-122.31"], KORD: ["41.98", "-87.90"], KDTW: ["42.21", "-83.35"], KMSP: ["44.88", "-93.22"], KIND: ["39.72", "-86.29"],
  KJAC: ["43.61", "-110.74"], BUFFALO: ["42.886", "-78.879"], ROUSES: ["44.994", "-73.365"], ZAPATA: ["26.91", "-99.27"],
};
if (!dir || !HOMES[home!]) { console.log(`RESULT ${JSON.stringify({ error: "usage" })}`); process.exit(2); }
const store = { async get(key: string) { const rel = key.replace(/^bm\/v1\/(?:[0-9a-f]{12}\/)?/, ""); try { return new Uint8Array(readFileSync(join(dir, rel))); } catch { return null; } } };
const [lat, lon] = HOMES[home!]!;
const p = parseQuery(new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242`));
if (!p.ok) { console.log(`RESULT ${JSON.stringify({ error: p.code })}`); process.exit(2); }
const r = await build(store, { lat0Micro: p.q.lat0Micro, lon0Micro: p.q.lon0Micro, rKm: p.q.rKm, epsMm: p.q.epsMm, mask: p.q.mask, levels: p.q.levels }, { commit: "0".repeat(40) });
if (!r.ok) { console.log(`RESULT ${JSON.stringify({ home, ok: false, status: r.status, code: r.code })}`); process.exit(1); }
const pkg = decode(r.bytes) as any, s = r.report.sea;
const provS = pkg.sections.find((x: any) => x.text)?.text, provT: string = provS ? (typeof provS === "string" ? provS : new TextDecoder().decode(provS)) : "";
const prov: Record<string, number> = Object.fromEntries(provT.split("\n").filter((l) => /^(dropped|collapsed)\./.test(l)).map((l) => [l.split(": ")[0], Number(l.split(": ")[1])]));
const waterRings = (pkg.sections.find((x: any) => x.type === 1)?.rings ?? []).length;
console.log(`RESULT ${JSON.stringify({ home, ok: true, sha: createHash("sha256").update(r.bytes).digest("hex").slice(0, 12), rung: r.ladder.rung, radiusMm: pkg.radiusMm,
  radii: r.report.radii, ntb: nonTerrainBytes(pkg.sections), droppedWater: prov["dropped.water"] ?? null, provKeys: Object.keys(prov).length, waterRings,
  sea: s ? { drawn: s.drawn, refused: s.refused, collapsed: s.collapsed, outside: s.outside, dropped: s.dropped, islandsDropped: s.islandsDropped, islandsSmall: s.islandsSmall,
    bridges: s.bridges, bytes: s.bytes, refused5: s.refused5, repairs: s.repairs, repairVertices: s.repairVertices, repairLog: s.repairLog,
    ringIdx: s.ringIdx, fates: s.fates.map((x) => [x.file, x.record, x.fate]) } : null })}`);
