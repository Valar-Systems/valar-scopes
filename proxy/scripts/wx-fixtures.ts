// WS7 (docs/wx-server.md): crops made by the SERVER PATH -- handleWx over a synthetic WXG1 in an in-memory
// R2 -- for the device's decoder to grade (test/host/test_wx_server_crops.cpp, in test/host/run.sh).
//
//   npx tsx scripts/wx-fixtures.ts           write test/fixtures/wx/server/
//   npx tsx scripts/wx-fixtures.ts --check   regenerate in memory; exit 1 if any committed byte differs
//
// --check runs in workers.yml, so a change to the server path cannot leave the device's test grading
// yesterday's crops. The expected cells in the manifest are the ENCODER's own cells (what it was asked to
// encode, from the flat grid), not a decode of the bytes, so the device decoder is graded against an
// independent answer. Every centre is a public airport cleared by the spike's forbidden.py.

import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { cropCellBox, encodeBox, quantise } from "../src/wx/crop";
import { FrameBuilder, type GridGeom, frameMeta } from "../src/wx/grid";
import { handleWx, type LatestRecord } from "../src/wx/handler";
import { PRODUCTS, frameKey, latestKey } from "../src/wx/products";
import type { Env } from "../src/types";
import { CONUS, flatCellAt, synthCodes } from "../test/wx-synth";

const OUT = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "test", "fixtures", "wx", "server");
const EPOCH = 1761000000;                      // fixed, so the fixtures are reproducible
const cref = PRODUCTS[0]!;
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

/** Just enough R2 for handleWx: get (whole or ranged) and put. */
class MemR2 {
  private m = new Map<string, Uint8Array>();
  async put(k: string, v: Uint8Array | string) { this.m.set(k, typeof v === "string" ? new TextEncoder().encode(v) : v); return {}; }
  async get(k: string, o?: { range?: { offset: number; length: number } }) {
    const v = this.m.get(k);
    if (!v) return null;
    const b = o?.range ? v.subarray(o.range.offset, o.range.offset + o.range.length) : v;
    return { text: async () => new TextDecoder().decode(b), arrayBuffer: async () => b.slice().buffer };
  }
}

async function frame(g: GridGeom, codes: Uint8Array) {
  const r2 = new MemR2();
  const m = frameMeta("cref", EPOCH, g);
  const fb = new FrameBuilder(m);
  for (let r = 0; r < g.nj; r++) fb.setRow(r, codes.subarray(r * g.ni, (r + 1) * g.ni));
  await r2.put(frameKey(cref, EPOCH), fb.bytes);
  const rec: LatestRecord = { product: "cref", epoch: EPOCH, key: frameKey(cref, EPOCH), ingestedAt: 0, ...g, tile: m.tile };
  await r2.put(latestKey(cref), JSON.stringify(rec));
  return { env: { WX: r2 } as unknown as Env, g, cellAt: flatCellAt(g, codes) };
}

const SMALL: GridGeom = { ni: 600, nj: 400, la1: 40.0, lo1: -100.0, di: 0.01, dj: 0.01 };   // WS6's grid
function smallCodes(): Uint8Array {
  const c = new Uint8Array(SMALL.ni * SMALL.nj);
  for (let r = 0; r < SMALL.nj; r++) for (let k = 0; k < SMALL.ni; k++) c[r * SMALL.ni + k] = (r * 7 + k * 3) % 14;
  return c;
}

async function main() {
  const check = process.argv.includes("--check");
  const realNow = Date.now;
  Date.now = () => (EPOCH + 60) * 1000;        // handleWx judges freshness by the clock; pin it
  const conus = await frame(CONUS, synthCodes());
  const small = await frame(SMALL, smallCodes());
  const airports = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "test", "fixtures", "wx-airports.txt"), "utf8")
    .trim().split(/\r?\n/).map((l) => { const [id, lat, lon] = l.trim().split(/\s+/); return { id: id!, lat: Number(lat), lon: Number(lon) }; });
  const at = (id: string) => { const a = airports.find((x) => x.id === id); if (!a) throw new Error(`${id} is not in the cleared list`); return a; };

  // The coarsened case: the first cleared airport whose 300 km crop needs coarsening, found by search so
  // the fixture set does not depend on knowing the synthetic grid by hand.
  let coarse: string | null = null;
  for (const a of airports) {
    const e = encodeBox(CONUS, EPOCH, cropCellBox(CONUS, quantise(a.lat, a.lon, 300)), conus.cellAt);
    if (e.flagged) { coarse = a.id; break; }
  }
  if (!coarse) throw new Error("no cleared airport has a coarsened 300 km crop on the synthetic grid -- the WS7 set cannot be built");

  const cases = [
    { name: "kden-80km", f: conus, a: at("KDEN"), r: 80, kind: "plain" },
    { name: `${coarse.toLowerCase()}-300km-coarsened`, f: conus, a: at(coarse), r: 300, kind: "coarsened" },
    { name: "kmci-80km-partly-outside", f: small, a: at("KMCI"), r: 80, kind: "partly outside the grid" },
    { name: "phnl-80km-all-14", f: conus, a: { id: "PHNL", lat: 21.32, lon: -157.92 }, r: 80, kind: "all 14 (off the grid)" },
    { name: "ksfo-25km", f: conus, a: at("KSFO"), r: 25, kind: "airport set" },
    { name: "kord-160km", f: conus, a: at("KORD"), r: 160, kind: "airport set" },
    { name: "kbos-222km", f: conus, a: at("KBOS"), r: 222, kind: "airport set" },
    { name: "kmsy-300km", f: conus, a: at("KMSY"), r: 300, kind: "airport set" },
  ];
  const crops = [];
  const files: Record<string, Uint8Array> = {};
  for (const c of cases) {
    const url = `https://proxy.test/api/v1/blipscope/wx/cref?lat=${c.a.lat.toFixed(2)}&lon=${c.a.lon.toFixed(2)}&r=${c.r}`;
    const res = await handleWx(new Request(url), c.f.env, "cref");
    if (res.status !== 200) throw new Error(`${c.name}: handleWx answered ${res.status}`);
    const bytes = new Uint8Array(await res.arrayBuffer());
    const enc = encodeBox(c.f.g, EPOCH, cropCellBox(c.f.g, quantise(c.a.lat, c.a.lon, c.r)), c.f.cellAt);
    if (sha(bytes) !== sha(enc.bytes)) throw new Error(`${c.name}: the handler's bytes differ from the encoder's`);
    files[`${c.name}.wxc`] = bytes;
    crops.push({ name: `${c.name}.wxc`, kind: c.kind, airport: c.a.id, rKm: c.r, frame: c.f === small ? "small-synth 600x400" : "conus-synth",
      bytes: bytes.length, sha256: sha(bytes), coarsen: enc.coarsen, flagged: enc.flagged, rows: enc.rows, cols: enc.cols,
      cellsSha256: sha(enc.cells) });
  }
  Date.now = realNow;
  const manifest = JSON.stringify({
    format: "WXC1 v1, made by the server path (proxy/src/wx/handler.ts) over a synthetic WXG1 in an in-memory R2",
    generator: "proxy/scripts/wx-fixtures.ts (regenerate; workers.yml runs --check)",
    expected: "cellsSha256 = sha256 of the ENCODER's own cells (row-major codes), not of a decode",
    epoch: EPOCH,
    crops,
  }, null, 1) + "\n";
  files["MANIFEST.json"] = new TextEncoder().encode(manifest);

  if (check) {
    const bad = Object.entries(files).filter(([n, b]) => !existsSync(join(OUT, n)) || sha(readFileSync(join(OUT, n))) !== sha(b)).map(([n]) => n);
    if (bad.length) { console.log(`STALE: test/fixtures/wx/server/ differs from the server path in ${bad.join(", ")} -- run npx tsx scripts/wx-fixtures.ts`); process.exit(1); }
    console.log(`wx server fixtures current: ${crops.length} crops + MANIFEST.json`);
    return;
  }
  mkdirSync(OUT, { recursive: true });
  for (const [n, b] of Object.entries(files)) writeFileSync(join(OUT, n), b);
  for (const c of crops) console.log(`${c.name.padEnd(34)} ${String(c.bytes).padStart(6)} B  k=${c.coarsen} ${c.flagged ? "flagged" : "       "} ${c.rows}x${c.cols}  ${c.kind}`);
}

main().catch((e) => { console.log(`FAIL: ${e instanceof Error ? e.message : String(e)}`); process.exit(2); });
