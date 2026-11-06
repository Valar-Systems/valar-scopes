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
//
// K12 (docs/wx-opuk.md) adds a second set in test/fixtures/wx/opuk/: eight crops over a REAL frame -- the wet K1
// fixture (proxy/test/fixtures/wx/opera/) through the COG reader and the resampling -- served by /wx/cref's
// dispatch at eight UK airports, r = 161 km. --check covers both sets, so a change to the reader or the
// resampling cannot leave the device grading yesterday's crops.

import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { cropCellBox, encodeBox, quantise } from "../src/wx/crop";
import { FrameBuilder, type GridGeom, frameMeta } from "../src/wx/grid";
import { handleWx, type LatestRecord } from "../src/wx/handler";
import { type RangeFetch, readWindow } from "../src/wx/opera";
import { OPUK_GRID, PRODUCTS, frameKey, latestKey, productById } from "../src/wx/products";
import { mapFor, resample } from "../src/wx/resample";
import type { Env } from "../src/types";
import { CONUS, flatCellAt, synthCodes } from "../test/wx-synth";

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, "..", "..", "test", "fixtures", "wx", "server");
const OUT_OPUK = join(HERE, "..", "..", "test", "fixtures", "wx", "opuk");
const OPUK_FRAME = "20261028T0255";           // the wet K1 fixture: echo runs in every crop, not only 0s and 14s
const OPUK_AIRPORTS = ["LHR", "MAN", "EDI", "GLA", "BFS", "ABZ", "CWL", "NQY"];
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

  const o = await opukSet();

  if (check) {
    let stale = false;
    for (const [dir, label, fs] of [[OUT, "server", files], [OUT_OPUK, "opuk", o.files]] as const) {
      const bad = Object.entries(fs).filter(([n, b]) => !existsSync(join(dir, n)) || sha(readFileSync(join(dir, n))) !== sha(b)).map(([n]) => n);
      if (bad.length) { console.log(`STALE: test/fixtures/wx/${label}/ differs from the server path in ${bad.join(", ")} -- run npx tsx scripts/wx-fixtures.ts`); stale = true; }
    }
    if (stale) process.exit(1);
    console.log(`wx server fixtures current: ${crops.length} crops + MANIFEST.json; opuk: ${o.crops.length} crops + MANIFEST.json`);
    return;
  }
  for (const [dir, fs] of [[OUT, files], [OUT_OPUK, o.files]] as const) {
    mkdirSync(dir, { recursive: true });
    for (const [n, b] of Object.entries(fs)) writeFileSync(join(dir, n), b);
  }
  for (const c of [...crops, ...o.crops]) console.log(`${c.name.padEnd(34)} ${String(c.bytes).padStart(6)} B  k=${c.coarsen} ${c.flagged ? "flagged" : "       "} ${c.rows}x${c.cols}  ${c.kind}`);
}

/** K12: the opuk set. The frame is real (the wet K1 fixture, read and resampled by the production code); the
 *  request is the device's own (/wx/cref), so the crop comes through the dispatch; the expected cells are the
 *  encoder's, from the flat resampled grid. Pins Date.now itself: main() has restored it by now. */
async function opukSet() {
  const realNow = Date.now;
  Date.now = () => (EPOCH + 60) * 1000;
  try { return await opukSetPinned(); } finally { Date.now = realNow; }
}

async function opukSetPinned() {
  const opuk = productById("opuk")!;
  const dir = join(HERE, "..", "test", "fixtures", "wx", "opera");
  const idx = JSON.parse(readFileSync(join(dir, `${OPUK_FRAME}.ranges.json`), "utf8")) as { ranges: number[][] };
  const bin = new Uint8Array(readFileSync(join(dir, `${OPUK_FRAME}.ranges.bin`)));
  const offs = new Map<string, number>();
  let at = 0;
  for (const [a, z] of idx.ranges) { offs.set(`${a}-${z}`, at); at += z! - a! + 1; }
  const rf: RangeFetch = async (a, z) => {
    const s = offs.get(`${a}-${z}`);
    if (s === undefined) throw new Error(`opuk frame: range ${a}-${z} is not in the fixture`);
    return bin.slice(s, s + z - a + 1);
  };
  const g = OPUK_GRID;
  const w = await readWindow(rf, g);
  const { codes, covered } = resample(w.value, w.quality, mapFor(w.geo, w.window, g), g);
  const r2 = new MemR2();
  const m = frameMeta("opuk", EPOCH, g);
  const fb = new FrameBuilder(m);
  for (let r = 0; r < g.nj; r++) fb.setRow(r, codes.subarray(r * g.ni, (r + 1) * g.ni));
  await r2.put(frameKey(opuk, EPOCH), fb.bytes);
  const rec: LatestRecord = { product: "opuk", epoch: EPOCH, key: frameKey(opuk, EPOCH), ingestedAt: 0, ...g, tile: m.tile, coveredShare: covered };
  await r2.put(latestKey(opuk), JSON.stringify(rec));
  const env = { WX: r2 } as unknown as Env;
  const cellAt = flatCellAt(g, codes);
  const uk = new Map(readFileSync(join(HERE, "..", "test", "fixtures", "wx-airports-uk.txt"), "utf8").split(/\r?\n/)
    .filter((l) => l.trim() && !l.startsWith("#")).map((l) => { const [id, lat, lon] = l.trim().split(/\s+/); return [id!, { lat: Number(lat), lon: Number(lon) }] as const; }));
  const crops = [];
  const files: Record<string, Uint8Array> = {};
  for (const id of OPUK_AIRPORTS) {
    const a = uk.get(id);
    if (!a) throw new Error(`${id} is not in wx-airports-uk.txt`);
    const name = `${id.toLowerCase()}-161km.wxc`;
    const res = await handleWx(new Request(`https://proxy.test/api/v1/blipscope/wx/cref?lat=${a.lat.toFixed(2)}&lon=${a.lon.toFixed(2)}&r=161`), env, "cref");
    if (res.status !== 200) throw new Error(`${name}: handleWx answered ${res.status}`);
    if (!res.headers.get("ETag")?.includes("-opuk-")) throw new Error(`${name}: /wx/cref was not served from opuk`);
    const bytes = new Uint8Array(await res.arrayBuffer());
    const enc = encodeBox(g, EPOCH, cropCellBox(g, quantise(a.lat, a.lon, 161)), cellAt);
    if (sha(bytes) !== sha(enc.bytes)) throw new Error(`${name}: the handler's bytes differ from the encoder's`);
    let echo = 0;
    for (const c of enc.cells) if (c >= 1 && c <= 13) echo++;
    files[name] = bytes;
    crops.push({ name, kind: "opuk, real frame", airport: id, rKm: 161, frame: `opuk from OPERA ${OPUK_FRAME}`, bytes: bytes.length,
      sha256: sha(bytes), coarsen: enc.coarsen, flagged: enc.flagged, rows: enc.rows, cols: enc.cols, echoCells: echo,
      cellsSha256: sha(enc.cells) });
  }
  const manifest = JSON.stringify({
    format: "WXC1 v1, made by the server path (proxy/src/wx/handler.ts, /wx/cref dispatched to opuk) over a REAL frame in an in-memory R2",
    generator: "proxy/scripts/wx-fixtures.ts (regenerate; workers.yml runs --check)",
    frame: `OPERA ${OPUK_FRAME} DBZH, via proxy/src/wx/opera.ts and resample.ts (proxy/test/fixtures/wx/opera/)`,
    expected: "cellsSha256 = sha256 of the ENCODER's own cells (row-major codes), not of a decode",
    epoch: EPOCH,
    crops,
  }, null, 1) + "\n";
  files["MANIFEST.json"] = new TextEncoder().encode(manifest);
  return { crops, files };
}

main().catch((e) => { console.log(`FAIL: ${e instanceof Error ? e.message : String(e)}`); process.exit(2); });
