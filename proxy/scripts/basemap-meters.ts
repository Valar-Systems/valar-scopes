// MM1 and MM3 (docs/basemap-route.md §6): cold builds on the sealed store, in local Node, with the WS8 meters --
// process.cpuUsage for CPU, and the live peak as the largest forced-GC heap + array buffers sampled while it runs.
// Prints numbers and verdicts, never a coordinate.
//
//   node --expose-gc --import tsx scripts/basemap-meters.ts --cache <ingest cache>
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { build } from "../src/basemap/build";
import { CURRENT_KEY } from "../src/basemap/store";
import { assembleStore } from "./basemap/d1";

const gc = (globalThis as { gc?: () => void }).gc;
if (!gc) throw new Error("run with node --expose-gc (the live peak needs a forced GC)");
const i = process.argv.indexOf("--cache"), cache = process.argv[i + 1];
if (i < 0 || !cache) throw new Error("--cache <dir>");
const cachePath = (c: string, url: string) => { const u = new URL(url); return join(c, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };
const { objects, current } = assembleStore(cache, cachePath);
const map = new Map(objects.map((o) => [o.key, o.bytes]));
map.set(CURRENT_KEY, current);
const store = { get: async (k: string) => map.get(k) ?? null };
const COMMIT = "1".repeat(40);

/** One cold build: CPU (user + system) and the live peak above the pre-build baseline. */
async function meter(q: Parameters<typeof build>[1]) {
  gc!();
  const live = () => { const m = process.memoryUsage(); return m.heapUsed + m.arrayBuffers; };
  const base = live();
  let peak = 0;
  const t = setInterval(() => { gc!(); peak = Math.max(peak, live() - base); }, 50);
  const c0 = process.cpuUsage(), w0 = Date.now();
  const r = await build(store, q, { commit: COMMIT });
  const c = process.cpuUsage(c0), wall = Date.now() - w0;
  clearInterval(t);
  peak = Math.max(peak, live() - base);
  return { ok: r.ok, cpuMs: Math.round((c.user + c.system) / 1000), wallMs: wall, peakMB: Math.round(peak / 1e5) / 10 };
}

const bend = readFileSync(join(process.cwd(), "..", "test/fixtures/wx/basemap/bend.bmpk")), dv = new DataView(bend.buffer, bend.byteOffset, bend.byteLength);
const LV = [{ radiusMm: 24_743_664, n: 246 }, { radiusMm: 49_487_328, n: 246 }, { radiusMm: 98_974_656, n: 246 }];
const mm1 = [];
for (let k = 0; k < 3; k++) mm1.push(await meter({ lat0Micro: dv.getInt32(8, true), lon0Micro: dv.getInt32(12, true), rKm: 97, epsMm: 100_584, mask: 63, levels: LV }));
const worst = mm1.reduce((a, b) => ({ cpuMs: Math.max(a.cpuMs, b.cpuMs), peakMB: Math.max(a.peakMB, b.peakMB) }), { cpuMs: 0, peakMB: 0 });
console.log(`MM1 Bend, 3 levels, 3 runs: CPU ${mm1.map((m) => m.cpuMs).join(", ")} ms; live peak ${mm1.map((m) => m.peakMB).join(", ")} MB -> ` +
  `largest CPU ${worst.cpuMs} ms (<= 1,500: ${worst.cpuMs <= 1500 ? "as frozen" : "OFF-PREDICTION"}), live peak ${worst.peakMB} MB (<= 96: ${worst.peakMB <= 96 ? "as frozen" : "OFF-PREDICTION"})`);
const ap = Object.fromEntries(readFileSync(join(process.cwd(), "test/fixtures/wx-airports.txt"), "utf8").split(/\r?\n/).filter(Boolean).map((l) => { const [id, a, b] = l.trim().split(/\s+/); return [id!, [Number(a), Number(b)]]; }));
const [la, lo] = ap["KATL"] as number[];
const mm3 = await meter({ lat0Micro: Math.round(la! * 100) * 10_000, lon0Micro: Math.round(lo! * 100) * 10_000, rKm: 222, epsMm: 925_000, mask: 31, levels: [] });
console.log(`MM3 KATL r 222, no terrain: CPU ${mm3.cpuMs} ms, wall ${mm3.wallMs} ms, live peak ${mm3.peakMB} MB -> cpu_ms = min(10 x CPU, 30,000) = ${Math.min(10 * mm3.cpuMs, 30_000)}`);
