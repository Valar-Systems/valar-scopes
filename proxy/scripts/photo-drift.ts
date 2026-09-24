/**
 * photo-drift.ts -- what a render-drift run's two numbers MEAN.
 *
 * Pure (no Node, no network): test/photo-drift.test.ts drives it, and
 * scripts/drift-report.ts prints it in the job log, the check run and the
 * dashboard -- so the rule is where the reader already is, not only in a README.
 *
 * TWO HALVES, TWO NUMBERS, never summed:
 *
 *   (a) render vs published manifest -- `ingest-photos --dry-run --env
 *       production`: rows whose freshly rendered keys differ from what
 *       photo:manifest says is live. Blob keys are content hashes, so this is a
 *       byte comparison of every render.
 *   (b) published manifest vs live pointers -- verify-photos: rows whose
 *       pointers, or the blobs they name, disagree with photo:manifest.
 *
 * They fail for different reasons and are fixed in different places, so one
 * combined number would answer neither question.
 *
 * AN UNREADABLE HALF IS NOT A ZERO. A half that could not read KV has no count,
 * and the run is FAILED -- never "0 drift". A dead token that printed 0 would be
 * the most reassuring possible failure.
 */

import { pointerKey, type ManifestEntry } from "../src/photolicense";

/** Half (a): what `ingest-photos --dry-run --env <env> --status-out` writes. */
export interface DryRunStatus {
  mode: "dry-run";
  verdict: "READ" | "UNREADABLE";
  reason: string;
  total: number;
  changed: number;
  changedRows: string[];
  publishedNotInRepo: string[];
  finishedAt: string;
}

/** Half (b): what `verify-photos --status-out` writes. */
export interface VerifyStatus {
  mode: "verify";
  verdict: "PASS" | "FAIL" | "UNTRUSTWORTHY" | "UNREADABLE";
  reason: string;
  rows: number;
  pointersChecked: number;
  rowsDisagreeing: string[];
  finishedAt: string;
}

export interface Half {
  label: string;
  /** null when the half could not be read -- a failure, never a zero. */
  count: number | null;
  rows: string[];
  meaning: string;
}

export interface DriftReport {
  v: 1;
  verdict: "CLEAN" | "DRIFT" | "FAILED";
  a: Half;
  b: Half;
  finishedAt: string;
}

export const LABEL_A = "(a) render vs published manifest";
export const LABEL_B = "(b) published manifest vs live pointers";

/**
 * "A few": up to this many rows. Above it and short of ALL is OUTSIDE THE RULE
 * on purpose -- neither "some photos changed" nor "the renderer changed" is a
 * safe assumption there, and a rule with no answer for a result is how an answer
 * gets improvised.
 */
export const FEW_MAX = 10;

// The rule for half (a), pre-registered: every outcome has a meaning before any
// run is looked at, including the one that fits none of them.
export function interpretA(s: DryRunStatus | null): Half {
  if (!s || s.verdict !== "READ") {
    return {
      label: LABEL_A, count: null, rows: [],
      meaning: `FAILED: could not compare -- ${s?.reason || "the dry run did not report"}. ` +
        "This is not a count; an unreadable published manifest would otherwise look like every row CHANGED.",
    };
  }
  const extra = s.publishedNotInRepo.length
    ? ` Also published but no longer in the repo: ${s.publishedNotInRepo.join(", ")} -- a publish of main would refuse to drop them.`
    : "";
  const count = s.changed + s.publishedNotInRepo.length;
  if (count === 0) return { label: LABEL_A, count, rows: [], meaning: `clean: all ${s.total} rows render to exactly what is published.` };
  const rows = [...s.changedRows, ...s.publishedNotInRepo];
  if (s.changed === s.total && s.total > 0) {
    return {
      label: LABEL_A, count, rows,
      meaning: `ALL ${s.total} rows changed: the RENDERER or sharp/libvips changed, not the photos. ` +
        "Check package-lock.json (sharp, @img/sharp-*) and scripts/photo-render.ts first. Do NOT publish to 'fix' it." + extra,
    };
  }
  if (s.changed <= FEW_MAX) {
    return {
      label: LABEL_A, count, rows,
      meaning: `${s.changed} row(s) differ from what devices are served: ${s.changedRows.join(", ") || "none"}. ` +
        "Usually photo commits on main that are not live yet -- a publish in flight, or one that failed (see its photos-publish check). " +
        "If no publish explains them, the render of those rows changed." + extra,
    };
  }
  return {
    label: LABEL_A, count, rows,
    meaning: `OUTSIDE THE RULE: ${s.changed} of ${s.total} changed -- more than a few, fewer than all. ` +
      "Neither 'these photos changed' nor 'the renderer changed' is safe to assume. Read the rows before acting." + extra,
  };
}

export function interpretB(s: VerifyStatus | null): Half {
  if (!s || s.verdict === "UNTRUSTWORTHY" || s.verdict === "UNREADABLE") {
    return {
      label: LABEL_B, count: null, rows: [],
      meaning: `FAILED: could not verify -- ${s?.reason || "the verifier did not report"}. This is not a count.`,
    };
  }
  const n = s.rowsDisagreeing.length;
  if (n === 0) {
    return { label: LABEL_B, count: 0, rows: [], meaning: `clean: all ${s.pointersChecked} pointers and the blobs they name match the published manifest.` };
  }
  return {
    label: LABEL_B, count: n, rows: s.rowsDisagreeing,
    meaning: `${n} row(s) are served keys the published manifest does not name: ${s.rowsDisagreeing.join(", ")}. ` +
      "A partial write, or KV edited outside the pipeline. Re-publishing main (Retry on the dashboard) converges it.",
  };
}

// Pointer and blob keys back to the rows that own them, so the report names ROWS.
export function rowsFor(manifest: ManifestEntry[], keys: string[]): string[] {
  const owner = new Map<string, string>();
  for (const e of manifest) {
    const id = `${e.kind}:${e.target}`;
    owner.set(pointerKey(e.kind, e.target), id);
    if (e.blobKey) owner.set(e.blobKey, id);
    for (const [size, key] of Object.entries(e.squareKeys ?? {})) {
      owner.set(pointerKey(e.kind, e.target, Number(size)), id);
      owner.set(key, id);
    }
  }
  return [...new Set(keys.map((k) => owner.get(k) ?? k))];
}

export function combine(a: DryRunStatus | null, b: VerifyStatus | null, now = new Date().toISOString()): DriftReport {
  const ha = interpretA(a), hb = interpretB(b);
  const verdict = ha.count === null || hb.count === null ? "FAILED" : ha.count + hb.count > 0 ? "DRIFT" : "CLEAN";
  return { v: 1, verdict, a: ha, b: hb, finishedAt: now };
}

/** 0 CLEAN, 1 DRIFT, 3 FAILED -- the same 3 the ingest uses for "cannot see". */
export const exitCodeFor = (r: DriftReport) => (r.verdict === "CLEAN" ? 0 : r.verdict === "DRIFT" ? 1 : 3);

const shown = (h: Half) => (h.count === null ? "FAILED" : String(h.count));

export function titleFor(r: DriftReport): string {
  return `${r.verdict}: (a) ${shown(r.a)} · (b) ${shown(r.b)}`;
}

export function formatReport(r: DriftReport): string {
  return [
    `render drift: ${r.verdict}`,
    `  ${r.a.label}: ${shown(r.a)}`,
    `      ${r.a.meaning}`,
    `  ${r.b.label}: ${shown(r.b)}`,
    `      ${r.b.meaning}`,
    "",
    "  how to read (a): 0 = clean. A few = the rows are listed; usually unpublished commits.",
    "                   ALL = the renderer or sharp changed -- check package-lock.json first.",
    "                   Anything else, or FAILED, is not covered: stop and look.",
  ].join("\n");
}

/** What the dashboard's drift panel shows for the latest drift run. */
export interface DriftPanel {
  state: "NONE" | "RUNNING" | "CLEAN" | "DRIFT" | "FAILED";
  /** null = not a count (unreadable, or the run never reported). Never shown as 0. */
  a: number | null;
  b: number | null;
  at: string;
  meaningA: string;
  meaningB: string;
}

// A COMPLETED RUN WITH NO REPORT IS FAILED, NOT CLEAN. The job can die before
// its report step (a runner fault, npm ci); showing that as "0 · 0" would be
// the reassuring failure. Only a posted report can produce a count.
export function driftPanel(
  run: { status: string; conclusion: string | null; updatedAt: string } | null,
  checkText: string | null,
): DriftPanel {
  const none = { a: null, b: null, meaningA: "", meaningB: "" };
  if (!run) return { state: "NONE", at: "", ...none };
  let r: DriftReport | null = null;
  try { r = checkText ? (JSON.parse(checkText) as DriftReport) : null; } catch { r = null; }
  if (r && (r.verdict === "CLEAN" || r.verdict === "DRIFT" || r.verdict === "FAILED")) {
    return {
      state: r.verdict, a: r.a?.count ?? null, b: r.b?.count ?? null, at: r.finishedAt || run.updatedAt,
      meaningA: r.a?.meaning ?? "", meaningB: r.b?.meaning ?? "",
    };
  }
  if (run.status !== "completed") return { state: "RUNNING", at: run.updatedAt, ...none };
  return {
    state: "FAILED", at: run.updatedAt, ...none,
    meaningA: `the drift run ended (${run.conclusion ?? "no conclusion"}) without posting a report -- nothing was measured`,
  };
}

// ---------------------------------------------------------------- freshness

/** A drift run older than this is stale: the daily job has missed at least one run. */
export const DRIFT_STALE_HOURS = 26;

export interface DriftFreshness {
  level: "ok" | "amber" | "red";
  ageHours: number | null; // null when there has never been a run
  label: string; // e.g. "last run 5.4 h ago"
}

// RED if the job failed; AMBER if the last run is over DRIFT_STALE_HOURS old;
// otherwise ok. Red wins: a failed job that is also old is a failed job.
//
// 26 h and not 24: the job is DAILY and GitHub runs scheduled workflows late --
// the 06:17Z run on 2026-09-24 started at 11:44Z -- so a 24 h threshold would go
// amber on an ordinary late start. 26 h leaves room for that and still catches a
// day with no run at all.
export function driftFreshness(state: string, at: string, nowMs: number): DriftFreshness {
  const t = at ? Date.parse(at) : NaN;
  const ageHours = Number.isFinite(t) ? Math.max(0, Math.round(((nowMs - t) / 3600000) * 10) / 10) : null;
  const label = ageHours === null ? "no run yet" : `last run ${ageHours} h ago`;
  if (state === "FAILED") return { level: "red", ageHours, label };
  if (ageHours !== null && ageHours > DRIFT_STALE_HOURS) return { level: "amber", ageHours, label };
  return { level: "ok", ageHours, label };
}
