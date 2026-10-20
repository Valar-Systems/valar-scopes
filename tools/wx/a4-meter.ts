// A4 re-meter (ruled 2026-10-20): V8's own accounting, heapUsed + external (arrayBuffers is inside external),
// sampled at every pipeline stage boundary and every 5 ms. One ingest per process.
//   node --experimental-strip-types tools/wx/a4-meter.ts <s3 key> <store path> [holdMb]
import { writeFileSync } from "node:fs";
import { ingestFrame } from "./ingest.ts";

const [, , key, storePath, holdArg] = process.argv;
const holdMb = Number(holdArg ?? 0);
const mem = () => { const m = process.memoryUsage(); return m.heapUsed + m.external; };
let peak = 0, samples = 0; const stages: Record<string, number> = {};
const sample = (stage?: string) => { const v = mem(); samples++; if (v > peak) peak = v; if (stage) stages[stage] = Math.max(stages[stage] ?? 0, v); };
const base = mem(); sample("start");
const timer = setInterval(() => sample(), 5);
const t0 = performance.now(), c0 = process.cpuUsage();
const { frame } = await ingestFrame(`https://noaa-mrms-pds.s3.amazonaws.com/${key}`, holdMb, (s) => sample(s));
writeFileSync(storePath, frame.packed); sample("stored");
const wall = performance.now() - t0, cpu = process.cpuUsage(c0);
clearInterval(timer);
const MB = (b: number) => +(b / 1048576).toFixed(1);
console.log(JSON.stringify({ frame: frame.validTimeUtc, gzMB: MB(frame.gzBytes), holdMb, wallMs: Math.round(wall),
  cpuMs: Math.round((cpu.user + cpu.system) / 1000), baselineMB: MB(base), peakMB: MB(peak), samples,
  stagesMB: Object.fromEntries(Object.entries(stages).map(([k, v]) => [k, MB(v)])) }));
