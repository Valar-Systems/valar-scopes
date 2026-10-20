// Generates the WXC1 crop fixtures in this folder with the weather spike's OWN encoder, so the C++
// decoder (include/WxCrop.h) is tested against crops it did not write. Nothing from the spike is copied:
// crop.ts is loaded from a checkout of spike/wx-overlay given on the command line.
//
//   node --experimental-strip-types test/fixtures/wx/make_fixtures.ts \
//        <spike-checkout> <frame.codes> <frame.json> <outDir>
//
// <frame.codes>/<frame.json> come from the spike's tools/wx/decode-cli.ts run on the NOAA object recorded
// in MANIFEST.json (no MRMS file is committed). Airport positions come from the spike's
// tools/wx/airports-a2.txt, except PHNL below. Every centre is a public airport, checked >= 150 km from
// every coordinate in the repo with the spike's tools/wx/forbidden.py. MANIFEST.json holds no coordinates;
// each crop's header carries its own north-west cell centre.
import { readFileSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const [, , spike, codesPath, metaPath, outDir] = process.argv;
if (!outDir) { console.error("usage: make_fixtures.ts <spike-checkout> <frame.codes> <frame.json> <outDir>"); process.exit(2); }
const { encodeCrop, decodeCrop } = await import(pathToFileURL(`${spike}/tools/wx/crop.ts`).href);

const meta = JSON.parse(readFileSync(metaPath, "utf8")).grid;
const g = { codes: new Uint8Array(readFileSync(codesPath)), ni: meta.ni, nj: meta.nj, la1: meta.la1, lo1: meta.lo1,
            di: meta.di, dj: meta.dj, frameEpoch: Math.floor(Date.parse(meta.validTimeUtc) / 1000) };

// ONLY the spike's vetted list (every entry clears forbidden.py). Its airport-candidates.txt is NOT read:
// 11 of its 66 entries are within 150 km of a repo coordinate (KLAX among them, found 2026-10-20).
const pos = new Map<string, [number, number]>();
for (const line of readFileSync(`${spike}/tools/wx/airports-a2.txt`, "utf8").split(/\r?\n/)) {
  const f = line.trim().split(/\s+/); if (f.length >= 3) pos.set(f[0], [Number(f[1]), Number(f[2])]);
}
pos.set("PHNL", [21.3187, -157.9225]);   // Honolulu: a public airport outside the CONUS grid (forbidden.py: clear)

const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const MI = 1.609344;
const header = (b: Uint8Array) => { const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  return { version: b[4], coarsen: b[5], flags: b[6], frameEpoch: dv.getUint32(8, true), rows: dv.getUint16(28, true), cols: dv.getUint16(30, true) }; };
const histogram = (codes: Uint8Array) => { const h: Record<string, number> = {}; for (const c of codes) h[c] = (h[c] ?? 0) + 1; return h; };

const out: any[] = [];
function emit(name: string, bytes: Uint8Array, params: object) {
  writeFileSync(`${outDir}/${name}`, bytes);
  let expect: object;
  try { const d = decodeCrop(bytes); expect = { cells: d.codes.length, codesSha256: sha(d.codes), histogram: histogram(d.codes) }; }
  catch (e) { expect = { refuse: (e as Error).message }; }
  out.push({ name, bytes: bytes.length, sha256: sha(bytes), header: bytes.length >= 32 ? header(bytes) : null, params, expect });
}
const crop = (icao: string, km: number, extra: object = {}, cap?: number, offByOne = false) => {
  const [lat, lon] = pos.get(icao)!;
  return { e: encodeCrop(g, lat, lon, km, cap, 0, offByOne), params: { airport: icao, radiusKm: km, ...extra } };
};

// The Stage 1 frame: 60 mi around Dallas/Fort Worth, storms to the south-east, dry to the north.
const main = crop("KDFW", 60 * MI, { radiusMi: 60 });
emit("kdfw-60mi.wxc", main.e.bytes, main.params);
// Edge cases.
const uni = crop("KPHX", 40);          emit("kphx-40km-uniform.wxc", uni.e.bytes, uni.params);
const off = crop("PHNL", 60 * MI, { radiusMi: 60 }); emit("phnl-60mi-outside-grid.wxc", off.e.bytes, off.params);
const big = crop("KMSP", 222);         emit("kmsp-222km-coarsened.wxc", big.e.bytes, big.params);
// Negative cases, all derived from the KDFW crop.
const bad = main.e.bytes.slice(); bad[0] = 0x58;                     // "XXC1"
emit("neg-bad-magic.wxc", bad, { from: "kdfw-60mi.wxc", change: "byte 0 'W' -> 'X'" });
const short = crop("KDFW", 60 * MI, { radiusMi: 60, sabotage: "crop.ts longRunOffByOne: every long run written one short" }, undefined, true);
emit("neg-run-short.wxc", short.e.bytes, short.params);
let cut = -1;                                                        // the last long run whose LEB128 takes >= 2 bytes
for (let i = 32; i < main.e.bytes.length; ) {
  const b = main.e.bytes[i++];
  if ((b & 15) === 0) { const first = i; if (main.e.bytes[i] & 0x80) cut = first + 1; while (main.e.bytes[i++] & 0x80); }
}
if (cut < 0) throw new Error("the KDFW crop has no multi-byte LEB128 run to truncate");
emit("neg-truncated-leb128.wxc", main.e.bytes.slice(0, cut), { from: "kdfw-60mi.wxc", change: `cut after byte ${cut}, inside a LEB128 length` });

// A2-style round-trip corpus: every vetted airport at 60 mi on this frame. The expected value is the
// ENCODER's own record of the cells it wrote (crop.ts EncodedCrop.cells), as A2 checked. The twin of each
// crop is encoded with crop.ts's own sabotage (every long run written one short) and must be refused.
// Files are records of [u32 little-endian length][crop bytes].
const corpus: any[] = [];
const goodParts: Uint8Array[] = [], sabParts: Uint8Array[] = [];
const rec = (b: Uint8Array) => { const r = new Uint8Array(4 + b.length); new DataView(r.buffer).setUint32(0, b.length, true); r.set(b, 4); return r; };
for (const icao of [...pos.keys()].filter((k) => k !== "PHNL")) {
  const ok = crop(icao, 60 * MI), sab = crop(icao, 60 * MI, {}, undefined, true);
  goodParts.push(rec(ok.e.bytes)); sabParts.push(rec(sab.e.bytes));
  corpus.push({ airport: icao, bytes: ok.e.bytes.length, cells: ok.e.cells.length, cellsSha256: sha(ok.e.cells),
                twinDiffers: sab.e.bytes.length !== ok.e.bytes.length || sab.e.bytes.some((v: number, i: number) => v !== ok.e.bytes[i]) });
}
const cat = (parts: Uint8Array[]) => { const o = new Uint8Array(parts.reduce((n, p) => n + p.length, 0)); let i = 0; for (const p of parts) { o.set(p, i); i += p.length; } return o; };
writeFileSync(`${outDir}/corpus-60mi.bin`, cat(goodParts));
writeFileSync(`${outDir}/corpus-60mi-sabotaged.bin`, cat(sabParts));
console.log(`corpus: ${corpus.length} crops at 60 mi; twins that differ: ${corpus.filter((c) => c.twinDiffers).length}`);

const manifest = {
  format: "WXC1 (docs/wx-spike-2026-10.md, 'Wire format v1', on spike/wx-overlay)",
  inputs: {
    encoder: "spike/wx-overlay tools/wx/crop.ts, last changed at 9147fd9579c8e1f2b0c751a935ba2a14f71e9110",
    decoder: "spike/wx-overlay tools/wx/mrms.ts, last changed at 681c80999bc347f05d5326fd4afb7b4a8e350b0d",
    noaaObject: "s3://noaa-mrms-pds/CONUS/MergedReflectivityQCComposite_00.50/20240521/MRMS_MergedReflectivityQCComposite_00.50_20240521-220042.grib2.gz",
    noaaSha256: "0bd824f76981ae5f470f52a5e19f0d69257d5fd7c99ab4634ef67eca6ef42829",
    frameValid: meta.validTimeUtc,
    capBytes: 32768,
  },
  fixtures: out,
  corpus: { good: "corpus-60mi.bin", sabotaged: "corpus-60mi-sabotaged.bin",
            note: "records of [u32 LE length][crop]; each good crop must decode to cellsSha256; each sabotaged twin that differs must be refused",
            records: corpus },
};
writeFileSync(`${outDir}/MANIFEST.json`, JSON.stringify(manifest, null, 1) + "\n");
for (const f of out) console.log(`${f.name.padEnd(28)} ${String(f.bytes).padStart(6)} B  ${"refuse" in f.expect ? "refuse: " + f.expect.refuse : `${f.header.rows}x${f.header.cols} k=${f.header.coarsen} flags=${f.header.flags}`}`);
