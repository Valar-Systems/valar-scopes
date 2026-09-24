import type { Env } from "./types";

// Render drift, read from THE SAME SOURCE the photo dashboard reads
// (proxy/scripts/photo-dashboard.ts driftStatus): the latest run of the
// photo-drift workflow on main, and the `photo-drift` check run it posted, whose
// text is the JSON report. Same rule as proxy/scripts/photo-drift.ts driftPanel:
// only a posted report can produce a verdict -- a completed run that posted none
// is FAILED, never CLEAN.
//
// The repo is public, so these GitHub reads need no token; GITHUB_TOKEN is used
// if set (rate limits), and a read that fails is UNREADABLE -- shown, never
// folded into "nothing".

export type DriftState = "CLEAN" | "DRIFT" | "FAILED" | "RUNNING" | "NONE" | "UNREADABLE";
export interface DriftStatus { state: DriftState; detail: string; at: string }

interface Run { status: string; conclusion: string | null; head_sha: string; created_at: string; updated_at: string }
interface Check { completed_at: string | null; output: { text: string | null } }

// PURE: the verdict from the latest run and (if any) the check-run text.
export function driftState(run: Run | null, checkText: string | null): DriftStatus {
  if (!run) return { state: "NONE", detail: "no photo-drift run yet", at: "" };
  let r: { verdict?: string; a?: { count?: number | null }; b?: { count?: number | null }; finishedAt?: string } | null = null;
  try { r = checkText ? JSON.parse(checkText) : null; } catch { r = null; }
  if (r && (r.verdict === "CLEAN" || r.verdict === "DRIFT" || r.verdict === "FAILED")) {
    const n = (v: number | null | undefined) => (v === null || v === undefined ? "FAILED" : String(v));
    return { state: r.verdict, detail: `(a) ${n(r.a?.count)} · (b) ${n(r.b?.count)}`, at: r.finishedAt || run.updated_at };
  }
  if (run.status !== "completed") return { state: "RUNNING", detail: "a drift run is in progress", at: run.updated_at };
  return { state: "FAILED", detail: `the run ended (${run.conclusion ?? "no conclusion"}) without posting a report`, at: run.updated_at };
}

export async function readDrift(env: Env): Promise<DriftStatus> {
  const repo = env.GITHUB_REPO ?? "Valar-Systems/valar-scopes";
  if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo)) return { state: "UNREADABLE", detail: "bad GITHUB_REPO", at: "" };
  const headers: Record<string, string> = { Accept: "application/vnd.github+json", "User-Agent": "blipscope-fleet-dashboard" };
  if (env.GITHUB_TOKEN) headers.Authorization = `Bearer ${env.GITHUB_TOKEN}`;
  try {
    const rr = await fetch(`https://api.github.com/repos/${repo}/actions/workflows/photo-drift.yml/runs?branch=main&per_page=1`, { headers });
    if (!rr.ok) return { state: "UNREADABLE", detail: `GitHub runs read failed (${rr.status})`, at: "" };
    const run = ((await rr.json()) as { workflow_runs?: Run[] }).workflow_runs?.[0] ?? null;
    if (!run) return driftState(null, null);
    const cr = await fetch(`https://api.github.com/repos/${repo}/commits/${run.head_sha}/check-runs?check_name=photo-drift&filter=all`, { headers });
    if (!cr.ok) return { state: "UNREADABLE", detail: `GitHub check-run read failed (${cr.status})`, at: "" };
    // The check run THIS run posted: completed inside the run's window (two runs
    // can share a commit), exactly as the photo dashboard matches it.
    const lo = Date.parse(run.created_at), hi = Date.parse(run.status === "completed" ? run.updated_at : new Date().toISOString()) + 120_000;
    const check = ((await cr.json()) as { check_runs?: Check[] }).check_runs?.find(
      (c) => c.completed_at && Date.parse(c.completed_at) >= lo && Date.parse(c.completed_at) <= hi,
    );
    return driftState(run, check?.output.text ?? null);
  } catch (err) {
    return { state: "UNREADABLE", detail: String(err instanceof Error ? err.message : err).slice(0, 160), at: "" };
  }
}

// ---------------------------------------------------------------- freshness

// The SAME rule as proxy/scripts/photo-drift.ts driftFreshness (the photo
// dashboard's panel). This Worker cannot import from proxy/scripts, so it is a
// copy, and test/phase1b.test.ts pins the threshold and the order: RED if the job
// failed (or could not be read), AMBER if the last run is over 26 h old, else ok.
// 26 h, not 24: GitHub runs the daily schedule late (06:17Z started 11:44Z on
// 2026-09-24), so 24 h would go amber on an ordinary late start.
export const DRIFT_STALE_HOURS = 26;

export interface DriftFreshness { level: "ok" | "amber" | "red"; ageHours: number | null; label: string }

export function driftFreshness(s: DriftStatus, nowMs: number): DriftFreshness {
  const t = s.at ? Date.parse(s.at) : NaN;
  const ageHours = Number.isFinite(t) ? Math.max(0, Math.round(((nowMs - t) / 3600000) * 10) / 10) : null;
  const label = ageHours === null ? "no run yet" : `last run ${ageHours} h ago`;
  if (s.state === "FAILED" || s.state === "UNREADABLE") return { level: "red", ageHours, label };
  if (ageHours !== null && ageHours > DRIFT_STALE_HOURS) return { level: "amber", ageHours, label };
  return { level: "ok", ageHours, label };
}
