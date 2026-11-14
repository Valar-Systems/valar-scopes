// routes-backup.ts -- the backup before any delete, and its read-back (docs/route-weekly-code.md, section 2q and its
// amendments). The writer (ingest-routes.ts) builds the payload from its OWN plan, so the list it saves is never derived
// a second time; this file only decides where a backup may live and writes it with a read-back.
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";

/** What a backup file holds: everything a run changes, as it was before the run. `keys` holds every key the run touches
 *  (writes and deletes), each with its pre-run value, or null where a bulk get's answer said "not found" (amendment 3). */
export interface BackupPayload {
  v: 1;
  env: string;
  runId: string;
  sentinel: string | null;
  meta: string;
  journal: string | null;
  keys: Record<string, string | null>;
}

/** Why `path` may not hold a backup, or null when it may (BK0). A backup is private: never inside a git work tree, where
 *  a `git add -A` would publish it, and never over an existing file. If git cannot answer, the path is refused: a check
 *  that cannot run proves nothing. */
export function backupPathRefusal(path: string): string | null {
  const p = resolve(path);
  if (existsSync(p)) return "the backup file already exists";
  const dir = dirname(p);
  if (!existsSync(dir) || !statSync(dir).isDirectory()) return "the backup's folder does not exist";
  const r = spawnSync("git", ["-C", dir, "rev-parse", "--is-inside-work-tree"], { encoding: "utf8" });
  if (r.error) return "git could not be run to check that the path lies outside every git work tree";
  if (r.status === 0 && r.stdout.trim() === "true") return "the path lies inside a git work tree";
  return null;
}

/** Write `payload` to `path` (mode 600, never over an existing file), read it back, and return its sha256. A read-back
 *  that differs refuses (BK2) and removes the bad file. `hooks` is a FUNCTION PARAMETER for the selftest's BK2 row only:
 *  the writer calls this with two arguments (BK2s), so no flag or environment variable reaches it. */
export function writeBackup(path: string, payload: BackupPayload, hooks: { afterWrite?: (p: string) => void } = {}): string {
  const text = JSON.stringify(payload);
  writeFileSync(path, text, { mode: 0o600, flag: "wx" });
  hooks.afterWrite?.(path);
  const back = readFileSync(path);
  if (back.toString("utf8") !== text) {
    rmSync(path, { force: true });
    throw new Error("REFUSING (BK2): the backup file read back differently from what was written, so it was removed. Nothing written to KV.");
  }
  return createHash("sha256").update(back).digest("hex");
}
