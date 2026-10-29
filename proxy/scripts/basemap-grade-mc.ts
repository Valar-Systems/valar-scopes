// Grades MC1-MC4 (docs/basemap-route.md A1.7, A2.38, A2.40) on the SEALED store, read through the exact objects D1
// verified in R2. Centres: the 50 cleared airports (test/fixtures/wx-airports.txt), and the centres admitted by
// forbidden.py with the two fixtures excluded by path and sha256 (A2.40; test/fixtures/basemap-mc-centres.txt: EGLL, PANC,
// CYYZ, PHTO in place of PHNL). Prints identifiers and verdicts, never a coordinate. Exit 0 as frozen, 1 off, 2 BLIND.
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
// S-BBOX's grading only: a coverage.bin built by planted code, in place of the sealed one (the store's files untouched)
const ci = process.argv.indexOf("--coverage");
if (ci > 0) map.set(storeKeys(storeId).coverage, new Uint8Array(readFileSync(process.argv[ci + 1]!)));
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

// MC2 (A2.40: PHTO in place of PHNL): EGLL, (0.00, 0.00) and CYYZ answer no_coverage; PHTO is covered
const LV = [{ radiusMm: 24_743_664, n: 246 }, { radiusMm: 49_487_328, n: 246 }, { radiusMm: 98_974_656, n: 246 }];
const req = (h: number, c: number) => ({ lat0Micro: h * 10_000, lon0Micro: c * 10_000, rKm: 97, epsMm: 100_584, mask: 63, levels: LV });
const commit = "1".repeat(40);
const nc = (r: Awaited<ReturnType<typeof build>>) => !r.ok && r.status === 404 && r.code === "no_coverage";
const say = (r: Awaited<ReturnType<typeof build>>) => (r.ok ? "built" : `${r.status}`);
const [e, y, t] = [at("EGLL")!, at("CYYZ")!, at("PHTO")!];
const egll = await build(store, req(e[0], e[1]), { commit }), zero = await build(store, req(0, 0), { commit });
const cyyz = await build(store, req(y[0], y[1]), { commit }), phto = await build(store, req(t[0], t[1]), { commit });
verdict("MC2", nc(egll) && nc(zero) && nc(cyyz) && phto.ok, `EGLL ${say(egll)}, (0, 0) ${say(zero)}, CYYZ ${say(cyyz)}, PHTO ${say(phto)}`);

// MC3: KSFO's cleared point moved 0.03 degrees east, into the bay, is covered (A2.40: admitted by forbidden.py)
const k = cleared["KSFO"]!;
verdict("MC3", covered(cov, k[0], k[1] + 3), `KSFO + 0.03 east ${covered(cov, k[0], k[1] + 3) ? "covered" : "NOT covered"}`);

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
