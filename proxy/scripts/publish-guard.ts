/**
 * publish-guard.ts -- what the photo ingest may write, and when it may publish.
 *
 * Pure: no Node, no network. ingest-photos.ts feeds it what it read from KV and
 * acts on the verdict, so every refusal here is unit-tested with planted inputs
 * (test/publish-guard.test.ts) rather than trusted because the code reads right.
 *
 * THE ORDER IS THE DESIGN (docs: "Photo publish pipeline: design"):
 *   1. preflight   -- can the verifier see KV at all? A blind instrument says
 *                     UNTRUSTWORTHY and NOTHING is written. "Everything missing"
 *                     is the most likely shape of a broken probe, not a verdict.
 *   2. writes      -- changed rows only, blob before pointer (the ingest).
 *   3. verify      -- every row, every key, read back.
 *   4. manifest    -- written only on PASS. It is the record of what is live;
 *                     writing it after a failed verify would make it a lie.
 */
import type { ManifestEntry } from "../src/photolicense";
import { pointerKey } from "../src/photolicense";

// ---------------------------------------------------------------- allowlist

// Both KV namespaces on the account hold EVERY key family (rt:, ap:, cfg:,
// enr:dev:, ...), and a Cloudflare token cannot be scoped below a namespace. So
// this list, enforced before wrangler is called, is the only thing that keeps
// the photo job off route and device data. The manifest key is photo:manifest,
// inside the photo: family.
export const ALLOWED_KEY_PREFIXES = ["photo:", "pptr:"] as const;

export class KeyNotAllowed extends Error {}

export function assertAllowedKey(key: string): void {
  if (!ALLOWED_KEY_PREFIXES.some((p) => key.startsWith(p))) {
    throw new KeyNotAllowed(
      `refusing to write "${key}": the photo ingest may write only ${ALLOWED_KEY_PREFIXES.join(" and ")} keys`,
    );
  }
}

// ---------------------------------------------------------------- expectations

export const EXPECTED_SQUARES = ["240", "412", "480"] as const;

/** Every pointer the manifest says devices should resolve, and the key it must hold. */
export function expectedPointers(manifest: ManifestEntry[]): [string, string][] {
  const out: [string, string][] = [];
  for (const e of manifest) {
    if (e.blobKey) out.push([pointerKey(e.kind, e.target), e.blobKey]);
    for (const [size, key] of Object.entries(e.squareKeys ?? {})) {
      out.push([pointerKey(e.kind, e.target, Number(size)), key]);
    }
  }
  return out;
}

const rowId = (e: { kind: string; target: string }) => `${e.kind}:${e.target}`;

// ---------------------------------------------------------------- preflight

export type Verdict = "PASS" | "FAIL" | "UNTRUSTWORTHY";

export interface Controls {
  /** A key known to exist read back as a non-empty value. */
  knownPresentReadsBack: boolean;
  /** A key known NOT to exist read back as null -- not an error, not a value. */
  knownAbsentReadsNull: boolean;
}

export interface PreflightInput {
  controls: Controls;
  /** Rows of the manifest currently published, or null when none is published yet. */
  published: { kind: string; target: string }[] | null;
  /** Rows the ingest is about to publish. */
  next: { kind: string; target: string }[];
}

export interface Decision {
  verdict: Verdict;
  reasons: string[];
}

// Before ANY write. A blind verifier refuses (it cannot certify what follows),
// and a manifest that would lose rows refuses (a deliberate removal is not what
// a photo publish does -- see the design's retire note).
export function preflight(p: PreflightInput): Decision {
  if (!p.controls.knownPresentReadsBack || !p.controls.knownAbsentReadsNull) {
    return {
      verdict: "UNTRUSTWORTHY",
      reasons: [
        "the verifier cannot see KV: " +
          (!p.controls.knownPresentReadsBack ? "a key known to exist did not read back" : "") +
          (!p.controls.knownPresentReadsBack && !p.controls.knownAbsentReadsNull ? "; " : "") +
          (!p.controls.knownAbsentReadsNull ? "a key known to be absent did not read as null" : "") +
          ". Nothing was written.",
      ],
    };
  }
  if (p.published) {
    const nextIds = new Set(p.next.map(rowId));
    const lost = p.published.map(rowId).filter((id) => !nextIds.has(id));
    if (lost.length) {
      return {
        verdict: "FAIL",
        reasons: [
          `the manifest would drop ${lost.length} row(s) (${p.published.length} -> ${p.next.length}): ` +
            `${lost.join(", ")}. Nothing was written.`,
        ],
      };
    }
  }
  return { verdict: "PASS", reasons: [] };
}

// ---------------------------------------------------------------- verify

export interface VerifyInput {
  controls: Controls;
  manifest: ManifestEntry[];
  /** pointer key -> value read back (null = absent). */
  pointers: Record<string, string | null>;
  /** Every blob key present in the namespace. */
  blobsPresent: Set<string>;
}

export interface VerifyResult extends Decision {
  pointersChecked: number;
  pointerMismatches: string[];
  missingBlobs: string[];
  rowsMissingKeys: string[];
}

export function verify(v: VerifyInput): VerifyResult {
  const base = { pointersChecked: 0, pointerMismatches: [], missingBlobs: [], rowsMissingKeys: [] };
  if (!v.controls.knownPresentReadsBack || !v.controls.knownAbsentReadsNull) {
    return {
      ...base,
      verdict: "UNTRUSTWORTHY",
      reasons: ["the verifier lost sight of KV after the writes; the manifest was not written"],
    };
  }
  const rowsMissingKeys = v.manifest
    .filter((e) => !e.blobKey || EXPECTED_SQUARES.some((s) => !(e.squareKeys ?? {})[s]))
    .map(rowId);
  const expect = expectedPointers(v.manifest);
  const pointerMismatches = expect.filter(([k, want]) => v.pointers[k] !== want).map(([k]) => k);
  const named = [...new Set(expect.map(([, key]) => key))];
  const missingBlobs = named.filter((b) => !v.blobsPresent.has(b));
  const reasons: string[] = [];
  if (rowsMissingKeys.length) reasons.push(`rows without a blob and 3 squares: ${rowsMissingKeys.join(", ")}`);
  if (pointerMismatches.length) reasons.push(`pointers wrong or missing: ${pointerMismatches.slice(0, 20).join(", ")}`);
  if (missingBlobs.length) reasons.push(`blobs missing: ${missingBlobs.slice(0, 20).join(", ")}`);
  return {
    verdict: reasons.length ? "FAIL" : "PASS",
    reasons,
    pointersChecked: expect.length,
    pointerMismatches,
    missingBlobs,
    rowsMissingKeys,
  };
}
