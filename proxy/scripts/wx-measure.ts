// WS8 (docs/wx-server.md): the real thing, measured locally in Node on Stage 1's storm frame. Graded by the
// details frozen before the first run (docs/wx-server.md, "WS8, grading details").
//
//   npx tsx scripts/wx-measure.ts cpu                      one un-forced run: CPU + wall, download -> WXG1
//   node --expose-gc --import tsx scripts/wx-measure.ts live [holdMb]   one live-meter run (A4's meter)
//   npx tsx scripts/wx-measure.ts crops                    the 200 WS2 crops on the real frame, timed
// One measurement per process, as A4 did. Prints one JSON line.

import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { cropCellBox, encodeBox, quantise } from "../src/wx/crop";
import { tileBytes, tileOffset, tiledCellAt, tilesFor } from "../src/wx/grid";
import { NOAA_BUCKET, buildFrame, epochOfKey } from "../src/wx/ingest";
import { PRODUCTS } from "../src/wx/products";

const KEY = "CONUS/MergedReflectivityQCComposite_00.50/20240521/MRMS_MergedReflectivityQCComposite_00.50_20240521-220042.grib2.gz";
const WANT_SHA = "0bd824f76981ae5f470f52a5e19f0d69257d5fd7c99ab4634ef67eca6ef42829";   // test/fixtures/wx/MANIFEST.json
const cref = PRODUCTS[0]!;
const MB = (b: number) => +(b / 1048576).toFixed(2);

async function download(): Promise<Uint8Array> {
  const res = await fetch(`${NOAA_BUCKET}/${KEY}`);
  if (!res.ok) { console.log(JSON.stringify({ blind: `NOAA ${res.status}` })); process.exit(2); }
  const gz = new Uint8Array(await res.arrayBuffer());
  const got = createHash("sha256").update(gz).digest("hex");
  if (got !== WANT_SHA) { console.log(JSON.stringify({ blind: `download sha256 ${got.slice(0, 12)} is not the storm frame's ${WANT_SHA.slice(0, 12)}` })); process.exit(2); }
  return gz;
}

async function main() {
  const mode = process.argv[2];
  const epoch = epochOfKey(cref, KEY)!;
  if (mode === "cpu") {
    const c0 = process.cpuUsage(), t0 = performance.now();
    const gz = await download();
    const fb = await buildFrame(cref, epoch, gz);
    const c = process.cpuUsage(c0), wall = performance.now() - t0;
    console.log(JSON.stringify({ mode, cpuS: +((c.user + c.system) / 1e6).toFixed(3), wallS: +(wall / 1000).toFixed(3), bytes: fb.bytes.length }));
  } else if (mode === "live") {
    const gc = (globalThis as { gc?: () => void }).gc;
    if (!gc) { console.log(JSON.stringify({ blind: "run with node --expose-gc (the live meter needs forced collections)" })); process.exit(2); }
    const holdMb = Number(process.argv[3] ?? 0);
    const samples: Array<[string, number]> = [];
    const sample = (stage: string) => { gc(); const m = process.memoryUsage(); samples.push([stage, m.heapUsed + m.external]); };
    sample("start");
    const hold = holdMb > 0 ? new Uint8Array(holdMb * 1048576) : null;
    if (hold) { for (let i = 0; i < hold.length; i += 4096) hold[i] = 1; sample("alloc-hold"); }
    const gz = await download(); sample("download");
    const fb = await buildFrame(cref, epoch, gz, sample);
    sample("done");
    let touch = 0; if (hold) for (let i = 0; i < hold.length; i += 4096) touch += hold[i]!;
    const peak = Math.max(...samples.map((s) => s[1]));
    console.log(JSON.stringify({ mode, holdMb, held: touch * 4096, livePeakMB: MB(peak), n: samples.length, bytes: fb.bytes.length,
      samples: samples.map(([s, v]) => [s, MB(v)]) }));
  } else if (mode === "crops") {
    const gz = await download();
    const fb = await buildFrame(cref, epoch, gz);
    const m = fb.meta;
    const airports = readFileSync(join(dirname(fileURLToPath(import.meta.url)), "..", "test", "fixtures", "wx-airports.txt"), "utf8")
      .trim().split(/\r?\n/).map((l) => { const [, lat, lon] = l.trim().split(/\s+/); return { lat: Number(lat), lon: Number(lon) }; });
    const ms: number[] = [];
    let flagged = 0, maxBytes = 0;
    for (const a of airports) for (const r of [25, 80, 160, 300]) {
      const t0 = performance.now();
      const box = cropCellBox(m, quantise(a.lat, a.lon, r));
      const tiles = new Map<string, Uint8Array>();
      for (const [ty, tx] of tilesFor(m, box.r0, box.r0 + box.rows - 1, box.c0, box.c0 + box.cols - 1)) {
        const o = tileOffset(m, ty, tx);
        tiles.set(`${ty}-${tx}`, fb.bytes.slice(o, o + tileBytes(m.tile)));     // a copy, as an R2 read returns
      }
      const e = encodeBox(m, epoch, box, tiledCellAt(m, tiles));
      ms.push(performance.now() - t0);
      if (e.flagged) flagged++;
      maxBytes = Math.max(maxBytes, e.bytes.length);
    }
    const s = [...ms].sort((x, y) => x - y);
    console.log(JSON.stringify({ mode, n: s.length, p50Ms: +s[Math.ceil(0.5 * s.length) - 1]!.toFixed(2),
      p95Ms: +s[Math.ceil(0.95 * s.length) - 1]!.toFixed(2), maxMs: +s[s.length - 1]!.toFixed(2), flagged, maxBytes }));
  } else {
    console.log("usage: wx-measure.ts cpu | live [holdMb] | crops"); process.exit(2);
  }
}

main().catch((e) => { console.log(JSON.stringify({ fail: String(e) })); process.exit(1); });
