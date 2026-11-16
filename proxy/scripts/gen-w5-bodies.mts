// Writes test/fixtures/wx/nws/w5/{alerts,conditions,forecast}.json: W5's three bodies for the SYNTHETIC warm case
// (test/fixtures/wx/nws/w5-raw/warm), as `ok`, fetched at FETCHED_UTC (docs/weatherscope-stage4.md S4.11, WS20).
// test/weatherscope.test.ts holds the live route's output to these files, and W8's device parser test reads them, so
// the device side is graded on what the Worker actually sends.
//
//   npx tsx scripts/gen-w5-bodies.mts            write
//   npx tsx scripts/gen-w5-bodies.mts --check    exit 1 if any file differs from what would be written
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { trimAlerts, trimForecast, trimObservation, utcOf } from "../src/weatherscope/trim.ts";

const root = (p: string): string => fileURLToPath(new URL(`../../${p}`, import.meta.url));
const raw = (k: string): unknown => JSON.parse(readFileSync(root(`test/fixtures/wx/nws/w5-raw/warm/${k}.json`), "utf8"));

/** The warm case's own capture time: the hourly document's generatedAt, as trim_nws.py's capturedUtc. */
export const FETCHED_UTC = utcOf((raw("hourly") as { properties: { generatedAt: string } }).properties.generatedAt);
const head = { v: 1, status: "ok", fetchedUtc: FETCHED_UTC };

const bodies: Record<string, unknown> = {
  alerts: { ...head, items: trimAlerts(raw("alerts")) },
  conditions: { ...head, ...trimObservation(raw("observation")) },
  forecast: { ...head, ...trimForecast(raw("forecast"), raw("hourly")) },
};

const check = process.argv.includes("--check");
let differ = 0;
mkdirSync(root("test/fixtures/wx/nws/w5"), { recursive: true });
for (const [name, body] of Object.entries(bodies)) {
  const path = root(`test/fixtures/wx/nws/w5/${name}.json`);
  const text = JSON.stringify(body, null, 1) + "\n";
  if (check) {
    let cur = "";
    try { cur = readFileSync(path, "utf8"); } catch { /* absent reads as different */ }
    const same = cur === text;
    if (!same) differ++;
    console.log(`  ${same ? "ok  " : "DIFF"}  ${name}`);
  } else {
    writeFileSync(path, text);
    console.log(`  wrote ${name}: ${Buffer.byteLength(text)} B`);
  }
}
if (check) {
  console.log(`w5 bodies: 3 files, ${differ} differ`);
  process.exit(differ ? 1 : 0);
}
