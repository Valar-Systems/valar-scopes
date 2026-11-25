#!/usr/bin/env node
// The screen service's full-logs watch verdict (docs/screen-logs-runbook.md, "Why full logs cannot silently stay on").
// It grades the service's public /health `logs` field, so a full-logs day that outlives its end is RED the next time
// the watch runs, whoever forgot the step that ends it.
//
//   node scripts/screen-logs-verdict.mjs <health url>      one anonymous GET, then the verdict
//   node scripts/screen-logs-verdict.mjs --body '<json>'   grade a given /health body (the proof-run; no network)
//   node scripts/screen-logs-verdict.mjs --selftest        P-V: six canned bodies on a fixed clock, then Vs (a COPY that
//                                                          compares `until` the wrong way round) must fail exactly its two
//   exit 0 GREEN, 1 RED, 2 BLIND (unreachable, not 200, no `logs`, or a shape the table does not name)
//
//   | /health logs                   | verdict |
//   | rate 0.1, until null           | GREEN   |
//   | rate 1.0, until ahead          | GREEN   (a full-logs day in progress)
//   | rate 1.0, until in the PAST    | RED     "full logs past their day"
//   | rate 1.0, until null           | RED     "full logs with no until" (a bundle the deploy script did not build)
//   | anything else                  | BLIND
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

/** The verdict for one /health body at time `now` (ms): { code: 0 | 1 | 2, line }. */
export function verdict(body, now) {
  /* STUB (red): the verdict is not written yet. */
  return { code: 0, line: "GREEN (stub)" };
}

const HOUR = 3_600_000;
export const CASES = (now) => [
  ["rate 0.1, until null", { logs: { rate: 0.1, until: null } }, 0],
  ["until +1 h", { logs: { rate: 1, until: new Date(now + HOUR).toISOString() } }, 0],
  ["until -1 h", { logs: { rate: 1, until: new Date(now - HOUR).toISOString() } }, 1],
  ["rate 1.0, until null", { logs: { rate: 1, until: null } }, 1],
  ["no logs field", { service: "blipscope-screen" }, 2],
  ["unstamped (rate null)", { logs: { rate: null, until: null } }, 2],
];

function grade(fn, now) {
  return CASES(now).map(([name, body, want]) => ({ name, want, got: fn(body, now).code }));
}

async function selftest() {
  const now = Date.parse("2026-12-02T12:00:00Z");
  const real = grade(verdict, now);
  const off = real.filter((r) => r.got !== r.want);
  real.forEach((r) => console.log(`${r.name}: want ${r.want}, got ${r.got} -> ${r.got === r.want ? "AS PREDICTED" : "NOT AS PREDICTED"}`));
  if (off.length) { console.log(`SELFTEST FAILED: ${off.length} of ${real.length} cases NOT AS PREDICTED`); return 1; }
  // Vs: `until` compared the wrong way round, in a COPY; exactly the two rate-1.0-with-until cases must flip.
  const src = readFileSync(fileURLToPath(import.meta.url), "utf8");
  const sab = src.replace("/*P-V*/ t > now", "/*P-V*/ t < now");
  if (sab === src) { console.log("Vs: the edit did not apply"); return 1; }
  const dir = mkdtempSync(join(tmpdir(), "screen-verdict-"));
  try {
    const p = join(dir, "verdict-vs.mjs");
    writeFileSync(p, sab);
    const mod = await import(pathToFileURL(p).href);
    const flipped = grade(mod.verdict, now).filter((r) => r.got !== r.want).map((r) => r.name);
    const ok = flipped.length === 2 && flipped.includes("until +1 h") && flipped.includes("until -1 h");
    console.log(`Vs: ${flipped.length} case(s) flipped (${flipped.join(", ")}) -> ${ok ? "AS PREDICTED" : "NOT AS PREDICTED"}`);
    console.log(ok ? "SELFTEST PASSED" : "SELFTEST FAILED");
    return ok ? 0 : 1;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

async function main(argv) {
  if (argv[0] === "--selftest") return selftest();
  let body;
  if (argv[0] === "--body" && argv[1]) {
    try { body = JSON.parse(argv[1]); } catch { console.log("BLIND: --body is not JSON"); return 2; }
  } else if (argv[0] && /^https:\/\//.test(argv[0])) {
    try {
      const r = await fetch(argv[0], { headers: { "Cache-Control": "no-cache" } });
      if (r.status !== 200) { console.log(`BLIND: /health answered ${r.status}`); return 2; }
      body = await r.json();
    } catch (e) { console.log(`BLIND: /health unreachable (${e?.name ?? "error"})`); return 2; }
  } else {
    console.log("usage: screen-logs-verdict.mjs <https url> | --body '<json>' | --selftest");
    return 2;
  }
  const v = verdict(body, Date.now());
  console.log(v.line);
  return v.code;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? "").href) process.exitCode = await main(process.argv.slice(2));
