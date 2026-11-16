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
  source: string;       // 2r item 7b: the run's own commits, so --restore can tell its run's seal from a later one
  prevSource: string;
  sentinel: string | null;
  meta: string;
  journal: string | null;
  keys: Record<string, string | null>;
}

/** One git answer: what the BK0 check needs from `git rev-parse --is-inside-work-tree`. */
export interface GitAnswer { status: number | null; stdout: string; stderr: string; error?: Error }
const askGit = (dir: string): GitAnswer => {
  const r = spawnSync("git", ["-C", dir, "rev-parse", "--is-inside-work-tree"], { encoding: "utf8" });
  return { status: r.status, stdout: r.stdout ?? "", stderr: r.stderr ?? "", ...(r.error ? { error: r.error } : {}) };
};

/** Why `path` may not hold a backup, or null when it may (BK0). A backup is private: never inside a git work tree, where
 *  a `git add -A` would publish it, and never over an existing file. ONLY git's own "not a git repository" answer allows
 *  the path (2r item 5): a dubious-ownership refusal, an unreadable .git or a git that cannot run all REFUSE, because a
 *  check that cannot answer proves nothing. `git` is a FUNCTION PARAMETER for the selftest's BK0g/BK0e/BK0n rows only;
 *  the writer calls this with one argument (BK0s), so no flag or environment variable reaches it. */
export function backupPathRefusal(path: string, git: (dir: string) => GitAnswer = askGit): string | null {
  const p = resolve(path);
  if (existsSync(p)) return "the backup file already exists";
  const dir = dirname(p);
  if (!existsSync(dir) || !statSync(dir).isDirectory()) return "the backup's folder does not exist";
  const r = git(dir);
  if (r.error) return "git could not be run to check that the path lies outside every git work tree";
  if (r.status === 0) return r.stdout.trim() === "true" ? "the path lies inside a git work tree" : "git did not say the path lies outside every work tree";
  if (/not a git repository/i.test(r.stderr)) return null;
  return `git could not answer for the path (exit ${r.status ?? "?"}); only its own "not a git repository" answer allows a backup there`;
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
