// SCRATCH (map-server): S4-2 BEFORE's package, ONE per centre (seam25.pred; the orchestrator's per-centre method, 2026-11-16).
// main's builder (wt-s4b at b282699ac091, its proxy/src/basemap) over the served store 4c0875738f38: the objects fetched for
// this centre (../../bm-obj-s4) first, then the local partial copy (../../bm-obj). The device's request at a configured 100 mi
// (MapQuery.h: r=162, eps=201168, layers water,rivers,roads,places,terrain, levels 41239440:246, 81808320:244, 162275520:242).
// Stamped with main's commit. Writes ../../s4-pkg/BEFORE-<centre>.bmpk. Prints ONE line, "RESULT " + JSON, counts only.
//   (from wt-s4b/proxy) node --import tsx ../../mirror/s4b_pkg.mts <centre>
import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { build } from "../wt-s4b/proxy/src/basemap/build.ts";
import { decode, validate } from "../wt-s4b/proxy/src/basemap/format.ts";
import { parseQuery } from "../wt-s4b/proxy/src/basemap/query.ts";

const MAIN = "b282699ac0911b825b04bdd84a84710000bea6fe";
const CENTRES: Record<string, [string, string]> = {
  "38021": ["45.94", "-98.17"], "39167": ["39.59", "-81.68"], "44007": ["41.76", "-71.38"],
  "56029": ["44.74", "-108.59"], "17005": ["38.74", "-89.50"], "19123": ["41.27", "-92.41"],
};
const PARAMS = "r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242";
const g = process.argv[2] ?? "";
const fail = (why: string) => { console.log(`RESULT ${JSON.stringify({ centre: g, error: why })}`); process.exit(2); };
if (!CENTRES[g]) fail("unknown centre");
const DIRS = ["../../bm-obj-s4", "../../bm-obj"];
let missing = 0, fromFetched = 0, fromLocal = 0;
const missingKinds: Record<string, number> = {};
const store = { async get(key: string) {
  const rel = key.replace(/^bm\/v1\/(?:[0-9a-f]{12}\/)?/, "");
  for (const [i, d] of DIRS.entries()) {
    try { const b = new Uint8Array(readFileSync(join(d, rel))); if (i === 0) fromFetched++; else fromLocal++; return b; } catch { /* the next source */ }
  }
  missing++; const k = rel.split("/")[0]!; missingKinds[k] = (missingKinds[k] ?? 0) + 1;
  return null;
} };
const [lat, lon] = CENTRES[g]!;
const p = parseQuery(new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&${PARAMS}`));
if (!p.ok) fail(p.code);
const r = await build(store, { lat0Micro: p.q.lat0Micro, lon0Micro: p.q.lon0Micro, rKm: p.q.rKm, epsMm: p.q.epsMm, mask: p.q.mask, levels: p.q.levels }, { commit: MAIN });
const out: Record<string, unknown> = { centre: g, ok: r.ok, status: r.ok ? 200 : r.status, code: r.ok ? null : r.code, why: r.ok ? null : ((r as { why?: string }).why ?? null),
  missing, missingKinds, fromFetched, fromLocal };
if (r.ok) {
  const pkg = decode(r.bytes) as { radiusMm: number; lat0Micro: number; lon0Micro: number };
  mkdirSync("../../s4-pkg", { recursive: true });
  writeFileSync(`../../s4-pkg/BEFORE-${g}.bmpk`, r.bytes);
  Object.assign(out, { storeId: r.storeId, rung: r.ladder.rung, bytes: r.bytes.length, validate: validate(r.bytes).status, radiusMm: pkg.radiusMm,
    sha: createHash("sha256").update(r.bytes).digest("hex").slice(0, 12) });
}
console.log(`RESULT ${JSON.stringify(out)}`);
process.exit(r.ok && missing === 0 ? 0 : 1);
