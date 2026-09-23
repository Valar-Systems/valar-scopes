/**
 * github-api.ts -- the photo dashboard's path to main, and back.
 *
 * Only what a publish needs: read main, build one commit from files (Git Data
 * API), move main FAST-FORWARD ONLY, and read back the publish's run and its
 * `photos-publish` check run. The token is passed in and never logged.
 */
const API = "https://api.github.com";

export class NotFastForward extends Error {}

export interface Gh {
  repo: string; // "owner/name"
  token: string;
}

async function call<T>(gh: Gh, method: string, path: string, body?: unknown): Promise<T> {
  const r = await fetch(`${API}/repos/${gh.repo}${path}`, {
    method,
    headers: {
      Authorization: `Bearer ${gh.token}`,
      Accept: "application/vnd.github+json",
      "X-GitHub-Api-Version": "2022-11-28",
      ...(body ? { "Content-Type": "application/json" } : {}),
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await r.text();
  if (!r.ok) {
    // A ref update that is not a fast-forward is 422 "Update is not a fast forward".
    if (r.status === 422 && /fast.?forward/i.test(text)) throw new NotFastForward(text.slice(0, 200));
    throw new Error(`GitHub ${method} ${path}: HTTP ${r.status} ${text.slice(0, 300)}`);
  }
  return (text ? JSON.parse(text) : {}) as T;
}

export async function mainHead(gh: Gh): Promise<string> {
  const ref = await call<{ object: { sha: string } }>(gh, "GET", "/git/ref/heads/main");
  return ref.object.sha;
}

/** A file's bytes at a ref, or null when it does not exist there. */
export async function fileAt(gh: Gh, path: string, ref: string): Promise<Buffer | null> {
  try {
    const f = await call<{ content: string; encoding: string; sha: string; size: number }>(
      gh, "GET", `/contents/${path}?ref=${ref}`,
    );
    if (f.encoding === "base64" && f.content) return Buffer.from(f.content, "base64");
    // Over 1 MB the contents API returns no body; fall back to the blob.
    const b = await call<{ content: string }>(gh, "GET", `/git/blobs/${f.sha}`);
    return Buffer.from(b.content, "base64");
  } catch (err) {
    if (String(err).includes("HTTP 404")) return null;
    throw err;
  }
}

/** The git blob sha of a path at a ref (to reuse unchanged bytes without re-uploading). */
export async function blobShaAt(gh: Gh, path: string, ref: string): Promise<string | null> {
  try {
    return (await call<{ sha: string }>(gh, "GET", `/contents/${path}?ref=${ref}`)).sha;
  } catch (err) {
    if (String(err).includes("HTTP 404")) return null;
    throw err;
  }
}

export interface FileChange {
  path: string;
  /** New bytes, or an existing blob sha to reuse. */
  bytes?: Buffer;
  blobSha?: string;
}

// One commit on top of `parent` containing exactly `files`, then main moved to
// it -- fast-forward only, so a publish that raced another one fails with
// NotFastForward instead of silently discarding the other's commit.
export async function commitToMain(gh: Gh, parent: string, files: FileChange[], message: string): Promise<string> {
  const parentCommit = await call<{ tree: { sha: string } }>(gh, "GET", `/git/commits/${parent}`);
  const tree = [];
  for (const f of files) {
    let sha = f.blobSha;
    if (!sha) {
      const b = await call<{ sha: string }>(gh, "POST", "/git/blobs", {
        content: f.bytes!.toString("base64"),
        encoding: "base64",
      });
      sha = b.sha;
    }
    tree.push({ path: f.path, mode: "100644", type: "blob", sha });
  }
  const t = await call<{ sha: string }>(gh, "POST", "/git/trees", { base_tree: parentCommit.tree.sha, tree });
  const c = await call<{ sha: string }>(gh, "POST", "/git/commits", { message, tree: t.sha, parents: [parent] });
  await call(gh, "PATCH", "/git/refs/heads/main", { sha: c.sha, force: false });
  return c.sha;
}

export interface CommitInfo {
  sha: string;
  message: string;
  date: string;
}

export async function photoCommits(gh: Gh, n = 8): Promise<CommitInfo[]> {
  const cs = await call<{ sha: string; commit: { message: string; committer: { date: string } } }[]>(
    gh, "GET", `/commits?sha=main&path=proxy/photos&per_page=${n}`,
  );
  return cs.map((c) => ({ sha: c.sha, message: c.commit.message, date: c.commit.committer.date }));
}

/** Commits that changed one path, newest first. */
export async function commitsForPath(gh: Gh, path: string, n = 30): Promise<string[]> {
  const cs = await call<{ sha: string }[]>(gh, "GET", `/commits?sha=main&path=${path}&per_page=${n}`);
  return cs.map((c) => c.sha);
}

export interface CheckRun {
  conclusion: string | null;
  status: string;
  title: string;
  text: string;
  completedAt: string | null;
}

// The verdict a given RUN reported. Two runs can share a commit (a failed push
// run, then a Retry dispatched while main has not moved), so the check run is
// matched to the run by time -- it must complete inside that run's window --
// never taken as "the first one on the commit".
export async function publishCheck(gh: Gh, sha: string, runStart: string, runEnd: string): Promise<CheckRun | null> {
  const r = await call<{ check_runs: { conclusion: string | null; status: string; completed_at: string | null; output: { title: string | null; text: string | null } }[] }>(
    gh, "GET", `/commits/${sha}/check-runs?check_name=photos-publish&filter=all`,
  );
  const lo = Date.parse(runStart), hi = Date.parse(runEnd) + 120_000;
  const c = r.check_runs.find((x) => x.completed_at && Date.parse(x.completed_at) >= lo && Date.parse(x.completed_at) <= hi);
  return c ? { conclusion: c.conclusion, status: c.status, title: c.output.title ?? "", text: c.output.text ?? "", completedAt: c.completed_at } : null;
}

export interface Run {
  id: number;
  sha: string;
  event: string; // push | workflow_dispatch
  status: string; // queued | in_progress | completed
  conclusion: string | null; // success | failure | cancelled | ...
  url: string;
  createdAt: string;
  updatedAt: string;
}

/** The photos workflow's most recent runs, any trigger, newest first. */
export async function recentPhotoRuns(gh: Gh, n = 8): Promise<Run[]> {
  const r = await call<{ workflow_runs: { id: number; head_sha: string; event: string; status: string; conclusion: string | null; html_url: string; created_at: string; updated_at: string }[] }>(
    gh, "GET", `/actions/workflows/photos.yml/runs?branch=main&per_page=${n}`,
  );
  return r.workflow_runs.map((w) => ({
    id: w.id, sha: w.head_sha, event: w.event, status: w.status, conclusion: w.conclusion,
    url: w.html_url, createdAt: w.created_at, updatedAt: w.updated_at,
  }));
}

// RETRY IS A FRESH RUN OF MAIN, not a re-run. A re-run replays the old run's
// commit and inputs: an old commit is refused as stale (correctly), and a run
// that failed on a planted test failure would plant it again. A new dispatch
// ingests main's whole manifest, which is exactly what converging needs.
export async function dispatchPhotos(gh: Gh): Promise<void> {
  await call(gh, "POST", "/actions/workflows/photos.yml/dispatches", { ref: "main" });
}
