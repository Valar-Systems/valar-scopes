// routes-tmp.ts -- the route tools' temp dirs, in ONE home (docs/route-weekly-code.md, section 2g; the reviewer,
// 2026-11-14). ingest-routes.ts and verify-routes.ts both import it.
//
// WHY: on 2026-11-14 the reviewer found 371 dirs (4.72 GB) these tools had left in the system temp: each holds an
// extracted ~13 MB snapshot, none was ever removed, and the selftests and sabotages made dozens per run.
//   1. tmpDir() registers every dir it makes, and an "exit" handler removes them all. Not try/finally: the tools refuse
//      via process.exit(), which skips finally blocks; an exit handler runs on it and on the uncaught-error path.
//   2. A kill -9 still skips the handler, and TMP1 (the selftests' check) only sees their PRIVATE temp. So each tool
//      also SWEEPS at start: its OWN prefix dirs, older than STALE_MS, whose content is only what these tools write.
//      THE 6-HOUR FLOOR IS WHAT KEEPS A CONCURRENT RUN'S LIVE DIRS SAFE: no run of these tools lasts anywhere near it, so
//      a dir that young may belong to a run in progress, and is left alone. Another prefix, and Temp\claude, are never
//      touched. A count is printed, never a name.
import { mkdtempSync, readdirSync, rmSync, statSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

export const STALE_MS = 6 * 3_600_000;
/** What these tools leave in a temp dir: an extracted snapshot, or their own JSON files. Anything else is foreign. */
const OURS = /^(standing-data-.+|bulk-\d+\.json|put-\d+\.json|delete-\d+\.json|bulk-get-\d+\.json|meta\.json|stale\.json|sentinel-delete\.json|verify-bad-keys\.json|sd\.tar\.gz)$/;

const made: string[] = [];
/** A temp dir under the system temp, removed when this process exits (process.exit() included). */
export function tmpDir(prefix: string): string {
  const d = mkdtempSync(join(tmpdir(), prefix));
  made.push(d);
  return d;
}
const cleanup = () => {
  for (const d of made) { try { rmSync(d, { recursive: true, force: true }); } catch { /* best effort */ } }
};
process.on("exit", cleanup);
// M3 (the review): a GitHub cancel or timeout arrives as SIGINT or SIGTERM, which skip the "exit" handler. Clean up,
// then exit with the shell's conventional 128 + signal number, so the caller still sees the run was stopped.
process.on("SIGINT", () => { cleanup(); process.exit(130); });
process.on("SIGTERM", () => { cleanup(); process.exit(143); });

/** Remove THIS tool's stale dirs in `dir`: name `<prefix><6 chars>`, mtime older than STALE_MS, content only ours.
 *  Returns counts; a fresh dir is kept, a stale dir with foreign content is kept and counted as foreign. */
export function sweepStale(dir: string, prefixes: string[], nowMs: number): { removed: number; kept: number; foreign: number } {
  const out = { removed: 0, kept: 0, foreign: 0 };
  let names: string[] = [];
  try { names = readdirSync(dir); } catch { return out; }
  for (const name of names) {
    if (name === "claude") continue;
    if (!prefixes.some((p) => name.startsWith(p) && /^[A-Za-z0-9]{6}$/.test(name.slice(p.length)))) continue;
    const p = join(dir, name);
    let st;
    try { st = statSync(p); } catch { continue; }
    if (!st.isDirectory()) continue;
    if (nowMs - st.mtimeMs < STALE_MS) { out.kept++; continue; }
    let entries: string[] = [];
    try { entries = readdirSync(p); } catch { out.foreign++; continue; }
    if (!entries.every((e) => OURS.test(e))) { out.foreign++; continue; }
    try { rmSync(p, { recursive: true, force: true }); out.removed++; } catch { out.foreign++; }
  }
  return out;
}

/** The start-of-run sweep over the system temp, for this tool's prefixes. Prints one count line when it found any. */
export function sweepAtStart(prefixes: string[]): void {
  const r = sweepStale(tmpdir(), prefixes, Date.now());
  if (r.removed + r.foreign > 0) {
    console.log(`temp sweep: removed ${r.removed} stale dir(s) of this tool; kept ${r.kept} fresh, ${r.foreign} with foreign content`);
  }
}
