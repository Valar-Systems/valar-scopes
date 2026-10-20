// A7: replay cron schedules against A6's 60 recorded frames (docs/wx-spike-2026-10.md, "A7 ... frozen").
//
//   node --experimental-strip-types tools/wx/a7-replay.ts <a6-poll.jsonl>
//
// Each frame's landing time is its S3 LastModified; processing time is that frame's own A6 measurement
// (crop ready minus seen). A schedule picks a frame up at its first firing at or after landing. Zero cron
// jitter is assumed (Cloudflare documents none; Stage 2 measures it).
//
// CONTROL first: the same code with pickup = A6's actual seen time must reproduce A6's published numbers
// (p50 1.06, p95 1.28; sabotage p50 1.33, p95 3.28, max 3.32). If it does not, nothing below it is reported.
import { readFileSync } from "node:fs";

interface Row { key: string; frameT: number; lastModT: number; seenT: number; readyT: number }
const rows: Row[] = readFileSync(process.argv[2], "utf8").trim().split(/\r?\n/).map((l) => JSON.parse(l));
rows.sort((a, b) => a.frameT - b.frameT);

// A6's own percentile (its analysis was Python): sorted[min(n-1, round(p*(n-1)))], round = half to even.
// The first run of this replay used nearest-rank and parity-even skips, and its control did NOT reproduce A6
// (p50 1.05, sabotage p95 3.23); these are A6's definitions, read from its analysis code.
const roundHalfEven = (x: number) => { const f = Math.floor(x), d = x - f; return d > 0.5 || (d === 0.5 && f % 2 !== 0) ? f + 1 : f; };
const pct = (xs: number[], p: number) => { const s = xs.filter(Number.isFinite).sort((a, b) => a - b); return s[Math.min(s.length - 1, roundHalfEven(p * (s.length - 1)))]; };
const min = (ms: number) => (ms / 60000).toFixed(2);

// One schedule -> per-frame pickup and ready times, then the two frozen Stage 2 checks.
function grade(name: string, pickup: (r: Row) => number, period: number | null, sabotage: boolean) {
  const ready = rows.map((r) => pickup(r) + (r.readyT - r.seenT));
  let waitOut = 0;
  if (period !== null) for (const r of rows) { const w = pickup(r) - r.lastModT; if (w < 0 || w >= period) waitOut++; }
  // Sabotage: skip every other frame (odd indices, as A6 did); a skipped frame's crop is the next processed
  // frame's. The last frame is skipped with no later frame in the run: it gets no crop (Infinity), which the
  // percentiles leave out (as A6's analysis did) and the no-skip check counts.
  const processed = rows.map((_, i) => !(sabotage && i % 2 === 1));
  const lag = rows.map((r, i) => {
    let j = i; while (j < rows.length && !processed[j]) j++;
    return j < rows.length ? ready[j] - r.frameT : Infinity;
  });
  let inOrder = true;
  for (let i = 1; i < rows.length; i++) if (processed[i] && processed[i - 1] && ready[i] < ready[i - 1]) inOrder = false;
  const skipped = processed.filter((p) => !p).length;
  const p95 = pct(lag, 0.95);
  const c1 = p95 <= 120000, c2 = skipped === 0 && inOrder;
  console.log(`${name.padEnd(34)} p50 ${min(pct(lag, 0.5))}  p95 ${min(p95)}  max ${min(Math.max(...lag.filter(Number.isFinite)))} min | ` +
              `p95<=2min ${c1 ? "GREEN" : "RED"} | no skips, in order ${c2 ? "GREEN" : "RED"} (${skipped} skipped)` +
              ` | no crop at all: ${lag.filter((x) => !Number.isFinite(x)).length}` + (period !== null ? ` | waits outside [0,${period / 1000})s: ${waitOut}` : ""));
  return { p50: pct(lag, 0.5), p95, c1, c2, waitOut };
}

console.log(`${rows.length} frames, ${new Date(rows[0].frameT).toISOString()} .. ${new Date(rows[rows.length - 1].frameT).toISOString()}`);
console.log("-- CONTROL: A6's own pickups (seen time) must reproduce A6 (p50 1.06, p95 1.28; sabotage p50 1.33, p95 3.28, max 3.32)");
const cA = grade("A6 as measured", (r) => r.seenT, null, false);
const cS = grade("A6 as measured, sabotage", (r) => r.seenT, null, true);
const ctlOk = min(cA.p50) === "1.06" && min(cA.p95) === "1.28" && min(cS.p50) === "1.33" && min(cS.p95) === "3.28";
console.log(`CONTROL ${ctlOk ? "REPRODUCES A6" : "DOES NOT REPRODUCE A6 -- replay not reported"}`);
if (!ctlOk) process.exit(2);

console.log("-- REPLAY");
const ceilTo = (t: number, step: number) => Math.ceil(t / step) * step;
const a = grade("(a) cron, once a minute", (r) => ceilTo(r.lastModT, 60000), 60000, false);
const a2 = grade("(a') cron + a check 30 s in", (r) => ceilTo(r.lastModT, 30000), 30000, false);
const as = grade("(a) sabotage: skip every other", (r) => ceilTo(r.lastModT, 60000), 60000, true);
const phase = rows.map((r) => Math.round((r.lastModT % 60000) / 1000));
console.log(`landing second-of-minute: min ${Math.min(...phase)} max ${Math.max(...phase)} median ${pct(phase, 0.5)}`);

// The frozen recommendation rule, including its outside-the-set row.
const outside = a.waitOut > 0 || a2.waitOut > 0 || as.c1 || as.c2;
let verdict: string;
if (outside) verdict = "OUTSIDE THE SET: the replay is wrong -- stop and report, no recommendation";
else if (a.p95 <= 105000) verdict = "(a): p95 <= 1.75 min";
else if (a.p95 <= 120000) verdict = "(a'): 1.75 < p95 <= 2.0 min";
else verdict = "(a'): (a) fails the frozen 2 min bar";
console.log(`VERDICT (frozen rule): ${verdict}`);
