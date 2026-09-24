/**
 * drift-report.ts -- combine the render-drift job's two halves into one report.
 *
 *   npx tsx scripts/drift-report.ts <a.json> <b.json> <report.json>
 *
 * A missing or unparseable half is FAILED, never zero (photo-drift.ts). Prints
 * the labelled report WITH its interpretation rule, writes the report JSON the
 * check run carries, and exits 0 CLEAN / 1 DRIFT / 3 FAILED.
 */
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { combine, exitCodeFor, formatReport, type DryRunStatus, type VerifyStatus } from "./photo-drift";

function load<T>(path: string | undefined): T | null {
  if (!path || !existsSync(path)) return null;
  try {
    return JSON.parse(readFileSync(path, "utf8")) as T;
  } catch {
    return null;
  }
}

const [aPath, bPath, outPath] = process.argv.slice(2);
const report = combine(load<DryRunStatus>(aPath), load<VerifyStatus>(bPath));
console.log(formatReport(report));
if (outPath) writeFileSync(outPath, JSON.stringify(report, null, 2) + "\n");
process.exitCode = exitCodeFor(report);
