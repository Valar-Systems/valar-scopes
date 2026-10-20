// A4 in Node: one ingest per process, so the peak belongs to this run alone.
//   node --experimental-strip-types tools/wx/a4-node.ts <s3 key> <store path> [holdMb]
// Reports wall time, CPU time (user + system) and peak RSS (process.resourceUsage().maxRSS).
import { writeFileSync } from "node:fs";
import { ingestFrame } from "./ingest.ts";

const [, , key, storePath, holdArg] = process.argv;
const holdMb = Number(holdArg ?? 0);
const rssStart = process.memoryUsage().rss;
const t0 = performance.now(), c0 = process.cpuUsage();
const { frame } = await ingestFrame(`https://noaa-mrms-pds.s3.amazonaws.com/${key}`, holdMb);
writeFileSync(storePath, frame.packed);                       // "store": the packed frame object
const wall = performance.now() - t0, cpu = process.cpuUsage(c0);
const peakMb = process.resourceUsage().maxRSS / 1024;
console.log(JSON.stringify({ frame: frame.validTimeUtc, gzBytes: frame.gzBytes, packedBytes: frame.packed.length, holdMb,
  wallMs: Math.round(wall), cpuMs: Math.round((cpu.user + cpu.system) / 1000),
  rssAtStartMb: +(rssStart / 1048576).toFixed(1), peakRssMb: +peakMb.toFixed(1) }));
