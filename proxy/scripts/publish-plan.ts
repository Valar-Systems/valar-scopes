/**
 * publish-plan.ts -- what a dashboard Publish is allowed to commit to main.
 *
 * Pure: no Node, no network (test/publish-plan.test.ts). The dashboard commits
 * straight to main, so these are the checks between a click and the fleet:
 *
 *   - every path in the commit is under proxy/photos/           (A15)
 *   - every manifest row passes shape + the licence gate         (A17)
 *   - local edits are re-applied onto main AS IT IS NOW, and a row that
 *     somebody else changed since this dashboard loaded it is a conflict,
 *     named, never silently overwritten                          (A5, A6)
 *   - a publish never removes a row (the CI preflight also refuses that)
 */
import { validateEntry, type ManifestEntry } from "../src/photolicense";

export const PHOTOS_PREFIX = "proxy/photos/";

export class PathOutsidePhotos extends Error {}

export function assertPhotoPaths(paths: string[]): void {
  const bad = paths.filter((p) => !p.startsWith(PHOTOS_PREFIX) || p.includes("..") || p.includes("\\"));
  if (bad.length) {
    throw new PathOutsidePhotos(
      `refusing to commit outside ${PHOTOS_PREFIX}: ${bad.join(", ")}. The photo dashboard may only change photos.`,
    );
  }
}

export interface RowProblem {
  target: string;
  errors: string[];
}

// Shape first (a row the licence gate cannot even read is still named), then
// the SAME validateEntry the ingest runs -- never a second implementation.
export function validateRows(rows: unknown): RowProblem[] {
  if (!Array.isArray(rows)) return [{ target: "(manifest)", errors: ["manifest is not a JSON array"] }];
  const out: RowProblem[] = [];
  const seen = new Set<string>();
  rows.forEach((r, i) => {
    const e = r as Partial<ManifestEntry>;
    const target = typeof e?.target === "string" && e.target ? e.target : `(row ${i + 1})`;
    const errors: string[] = [];
    if (!r || typeof r !== "object") errors.push("row is not an object");
    else {
      if (typeof e.target !== "string" || !/^[A-Za-z0-9~]+$/.test(e.target)) errors.push("target missing or not alphanumeric");
      if (e.kind !== "type" && e.kind !== "hex") errors.push(`kind must be "type" or "hex"`);
      if (typeof e.file !== "string" || !/^src\/[a-z0-9~_.-]+\.jpg$/.test(e.file)) errors.push("file must be src/<name>.jpg");
      const id = `${e.kind}:${e.target}`;
      if (seen.has(id)) errors.push("duplicate row");
      seen.add(id);
      if (!errors.length) {
        const v = validateEntry(e as ManifestEntry);
        if (!v.ok) errors.push(...v.errors);
      }
    }
    if (errors.length) out.push({ target, errors });
  });
  return out;
}

const id = (e: { kind: string; target: string }) => `${e.kind}:${e.target}`;
const same = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

export interface Plan {
  /** main's manifest as it is now, with this dashboard's edits applied. */
  merged: ManifestEntry[];
  /** Targets this publish changes or adds. */
  changed: string[];
  /** Targets someone else changed on main since `base` -- refused, named. */
  conflicts: string[];
  /** Targets the local copy dropped -- refused, named. */
  removed: string[];
}

// `base` is main's manifest as this dashboard last saw it; `local` is what the
// operator has now; `current` is main's manifest right now. A row counts as an
// edit only if local differs from base -- so a stale base never turns someone
// else's newer row into "our" change.
export function planPublish(base: ManifestEntry[], current: ManifestEntry[], local: ManifestEntry[]): Plan {
  const B = new Map(base.map((e) => [id(e), e]));
  const C = new Map(current.map((e) => [id(e), e]));
  const L = new Map(local.map((e) => [id(e), e]));
  const changed: string[] = [];
  const conflicts: string[] = [];
  const removed = base.filter((e) => !L.has(id(e))).map((e) => e.target);
  const merged = current.map((e) => ({ ...e }));
  for (const [k, row] of L) {
    const b = B.get(k);
    if (b && same(b, row)) continue; // not edited here
    const c = C.get(k);
    if (c && same(c, row)) continue; // main already holds exactly this row
    // Edited here, and main's row is no longer the one this edit started from:
    // changed or removed by someone else since (b && c differ, or b && !c), or
    // "added" here while main gained a different row of the same id (!b && c).
    const mainMoved = b ? !c || !same(b, c) : !!c;
    if (mainMoved) {
      conflicts.push(row.target);
      continue;
    }
    changed.push(row.target);
    const at = merged.findIndex((e) => id(e) === k);
    if (at >= 0) merged[at] = { ...row };
    else merged.push({ ...row });
  }
  return { merged, changed, conflicts, removed };
}
