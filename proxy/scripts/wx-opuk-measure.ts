// K11 (docs/wx-opuk.md): opuk's ingest measured in Node on real frames, from the tile bytes in memory to the WXG1
// bytes. The one-time static map (resample.ts mapFor) is timed and printed separately. One measurement per process,
// as A4 and WS8 did. Prints one JSON line.
//
//   npx tsx scripts/wx-opuk-measure.ts fetch <stamp> <dir>                     a frame's head + window rows, from the cache
//   npx tsx scripts/wx-opuk-measure.ts cpu <ranges.json>                       CPU: map once, then one frame
//   node --expose-gc --import tsx scripts/wx-opuk-measure.ts live <ranges.json> [holdMb]   A4's live meter
//
// <ranges.json> is a fixture index (tools/wx/opera_fixture.py's shape): the bytes are its sibling .ranges.bin.

import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { FrameBuilder, frameMeta } from "../src/wx/grid";
import { OPERA_BUCKET, OPERA_UA } from "../src/wx/ingest";
import { HEAD_BYTES, type RangeFetch, parseHead, readWindow, windowFor } from "../src/wx/opera";
import { OPUK_GRID } from "../src/wx/products";
import { mapFor, resample } from "../src/wx/resample";

const MB = (b: number) => +(b / 1048576).toFixed(2);
const cpuS = (c: NodeJS.CpuUsage) => +((c.user + c.system) / 1e6).toFixed(3);

function memoryFetch(indexPath: string): RangeFetch {
  const idx = JSON.parse(readFileSync(indexPath, "utf8")) as { ranges: number[][] };
  const bin = new Uint8Array(readFileSync(indexPath.replace(/\.ranges\.json$/, ".ranges.bin")));
  const offs = new Map<string, number>();
  let o = 0;
  for (const [a, z] of idx.ranges) { offs.set(`${a}-${z}`, o); o += z! - a! + 1; }
  if (o !== bin.length) throw new Error(`ranges add up to ${o} B, the file holds ${bin.length}`);
  return async (a, z) => {
    const s = offs.get(`${a}-${z}`);
    if (s === undefined) throw new Error(`range ${a}-${z} is not held`);
    return bin.slice(s, s + z - a + 1);
  };
}

async function main() {
  const mode = process.argv[2];
  if (mode === "fetch") {
    const stamp = process.argv[3]!, dir = process.argv[4]!;
    if (!/^\d{8}T\d{4}$/.test(stamp)) throw new Error("stamp is YYYYMMDDTHHMM");
    const url = `${OPERA_BUCKET}/${stamp.slice(0, 4)}/${stamp.slice(4, 6)}/${stamp.slice(6, 8)}/OPERA/COMP/OPERA@${stamp}@0@DBZH.tiff`;
    const ranges: number[][] = [];
    const parts: Uint8Array[] = [];
    const get: RangeFetch = async (a, z) => {
      const res = await fetch(url, { headers: { Range: `bytes=${a}-${z}`, "User-Agent": OPERA_UA } });
      if (res.status !== 206) throw new Error(`HTTP ${res.status} for ${a}-${z}`);
      const b = new Uint8Array(await res.arrayBuffer());
      ranges.push([a, z]); parts.push(b);
      return b;
    };
    const w = await readWindow(get, OPUK_GRID);
    const bin = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
    let o = 0; for (const p of parts) { bin.set(p, o); o += p.length; }
    writeFileSync(join(dir, `${stamp}.ranges.bin`), bin);
    writeFileSync(join(dir, `${stamp}.ranges.json`), JSON.stringify({ url, ranges }, null, 1));
    console.log(JSON.stringify({ mode, stamp, ranges: ranges.length, bytes: bin.length, window: w.window }));
    return;
  }

  const index = process.argv[3]!;
  const fetchRange = memoryFetch(index);
  const g = OPUK_GRID;
  if (mode === "cpu") {
    // the static map, once per isolate: from the head's geometry alone
    const geo = parseHead(await fetchRange(0, HEAD_BYTES - 1));
    const m0 = process.cpuUsage();
    mapFor(geo, windowFor(geo, g), g);
    const mapCpu = cpuS(process.cpuUsage(m0));
    // one frame, from the tile bytes in memory to the WXG1 bytes
    const c0 = process.cpuUsage(), t0 = performance.now();
    const w = await readWindow(fetchRange, g);
    const { codes, covered } = resample(w.value, w.quality, mapFor(w.geo, w.window, g), g);
    const fb = new FrameBuilder(frameMeta("opuk", 0, g));
    for (let r = 0; r < g.nj; r++) fb.setRow(r, codes.subarray(r * g.ni, (r + 1) * g.ni));
    const c = process.cpuUsage(c0), wall = performance.now() - t0;
    console.log(JSON.stringify({ mode, frame: index.replace(/^.*[\\/]/, "").slice(0, 13), mapCpuS: mapCpu, cpuS: cpuS(c),
      wallS: +(wall / 1000).toFixed(3), bytes: fb.bytes.length, covered: +covered.toFixed(4),
      sha12: createHash("sha256").update(fb.bytes).digest("hex").slice(0, 12) }));
  } else if (mode === "live") {
    const gc = (globalThis as { gc?: () => void }).gc;
    if (!gc) { console.log(JSON.stringify({ blind: "run with node --expose-gc (the live meter needs forced collections)" })); process.exit(2); }
    const holdMb = Number(process.argv[4] ?? 0);
    const samples: Array<[string, number]> = [];
    const sample = (stage: string) => { gc(); const m = process.memoryUsage(); samples.push([stage, m.heapUsed + m.external]); };   // A4's meter: external holds the ArrayBuffers
    sample("start");
    const hold = holdMb > 0 ? new Uint8Array(holdMb * 1048576) : null;
    if (hold) { for (let i = 0; i < hold.length; i += 4096) hold[i] = 1; sample("alloc-hold"); }
    let n = 0;
    const metered: RangeFetch = async (a, z) => { const b = await fetchRange(a, z); sample(`range-${n++}`); return b; };
    const w = await readWindow(metered, g); sample("window");
    const m = mapFor(w.geo, w.window, g); sample("map");
    const { codes } = resample(w.value, w.quality, m, g); sample("resampled");
    const fb = new FrameBuilder(frameMeta("opuk", 0, g));
    for (let r = 0; r < g.nj; r++) fb.setRow(r, codes.subarray(r * g.ni, (r + 1) * g.ni));
    sample("wxg1");
    let touch = 0; if (hold) for (let i = 0; i < hold.length; i += 4096) touch += hold[i]!;
    const peak = Math.max(...samples.map((s) => s[1]));
    console.log(JSON.stringify({ mode, frame: index.replace(/^.*[\\/]/, "").slice(0, 13), holdMb, held: touch * 4096,
      livePeakMB: MB(peak), n: samples.length, bytes: fb.bytes.length, samples: samples.map(([s, v]) => [s, MB(v)]) }));
  } else {
    console.log("usage: wx-opuk-measure.ts fetch <stamp> <dir> | cpu <ranges.json> | live <ranges.json> [holdMb]");
    process.exit(2);
  }
}

main().catch((e) => { console.log(JSON.stringify({ fail: String(e) })); process.exit(1); });
