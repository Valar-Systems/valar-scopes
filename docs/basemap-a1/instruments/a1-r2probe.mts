// SCRATCH, read-only: SR1/SR4's object reads (docs/basemap-a1.md §8 4c, registered at f7bf74af). For each airport, the
// package and the lock key are computed with the handler's OWN parseQuery / queryDigest / pkgKey / lockKey (imported,
// never re-implemented) and read with `wrangler r2 object get <bucket>/<key> --remote --pipe`.
//   present = exit 0 with bytes; absent = exit 1 with wrangler's exact "The specified key does not exist."; else BLIND.
// The package read is the anchor: a lock reads "absent" only where the package read "present" in the same pass.
// Prints one boolean pair per airport. Never a key, a digest or a coordinate.
//   (from wt-main/proxy) npx tsx ../../a1-r2probe.mts <store12> KSEA [KSFO ...]
import { spawnSync } from "node:child_process";
import { parseQuery, queryDigest } from "./wt-main/proxy/src/basemap/query.ts";
import { lockKey, pkgKey } from "./wt-main/proxy/src/basemap/handler.ts";

const BUCKET = "blipscope-basemap-pkg-staging";
const AT: Record<string, [string, string]> = {
  KSEA: ["47.45", "-122.31"], KSFO: ["37.62", "-122.38"], KLAX: ["33.94", "-118.41"], KORD: ["41.98", "-87.90"],
  KJFK: ["40.64", "-73.78"], KATL: ["33.64", "-84.43"], KDFW: ["32.90", "-97.04"], KMIA: ["25.79", "-80.29"],
  KBOS: ["42.36", "-71.01"], KMSP: ["44.88", "-93.22"],
  // SYNC_WAIT's fresh ten (Q-SW2)
  KPHX: ["33.43", "-112.01"], KLAS: ["36.08", "-115.15"], KSLC: ["40.79", "-111.98"], KPDX: ["45.59", "-122.60"],
  KSAN: ["32.73", "-117.19"], KIAH: ["29.98", "-95.34"], KMCO: ["28.43", "-81.31"], KCLT: ["35.21", "-80.94"],
  KDTW: ["42.21", "-83.35"], KSTL: ["38.75", "-90.37"],
};
const [store, ...aps] = process.argv.slice(2);
if (!/^[0-9a-f]{12}$/.test(store ?? "") || aps.length === 0 || aps.some((a) => !AT[a])) { console.log("usage: <store12> <airport>..."); process.exit(2); }

function read(key: string): "present" | "absent" | "BLIND" {
  const r = spawnSync(`npx wrangler r2 object get ${BUCKET}/${key} --remote --pipe`, { shell: true, maxBuffer: 64 * 1024 * 1024 });
  const err = (r.stderr ?? Buffer.alloc(0)).toString("utf8");
  if (r.status === 0 && (r.stdout?.length ?? 0) > 0) return "present";
  if (r.status === 1 && err.includes("The specified key does not exist.")) return "absent";
  console.log(`   BLIND read: exit ${r.status}, ${r.stdout?.length ?? 0} bytes; stderr: ${err.replace(/[0-9a-f]{12,}/g, "<hex>").replace(/\s+/g, " ").slice(-240)}`);
  return "BLIND";
}

let blind = 0;
for (const ap of aps) {
  const [lat, lon] = AT[ap]!;
  const u = new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&r=97&eps=100584&layers=water,rivers,roads,peaks,places,terrain&levels=24743664:246,49487328:246,98974656:246`);
  const p = parseQuery(u);
  if (!p.ok) { console.log(`${ap}: the query does not parse (${p.code})`); process.exit(2); }
  const digest = await queryDigest(p.q);
  const pkg = read(pkgKey(store!, digest));
  const lock = pkg === "present" ? read(lockKey(store!, digest)) : "not read (no anchor)";
  if (pkg === "BLIND" || lock === "BLIND") blind++;
  console.log(`${ap}: package ${pkg}; lock ${lock}`);
}
process.exit(blind ? 2 : 0);
