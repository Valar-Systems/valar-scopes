// Grades MS0 and §5.4's outcome classes (docs/basemap-route.md A2.30) on the SEALED store: the builder over the exact
// objects D1 verified in R2 (assembleStore), at §5.4's centres and requests. Prints identifiers, numbers and verdicts,
// never a coordinate. Exit 0 every cell as frozen, 1 any cell off-prediction, 2 a cell that could not be read (BLIND).
//
//   npx tsx scripts/basemap-grade-store.ts --cache <ingest cache>
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { build } from "../src/basemap/build";
import { decode } from "../src/basemap/format";
import { CURRENT_KEY } from "../src/basemap/store";
import { assembleStore } from "./basemap/d1";

const arg = (n: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : undefined; };
const cache = arg("--cache");
if (!cache) throw new Error("--cache <dir>");
const cachePath = (c: string, url: string) => { const u = new URL(url); return join(c, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)); };
const { storeId, objects, current } = assembleStore(cache, cachePath);
const map = new Map(objects.map((o) => [o.key, o.bytes]));
map.set(CURRENT_KEY, current);
const store = { get: async (k: string) => map.get(k) ?? null };
const commit = execFileSync("git", ["rev-parse", "HEAD"], { encoding: "utf8" }).trim();

// A2.30, transcribed: MS0's base bytes and the frozen class sets, per centre, for [r97+terrain, r97, r161, r222]
const MS0: Record<string, Array<number | null>> = {
  Bend: [null, 1988, 1988, 2125], KGJT: [2161, 1988, 1988, 1988], KDEN: [2161, 1988, 1988, 1988], KMSP: [2161, 1987, 1988, 1990],
  KMSY: [2298, 2124, 2124, 2125], KBUF: [2310, 2136, 2137, 2137], KRDU: [2298, 1987, 2127, 2127], KATL: [2161, 1988, 1990, 1990],
};
const CLS: Record<string, Array<string[] | null>> = {
  Bend: [null, ["F", "C"], ["C", "D"], ["D"]], KGJT: [["F"], ["F"], ["F", "C", "D"], ["C", "D"]], KDEN: [["D"], ["C", "D"], ["D"], ["D"]],
  KMSP: [["D", "X"], ["D", "X"], ["D", "X"], ["D", "X"]], KMSY: [["D"], ["D"], ["D"], ["D"]], KBUF: [["D"], ["C", "D"], ["D"], ["D"]],
  KRDU: [["D"], ["D"], ["D", "X"], ["D", "X"]], KATL: [["D"], ["D"], ["D", "X"], ["D", "X"]],
};
const W = join(process.cwd(), "..");
const bend = readFileSync(join(W, "test/fixtures/wx/basemap/bend.bmpk")), bdv = new DataView(bend.buffer, bend.byteOffset, bend.byteLength);
const centres: Array<[string, number, number]> = [["Bend", bdv.getInt32(8, true), bdv.getInt32(12, true)]];
const ap = Object.fromEntries(readFileSync(join(process.cwd(), "test/fixtures/wx-airports.txt"), "utf8").split(/\r?\n/).filter(Boolean).map((l) => { const [id, a, b] = l.trim().split(/\s+/); return [id!, [Number(a), Number(b)]]; }));
for (const id of ["KGJT", "KDEN", "KMSP", "KMSY", "KBUF", "KRDU", "KATL"]) { const [a, b] = ap[id] as number[]; centres.push([id, Math.round(a! * 100) * 10_000, Math.round(b! * 100) * 10_000]); }
const ALL = 31, TER = 63;
const reqs: Array<[string, number, number, number, Array<{ radiusMm: number; n: number }>]> = [
  ["r97+terrain", 97, 100_584, TER, [{ radiusMm: 24_743_664, n: 246 }, { radiusMm: 49_487_328, n: 246 }, { radiusMm: 98_974_656, n: 246 }]],
  ["r97", 97, 404_167, ALL, []], ["r161", 161, 670_833, ALL, []], ["r222", 222, 925_000, ALL, []],
];

let off = 0, blind = 0;
console.log(`store ${storeId}; builder ${commit.slice(0, 12)}`);
for (const [id, lat0Micro, lon0Micro] of centres) {
  for (let k = 0; k < reqs.length; k++) {
    const [name, rKm, epsMm, mask, levels] = reqs[k]!;
    const base = MS0[id]![k], want = CLS[id]![k];
    if (base === null || !want) continue; // Bend's Weatherscope cell: excluded (A2.29, A2.30)
    const t0 = Date.now();
    let r;
    try { r = await build(store, { lat0Micro, lon0Micro, rKm, epsMm, mask, levels }, { commit }); } catch (e) { blind++; console.log(`${id.padEnd(5)} ${name.padEnd(12)} BLIND: ${(e as Error).message.slice(0, 120)}`); continue; }
    const ms = Date.now() - t0;
    if (!r.ok) {
      const cls = r.status === 422 ? "X" : `${r.status}`;
      const okc = want.includes(cls);
      if (!okc) off++;
      console.log(`${id.padEnd(5)} ${name.padEnd(12)} ${cls} ${okc ? "as frozen" : `OFF-PREDICTION (frozen ${want.join("/")})`}; MS0 not graded (no package); ${ms} ms`);
      continue;
    }
    const p = decode(r.bytes);
    const prov = new TextDecoder().decode((p.sections.find((s) => s.type === 7) as { text: Uint8Array }).text).split("\n");
    const counts = prov.filter((l) => /^(dropped|collapsed)\./.test(l)).map((l) => l.slice(l.indexOf(": ") + 2));
    const provBytes = (p.sections.find((s) => s.type === 7) as { text: Uint8Array }).text.length;
    const expectProv = base + counts.reduce((a, c) => a + c.length - 1, 0);
    const terrainBody = p.sections.filter((s) => s.type === 6).reduce((a, s) => a + 8 + (s as { values: Uint8Array }).values.length, 0);
    const nonTerrain = r.bytes.length - terrainBody;
    const dropped = prov.filter((l) => l.startsWith("dropped.")).reduce((a, l) => a + Number(l.slice(l.indexOf(": ") + 2)), 0);
    const cls = p.toleranceMm !== epsMm || dropped > 0 ? "D" : nonTerrain <= 20_480 ? "F" : "C";
    const okc = want.includes(cls), okp = provBytes === expectProv && counts.length === 14;
    if (!okc || !okp) off++;
    console.log(`${id.padEnd(5)} ${name.padEnd(12)} ${cls} ${okc ? "as frozen" : `OFF-PREDICTION (frozen ${want.join("/")})`}; non-terrain ${nonTerrain} B; ` +
      `PROVENANCE ${provBytes} B vs MS0 ${expectProv} (${counts.length} count lines) ${okp ? "exact" : "OFF-PREDICTION"}; dropped ${dropped}; ${ms} ms; sha ${createHash("sha256").update(r.bytes).digest("hex").slice(0, 12)}`);
  }
}
console.log(`cells off-prediction: ${off}; BLIND: ${blind}`);
process.exit(blind ? 2 : off ? 1 : 0);
