/**
 * verify-photos.ts -- half (b) of the render-drift check: does what devices are
 * served match the manifest that says what is live?
 *
 *   npx tsx scripts/verify-photos.ts --env production --status-out b.json
 *
 * READ-ONLY. Every pointer the published photo:manifest names must hold exactly
 * the manifest's key, and every blob those pointers name must exist -- the same
 * verify() the publish runs after its writes (publish-guard.ts), reading KV over
 * REST (kv-rest.ts, which has no put). Token: PHOTO_KV_READ_TOKEN, else
 * CLOUDFLARE_API_TOKEN; never printed.
 *
 * Exit 0 PASS, 1 FAIL (rows disagree -- listed), 3 cannot see KV or read the
 * manifest. Anchor controls first: a dead token must come back 3, never as "every
 * pointer is missing".
 */
import { writeFileSync } from "node:fs";
import { bulkGet, getValue, kvTargetFromWranglerToml, listKeys, readControls, readToken } from "./kv-rest";
import { expectedPointers, verify } from "./publish-guard";
import { rowsFor, type VerifyStatus } from "./photo-drift";
import { MANIFEST_KEY, type ManifestEntry } from "../src/photolicense";

function arg(name: string): string | undefined {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : undefined;
}

async function main(): Promise<void> {
  const env = arg("--env");
  const out = arg("--status-out");
  if (!env) throw new Error("usage: verify-photos --env <name> [--status-out <file>]");
  const status: VerifyStatus & { v: 1; env: string } = {
    v: 1, mode: "verify", env, verdict: "UNREADABLE", reason: "", rows: 0, pointersChecked: 0, rowsDisagreeing: [], finishedAt: "",
  };
  const done = (code: number) => {
    status.finishedAt = new Date().toISOString();
    const line = `verify ${env}: ${status.verdict}` + (status.reason ? ` -- ${status.reason}` : "") +
      (status.verdict === "PASS" || status.verdict === "FAIL"
        ? ` | ${status.rowsDisagreeing.length} of ${status.rows} rows disagree, ${status.pointersChecked} pointers checked`
        : "");
    (code === 0 ? console.log : console.error)(line);
    if (out) writeFileSync(out, JSON.stringify(status, null, 2) + "\n");
    process.exitCode = code;
  };

  const target = kvTargetFromWranglerToml("wrangler.toml", env, readToken());
  const controls = await readControls(target, MANIFEST_KEY);
  console.log(`control: ${MANIFEST_KEY} reads back=${controls.knownPresentReadsBack}  absent key reads as null=${controls.knownAbsentReadsNull}`);
  if (!controls.knownPresentReadsBack || !controls.knownAbsentReadsNull) {
    status.verdict = "UNTRUSTWORTHY";
    status.reason = `the verifier cannot tell present from absent in ${env}` + (controls.error ? ` (${controls.error.slice(0, 200)})` : "");
    return done(3);
  }

  let manifest: ManifestEntry[];
  try {
    const parsed: unknown = JSON.parse((await getValue(target, MANIFEST_KEY)) ?? "null");
    if (!Array.isArray(parsed)) throw new Error(`${MANIFEST_KEY} is not a JSON array`);
    manifest = parsed as ManifestEntry[];
  } catch (err) {
    status.reason = `could not read ${MANIFEST_KEY}: ${String(err instanceof Error ? err.message : err).slice(0, 200)}`;
    return done(3);
  }
  status.rows = manifest.length;

  let pointers: Record<string, string | null>;
  let blobs: Set<string>;
  try {
    pointers = await bulkGet(target, expectedPointers(manifest).map(([k]) => k));
    blobs = await listKeys(target, "photo:");
  } catch (err) {
    status.verdict = "UNTRUSTWORTHY";
    status.reason = `KV read failed mid-verify: ${String(err instanceof Error ? err.message : err).slice(0, 200)}`;
    return done(3);
  }
  const res = verify({ controls, manifest, pointers, blobsPresent: blobs });
  status.verdict = res.verdict;
  status.pointersChecked = res.pointersChecked;
  status.rowsDisagreeing = rowsFor(manifest, [...res.pointerMismatches, ...res.missingBlobs, ...res.rowsMissingKeys]);
  status.reason = res.reasons.join(" ").slice(0, 500);
  done(res.verdict === "PASS" ? 0 : res.verdict === "FAIL" ? 1 : 3);
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exitCode = 3;
});
