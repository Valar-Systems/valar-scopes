// A4 live-set meter (ruled 2026-10-20): node --expose-gc. A forced gc() then heapUsed + external at every
// pipeline stage boundary and right after each large allocation; not on a timer. One ingest per process.
//   node --expose-gc --experimental-strip-types tools/wx/a4-live.ts <s3 key> <store path> [holdMb]
// Prints the live peak and every sample (stage, MB), so two runs can be compared sample by sample.
import { writeFileSync } from "node:fs";
import { ingestFrame } from "./ingest.ts";

const gc = (globalThis as any).gc as (() => void) | undefined;
if (!gc) { console.error("REFUSED: run with --expose-gc (the live meter needs forced collections)"); process.exit(2); }
const [, , key, storePath, holdArg] = process.argv;
const holdMb = Number(holdArg ?? 0);
const samples: [string, number][] = [];
const sample = (stage: string) => { gc!(); const m = process.memoryUsage(); samples.push([stage, m.heapUsed + m.external]); };
sample("start");
const { frame } = await ingestFrame(`https://noaa-mrms-pds.s3.amazonaws.com/${key}`, holdMb, sample);
writeFileSync(storePath, frame.packed); sample("stored");
const MB = (b: number) => +(b / 1048576).toFixed(2);
const peak = Math.max(...samples.map((s) => s[1]));
console.log(JSON.stringify({ frame: frame.validTimeUtc, holdMb, livePeakMB: MB(peak), n: samples.length,
  samples: samples.map(([s, v]) => [s, MB(v)]) }));
