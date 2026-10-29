// Grades MC1, MC2 and MC4 (docs/basemap-route.md A1.7, A2.38) on the SEALED store, read through the exact objects D1
// verified in R2. Centres: the 50 cleared airports (test/fixtures/wx-airports.txt), and EGLL and PANC, admitted by
// forbidden.py (test/fixtures/basemap-mc-centres.txt). PHNL and CYYZ (MC2) and MC3 wait: forbidden.py does not admit
// them (A2.38). Prints identifiers and verdicts, never a coordinate. Exit 0 as frozen, 1 off-prediction, 2 BLIND.
//
//   npx tsx scripts/basemap-grade-mc.ts --cache <ingest cache>
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { build } from "../src/basemap/build";
import { decode } from "../src/basemap/format";
import { CURRENT_KEY, covered, storeKeys } from "../src/basemap/store";
import { assembleStore } from "./basemap/d1";

const i = process.argv.indexOf("--cache"), cache = process.argv[i + 1];
if (i < 0 || !cache) throw new Error("--cache <dir>");
const cachePath = (c: string, url: string) => { const u = new URL(url); return join(c, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };
const { storeId, objects, current } = assembleStore(cache, cachePath);
const map = new Map(objects.map((o) => [o.key, o.bytes]));
map.set(CURRENT_KEY, current);
const store = { get: async (k: string) => map.get(k) ?? null };
const readIds = (p: string) => Object.fromEntries(readFileSync(p, "utf8").split(/\r?\n/).filter(Boolean).map((l) => { const [id, a, b] = l.trim().split(/\s+/); return [id!, [Math.round(Number(a) * 100), Math.round(Number(b) * 100)] as [number, number]]; }));
const cleared = readIds(join(process.cwd(), "test/fixtures/wx-airports.txt"));
const extra = readIds(join(process.cwd(), "test/fixtures/basemap-mc-centres.txt"));
const at = (id: string) => cleared[id] ?? extra[id];
let off = 0;
const verdict = (row: string, ok: boolean, what: string) => { if (!ok) off++; console.log(`${row}: ${ok ? "as frozen" : "OFF-PREDICTION"} -- ${what}`); };

// MC1: the 50 cleared airports are covered
const cov = map.get(storeKeys(storeId).coverage)!;
const ids = Object.keys(cleared), hit = ids.filter((id) => covered(cov, cleared[id]![0], cleared[id]![1]));
verdict("MC1", ids.length === 50 && hit.length === 50, `${hit.length} of ${ids.length} covered${hit.length < ids.length ? `; not: ${ids.filter((x) => !hit.includes(x)).join(", ")}` : ""}`);

// MC2: EGLL and (0.00, 0.00) answer no_coverage; PHNL and CYYZ wait (A2.38)
const LV = [{ radiusMm: 24_743_664, n: 246 }, { radiusMm: 49_487_328, n: 246 }, { radiusMm: 98_974_656, n: 246 }];
const req = (h: number, c: number) => ({ lat0Micro: h * 10_000, lon0Micro: c * 10_000, rKm: 97, epsMm: 100_584, mask: 63, levels: LV });
const commit = "1".repeat(40);
const e = at("EGLL")!, egll = await build(store, req(e[0], e[1]), { commit }), zero = await build(store, req(0, 0), { commit });
const nc = (r: Awaited<ReturnType<typeof build>>) => !r.ok && r.status === 404 && r.code === "no_coverage";
verdict("MC2 (EGLL, 0.00/0.00)", nc(egll) && nc(zero), `EGLL ${egll.ok ? "built" : egll.status}, (0, 0) ${zero.ok ? "built" : zero.status}; PHNL and CYYZ WAIT (forbidden.py)`);

// MC4: the credits with Weatherscope's three levels; each carries the levels processing line; none refused
const want: Record<string, string[]> = { KDEN: ["SRTM"], KBOS: ["SRTM", "ETOPO1"], KBUF: ["SRTM", "NRCAN"], KELP: ["SRTM"], PANC: ["SRTM", "GMTED2010", "ETOPO1"] };
const name = (line: string) => (/^credit\.terrain: (SRTM|GMTED2010|ETOPO1)/.exec(line)?.[1]) ?? (line.includes("Open Government Licence") ? "NRCAN" : "?");
for (const [id, credits] of Object.entries(want)) {
  const p = at(id);
  if (!p) { off++; console.log(`MC4 ${id}: BLIND -- no admitted position`); continue; }
  const r = await build(store, req(p[0], p[1]), { commit });
  if (!r.ok) { verdict(`MC4 ${id}`, false, `refused: ${r.status} ${r.code}`); continue; }
  const prov = new TextDecoder().decode((decode(r.bytes).sections.find((s) => s.type === 7) as { text: Uint8Array }).text).split("\n");
  const got = prov.filter((l) => l.startsWith("credit.terrain: ")).map(name);
  const levelsLine = prov.some((l) => l.startsWith("processing.terrain: Terrarium z9 elevations; hillshade"));
  verdict(`MC4 ${id}`, JSON.stringify(got) === JSON.stringify(credits) && levelsLine, `[${got.join(", ")}] against [${credits.join(", ")}]; levels processing line ${levelsLine ? "present" : "MISSING"}`);
}
console.log(`store ${storeId}; off-prediction: ${off}`);
process.exit(off ? 1 : 0);
