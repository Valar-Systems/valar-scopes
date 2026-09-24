/**
 * ingest-photos.ts -- the stock-photo upload/ingest tool.
 *
 * Reads photos/manifest.json, enforces the per-layer license gate (the SAME
 * validateEntry the tests cover -- imported, never re-implemented), resizes each
 * source image to the exact device sprite dims as baseline JPEG with EXIF
 * stripped, writes content-addressed immutable blobs to KV, flips the per-hex /
 * per-type pointer, publishes the public manifest, and regenerates credits.html.
 *
 *   npm run ingest -- --check                 # validate the manifest only (no KV, no sharp)
 *   npm run ingest -- --env staging           # resize + upload to staging KV, flip pointers
 *   npm run ingest -- --env staging --dry-run # do everything but the KV writes
 *
 * The license gate is atomic: if ANY row fails validation the run aborts before
 * a single upload. Blobs are never mutated in place -- a re-upload lands on a new
 * hash8 key and the pointer flips to it (old blobs are harmless orphans).
 *
 * Requires `tsx` (dev dep, runs .ts directly) and, for actual uploads, `sharp`
 * (lazily imported) and an authenticated `wrangler`.
 */
import { execSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { execWithRetry, sleepSync } from "./exec-retry";
import { bulkGet, getValue, kvTargetFromWranglerToml, listKeys, readControls, readToken } from "./kv-rest";
import { renderRect, renderSquares } from "./photo-render";
import { KeyNotAllowed, assertAllowedKey, expectedPointers, preflight, verify } from "./publish-guard";
import {
  MANIFEST_KEY,
  deriveBlobKey,
  pointerKey,
  renderCreditsHtml,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";

interface Args {
  env?: string;
  dryRun: boolean;
  checkOnly: boolean;
  photosDir: string;
  square: boolean;
  force: boolean;
  /** --dry-run only: write the first N changed rows' squares here to be looked at. */
  sampleOut?: string;
  sampleCount: number;
  /** Write a machine-readable status (verdict, rows live/failed/not reached) here on every exit. */
  statusOut?: string;
  /** Prove the allowlist refuses <key> before wrangler is reached; writes nothing. */
  selftestAllowlist?: string;
}

function parseArgs(argv: string[]): Args {
  const a: Args = {
    dryRun: false, checkOnly: false, photosDir: "photos", square: true, force: false, sampleCount: 3,
  };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--env") a.env = argv[++i];
    else if (v === "--dry-run") a.dryRun = true;
    else if (v === "--check") a.checkOnly = true;
    // Default ON. The square variants are what FW >= 7 draws, so a run that
    // quietly skipped them would leave new firmware falling back to rectangles
    // with nothing reporting it -- the flag exists to turn them OFF for a fast
    // legacy-only re-run, not to opt in to the current product.
    else if (v === "--no-square") a.square = false;
    // --sample-out <dir> [--sample-count N]: on a dry run, write out the squares
    // the real run WOULD publish. Looking at three of them costs nothing and is
    // the only check that sees what a customer sees; every other assertion here
    // is about bytes, keys and counts, none of which can tell you the framing is
    // wrong. Deliberately dry-run only -- an upload run's output is the artifact.
    else if (v === "--sample-out") a.sampleOut = argv[++i];
    else if (v === "--sample-count") a.sampleCount = Number(argv[++i]) || 3;
    // Re-upload every row even when KV already holds the identical key. The skip
    // is a claim about what is in KV, inferred from the published manifest; this
    // is how you act when you suspect that claim is wrong (a partial run, a
    // hand-edited key, a namespace restored from elsewhere).
    else if (v === "--force") a.force = true;
    else if (v === "--photos-dir") a.photosDir = argv[++i] ?? a.photosDir;
    else if (v === "--status-out") a.statusOut = argv[++i];
    else if (v === "--selftest-allowlist") a.selftestAllowlist = argv[++i];
    else throw new Error(`unknown argument: ${v}`);
  }
  if (a.selftestAllowlist) return a;
  if (!a.checkOnly && !a.dryRun && !a.env) {
    throw new Error("an upload run needs --env <name> (or use --check / --dry-run)");
  }
  return a;
}

// Shell-quote one argument. Windows Node won't spawn the npx/wrangler .cmd shims
// via execFileSync (EINVAL), so we build a quoted command string and run it
// through the shell with execSync instead -- quoting keeps paths-with-spaces and
// the colon in KV keys intact.
function q(s: string): string {
  return `"${s.replace(/"/g, '\\"')}"`;
}

// A single KV write occasionally fails transiently (a rate-limit 429, a network
// blip) partway through the ~136 writes a full ingest makes -- and without a
// retry that aborts the whole idempotent run. Retry with backoff and a NATIVE
// wait; see exec-retry.ts for why the wait used to be the thing that failed.
//
// THE ALLOWLIST RUNS FIRST, before the command is even built: the photo job may
// write only photo:* and pptr:* (publish-guard.ts), because both namespaces on
// the account also hold route, airport, config and device data.
//
// `exec` is injectable so the allowlist self-test can prove wrangler was never
// reached, and so a planted failure (PHOTO_INGEST_PLANT_FAIL) goes through the
// real retry path.
type Exec = (cmd: string) => void;
const realExec: Exec = (c) => { execSync(c, { stdio: "inherit" }); };
function wranglerPut(env: string, key: string, opts: { value?: string; path?: string }, exec: Exec = realExec): void {
  assertAllowedKey(key);
  const parts = ["npx", "wrangler", "kv", "key", "put", q(key)];
  if (opts.value !== undefined) parts.push(q(opts.value));
  if (opts.path !== undefined) parts.push("--path", q(opts.path));
  parts.push("--binding=ENRICH_KV", `--env=${env}`, "--remote");
  execWithRetry(exec, parts.join(" "), `put ${key}`);
}

// A test seam that can only make a publish FAIL, never pass: the named row's
// blob write throws on every attempt, through the real retry. Set from the
// photos workflow's manual-dispatch input for acceptance tests A4/A14.
function execFor(target: string): Exec {
  const planted = process.env.PHOTO_INGEST_PLANT_FAIL;
  if (planted && planted === target) {
    return () => { throw new Error(`planted failure for ${target} (PHOTO_INGEST_PLANT_FAIL)`); };
  }
  return realExec;
}

// The manifest already published to KV, or null when there is none / it cannot be
// read. Used to skip rows whose artifacts are already correct.
//
// WHY THIS EXISTS. Every run rewrote all four artifacts for all 233 rows -- ~940
// KV writes, roughly twenty minutes -- to re-upload bytes that had not changed.
// Blobs are CONTENT-ADDRESSED, so an unchanged photo produces byte-identical keys
// and the writes are provably no-ops. The cost was not just time: it made every
// framing experiment a twenty-minute commitment, which is the kind of friction
// that stops experiments happening at all.
//
// KV is the comparison source rather than a local state file on purpose. A side
// file records what THIS machine believes it uploaded; KV records what is
// actually being served, and those diverge the moment anyone ingests from
// somewhere else. When it cannot be read the answer is to upload everything --
// the expensive direction is the safe one.
//
// READ OVER REST, not wrangler, since 2026-09-23: the render-drift job
// (photo-drift.yml) runs this with a KV-READ-only token (PHOTO_KV_READ_TOKEN)
// and no write token at all, and a REST read is what that token can do.
let manifestReadError = ""; // why the last fetchPublishedManifest returned null
async function fetchPublishedManifest(env: string): Promise<ManifestEntry[] | null> {
  const fail = (why: string) => {
    manifestReadError = why;
    console.warn(`  manifest read failed: ${why}`);
    return null;
  };
  try {
    const target = kvTargetFromWranglerToml("wrangler.toml", env, readToken());
    const text = await getValue(target, MANIFEST_KEY);
    if (text === null) return fail(`${MANIFEST_KEY} does not exist in ${env}`);
    const parsed: unknown = JSON.parse(text);
    return Array.isArray(parsed) ? (parsed as ManifestEntry[]) : fail(`${MANIFEST_KEY} is not a JSON array`);
  } catch (err) {
    // SAY WHY. This returned a bare null, and the caller then printed "could not
    // read the published manifest" -- which is true, uninformative, and identical
    // for a missing key, an expired token, a wrangler crash and a JSON parse
    // failure. Those want four different responses, and the run continues in a
    // mode ("upload every row") that looks like a decision rather than a
    // fallback, so nothing downstream reveals which one happened.
    return fail(String(err instanceof Error ? err.message : err).slice(0, 300));
  }
}

// True when every artifact this row would write is already in KV under exactly
// the key we are about to write. Compares the SQUARE keys too, not just the
// rectangle -- a run that added square variants to an existing row changes no
// rectangle key at all, so comparing only that would skip the very rows the
// square rollout needed to write.
function alreadyPublished(
  prior: ManifestEntry | undefined,
  blobKey: string,
  squareKeys: Record<string, string>,
): boolean {
  if (!prior || prior.blobKey !== blobKey) return false;
  const had = prior.squareKeys ?? {};
  const want = Object.keys(squareKeys);
  if (want.length !== Object.keys(had).length) return false;
  return want.every((k) => had[k] === squareKeys[k]);
}


async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));

  // --selftest-allowlist <key>: drive the REAL wranglerPut with a spy in place
  // of the shell. A refused key must throw before the spy is ever called; an
  // allowed key must reach the spy -- the control that proves the spy can see a
  // call at all. Nothing touches KV either way.
  if (args.selftestAllowlist) {
    const calls: string[] = [];
    const spy: Exec = (c) => { calls.push(c); };
    let refused = false;
    try {
      wranglerPut("production", args.selftestAllowlist, { value: "selftest" }, spy);
    } catch (err) {
      refused = err instanceof KeyNotAllowed;
      console.log(`allowlist: ${String(err instanceof Error ? err.message : err)}`);
    }
    wranglerPut("production", "pptr:t:ALLOWLIST-SELFTEST", { value: "selftest" }, spy);
    const reachedForForbidden = calls.some((c) => c.includes(`"${args.selftestAllowlist}"`));
    const reachedForAllowed = calls.some((c) => c.includes('"pptr:t:ALLOWLIST-SELFTEST"'));
    console.log(`"${args.selftestAllowlist}": refused=${refused} reached wrangler=${reachedForForbidden}; ` +
      `CONTROL pptr:t:ALLOWLIST-SELFTEST reached wrangler=${reachedForAllowed}`);
    process.exit(refused && !reachedForForbidden && reachedForAllowed ? 0 : 1);
  }

  const manifestPath = join(args.photosDir, "manifest.json");
  const raw = readFileSync(manifestPath, "utf8");
  const entries = JSON.parse(raw) as ManifestEntry[];
  if (!Array.isArray(entries)) throw new Error(`${manifestPath} must be a JSON array`);

  // --- license + shape gate (atomic) ---
  let failed = 0;
  for (const e of entries) {
    const res = validateEntry(e);
    if (!res.ok) {
      failed++;
      console.error(`REJECT ${e.kind}:${e.target} [${res.license}]`);
      for (const err of res.errors) console.error(`   - ${err}`);
    }
  }
  if (failed > 0) {
    console.error(`\n${failed}/${entries.length} manifest rows failed the gate; aborting (no uploads).`);
    process.exit(1);
  }
  console.log(`gate: ${entries.length}/${entries.length} rows OK`);
  if (args.checkOnly) return;

  // --- resize + content-address + upload + pointer flip ---
  const tmp = mkdtempSync(join(tmpdir(), "blip-photo-"));
  // Not optional. Every branch below used to be guarded on `sharp &&`, with a
  // `Buffer.from(src)` fallback that hashed the SOURCE file -- which is precisely
  // what let a sharp-less dry run print 213 plausible lines and prove nothing.
  let sharp: typeof import("sharp");
  // --dry-run STILL RESIZES. It used to skip sharp entirely, which made the one
  // mode you would reach for before a real ingest the one mode that never
  // exercised the image pipeline -- the crop, the extract geometry and the
  // baseline-JPEG assertion were all unreachable, so a regression in any of them
  // would sail through a clean dry run and surface on a device. The docstring
  // already claimed "everything but the KV writes"; now that is true.
  //
  // AND IT REFUSES TO RUN WITHOUT SHARP (#207). This used to print one
  // console.warn and carry on, which put the bug back one layer out: sharp is an
  // optional dependency, so ABSENT IS THE DEFAULT STATE OF A FRESH CHECKOUT, and
  // a single warning scrolls past 213 lines of per-row output and is gone. The
  // mode was then proving nothing again, with the operator holding a clean run
  // and no signal they had lost their only pre-flight check.
  //
  // A dry run that cannot exercise the pipeline is a FAILED dry run, not a
  // partial one. --check is the mode that deliberately tests only the licence
  // gate, so nothing is lost by making this one honest.
  try {
    sharp = (await import("sharp")).default as unknown as typeof import("sharp");
  } catch {
    throw new Error(
      "sharp is required and is not installed: npm i -D sharp\n" +
        (args.dryRun
          ? "  --dry-run exercises the resize/crop path on purpose -- without sharp it would\n" +
            "  check nothing but the licence gate, which is what --check already does."
          : "  an upload run cannot produce the artifacts without it."),
    );
  }

  // What KV already holds, fetched once. A single round trip buys the right to
  // skip up to ~940 of them.
  //
  // FETCHED ON --dry-run TOO, since 2026-08-14. It previously was not, and that
  // made the mode unable to answer the only question it gets run to answer.
  // Without the prior manifest every row compares against nothing, so a dry run
  // printed all 234 rows whether the library was completely stale or perfectly
  // current -- the same output in both worlds, which is not a measurement.
  //
  // It is the same defect the comment above about sharp describes ("a dry run
  // that cannot exercise the pipeline is a FAILED dry run"), one step further
  // along: the pipeline ran, and then its result was compared to nothing.
  //
  // Read-only, so it costs one KV GET and changes nothing.
  const priorByTarget = new Map<string, ManifestEntry>();
  if (args.env && !args.force) {
    const prior = await fetchPublishedManifest(args.env);
    if (prior) {
      for (const p of prior) priorByTarget.set(`${p.kind}:${p.target}`, p);
      console.log(`published manifest read: ${prior.length} rows already in ${args.env}`);
    } else if (args.dryRun) {
      // A DRY RUN THAT CANNOT READ THE PUBLISHED MANIFEST HAS NO ANSWER. It used to
      // fall through to the branch below and compare every row against nothing,
      // printing N of N CHANGED -- exactly what a renderer or sharp change prints,
      // so a dead token read as "the whole library drifted". An upload run may
      // fall back to uploading everything (the safe direction); a measurement may
      // not fall back to anything. Exit 3, the same code as UNTRUSTWORTHY.
      console.error(`could not read the published manifest from ${args.env} -- REFUSING: ` +
        `a dry run compared against nothing would report every row CHANGED`);
      finishDryRun(args, { ...newDryRunStatus(args.env), verdict: "UNREADABLE",
        reason: `could not read ${MANIFEST_KEY} from ${args.env}: ${manifestReadError}` }, 3);
    } else {
      // Say so. A silent fall-through to "upload everything" is the same shape as
      // a silent skip, just expensive instead of wrong.
      console.log(`could not read the published manifest -- uploading every row`);
    }
  }

  // ---- PLAN: render every row and derive every key. Nothing is written yet. ----
  //
  // Planning the whole run before the first write is what lets a failure say
  // exactly which rows are live, which failed and which were never reached --
  // the status a publish must report instead of "something went wrong".
  interface Planned {
    e: ManifestEntry;
    jpeg: Buffer;
    squareBufs: { size: number; key: string; buf: Buffer }[];
    blobKey: string;
    squareKeys: Record<string, string>;
  }
  const toWrite: Planned[] = [];
  let resized = 0, squaresMade = 0, noSource = 0, skipped = 0, changed = 0, sampled = 0;
  const changedRows: string[] = [];
  for (const e of entries) {
    if (!e.file) {
      console.warn(`skip ${e.kind}:${e.target}: no source file`);
      noSource++;
      continue;
    }
    const src = readFileSync(join(args.photosDir, e.file));
    // Rendering lives in photo-render.ts so the dashboard's preview is these
    // exact bytes. Both renders refuse anything but a baseline JPEG (SOF2 =
    // progressive = undecodable on-device). THE RECTANGLE STAYS 150x100: the
    // firmware's drawJpg clips rather than scales, so any larger image would
    // render as its own top-left corner on every legacy device.
    const jpeg = await renderRect(sharp, src, e);
    const squares = args.square ? await renderSquares(sharp, src, e) : [];

    resized++;
    squaresMade += squares.length;
    const blobKey = await deriveBlobKey(e.target, new Uint8Array(jpeg));
    e.blobKey = blobKey;

    // Every key this row would write, derived BEFORE any upload decision.
    const squareKeys: Record<string, string> = {};
    const squareBufs: { size: number; key: string; buf: Buffer }[] = [];
    for (const s of squares) {
      const sKey = await deriveBlobKey(e.target, new Uint8Array(s.buf));
      squareKeys[String(s.size)] = sKey;
      squareBufs.push({ size: s.size, key: sKey, buf: s.buf });
    }
    e.squareKeys = squareKeys;

    const sq = squares.map((s) => `${s.size}:${s.buf.length}B`).join(" ");
    const unchanged = alreadyPublished(priorByTarget.get(`${e.kind}:${e.target}`), blobKey, squareKeys);
    if (args.dryRun || !args.env) {
      if (unchanged) skipped++;
      else { changed++; changedRows.push(`${e.kind}:${e.target}`); }
      // Optional: write the artifacts this run WOULD publish, so they can be
      // looked at before the library is rewritten -- the real pipeline's bytes.
      if (args.sampleOut && !unchanged && sampled < args.sampleCount) {
        mkdirSync(args.sampleOut, { recursive: true });
        for (const s of squares) {
          writeFileSync(join(args.sampleOut, `${e.kind}-${e.target}-${s.size}.jpg`), s.buf);
        }
        sampled++;
      }
      console.log(
        `${unchanged ? "same   " : "CHANGED"} ${e.kind}:${e.target} -> ${blobKey} ` +
          `(${jpeg.length} B)${sq ? `  square ${sq}` : ""}`,
      );
      continue;
    }
    // ALREADY THERE: content-addressed keys make "unchanged" provable -- the
    // writes would be byte-identical no-ops. The row still goes into the manifest.
    if (unchanged) {
      skipped++;
      continue;
    }
    toWrite.push({ e, jpeg, squareBufs, blobKey, squareKeys });
  }

  // A LAST LINE THAT STATES WHAT ACTUALLY HAPPENED (#207).
  console.log(
    `summary: ${resized} resized, ${squaresMade} square variant(s), ` +
      `${noSource} row(s) with no source file` +
      (args.dryRun ? `, ${changed} CHANGED vs the published manifest, ${skipped} already current` : "") +
      (!args.dryRun && skipped ? `, ${skipped} unchanged and SKIPPED (${skipped * 4} KV writes avoided)` : "") +
      (!args.dryRun && args.env ? `, ${toWrite.length} to write` : "") +
      (args.sampleOut ? `, ${sampled} sample row(s) written to ${args.sampleOut}` : "") +
      (args.square ? "" : "  [--no-square: square variants were NOT built]") +
      (args.force ? "  [--force: skip check bypassed]" : "") +
      (args.dryRun ? "  [--dry-run: nothing was written to KV]" : ""),
  );

  // --- the public manifest (drop local file paths) + credits page ---
  const publicManifest: ManifestEntry[] = entries.map(({ file, ...rest }) => rest);
  const creditsHtml = renderCreditsHtml(publicManifest);
  writeFileSync(join(args.photosDir, "credits.html"), creditsHtml);
  console.log(`wrote ${join(args.photosDir, "credits.html")}`);

  if (args.dryRun && args.env && !args.force) {
    // What the render-drift job reads. Published rows the repo no longer has are
    // a difference too -- a dry run renders only the repo's rows, so it would
    // otherwise never mention them.
    const here = new Set(entries.map((e) => `${e.kind}:${e.target}`));
    finishDryRun(args, {
      ...newDryRunStatus(args.env), verdict: "READ", total: resized, changed, same: skipped,
      changedRows, publishedNotInRepo: [...priorByTarget.keys()].filter((k) => !here.has(k)),
    }, 0);
  }
  if (args.dryRun || !args.env) return;
  const env = args.env;
  const status = newStatus(env);
  status.skipped = skipped;
  status.toWrite = toWrite.map((p) => p.e.target);

  // ---- PREFLIGHT: can the verifier see KV, and would any row be lost? ----
  //
  // BEFORE ANY WRITE. A blind verifier cannot certify what follows, so a dead
  // or wrong token stops the run here with nothing written (A11) -- reported as
  // UNTRUSTWORTHY, never as a list of "missing" keys.
  //
  // PHOTO_VERIFY_TOKEN overrides the read token only, and only ever makes the
  // run refuse: it is the seam for acceptance test A11.
  const target = kvTargetFromWranglerToml("wrangler.toml", env,
    // `||`, not `??`: the workflow passes an EMPTY string when the A11 input is
    // off, and an empty token would blind the verifier on every normal publish.
    process.env.PHOTO_VERIFY_TOKEN || process.env.CLOUDFLARE_API_TOKEN || "");
  const pre0 = await readControls(target, MANIFEST_KEY);
  let publishedRows: { kind: string; target: string }[] | null = null;
  if (pre0.knownPresentReadsBack) {
    try {
      const v = await bulkGet(target, [MANIFEST_KEY]);
      publishedRows = JSON.parse(v[MANIFEST_KEY] ?? "[]") as ManifestEntry[];
    } catch {
      pre0.knownPresentReadsBack = false;
    }
  }
  const pre = preflight({ controls: pre0, published: publishedRows, next: entries });
  if (pre.verdict !== "PASS") {
    status.verdict = pre.verdict === "UNTRUSTWORTHY" ? "UNTRUSTWORTHY" : "FAILED";
    status.reason = pre.reasons.join(" ") + (pre0.error ? ` (${pre0.error})` : "");
    status.notReached = status.toWrite;
    finish(args, status, pre.verdict === "UNTRUSTWORTHY" ? 3 : 1);
  }
  console.log(`preflight: verifier sees ${env} KV; ${publishedRows?.length ?? 0} published row(s), none dropped`);

  // ---- WRITES: changed rows only, in manifest order, blob before pointer. ----
  let writeFailed = false;
  for (let i = 0; i < toWrite.length; i++) {
    const p = toWrite[i]!;
    const exec = execFor(p.e.target);
    try {
      console.log(`${p.e.kind}:${p.e.target} -> ${p.blobKey} (${p.jpeg.length} B)`);
      const blobPath = join(tmp, `${p.blobKey.replace(/[^a-z0-9]/gi, "_")}.jpg`);
      writeFileSync(blobPath, p.jpeg);
      wranglerPut(env, p.blobKey, { path: blobPath }, exec); // immutable blob
      wranglerPut(env, pointerKey(p.e.kind, p.e.target), { value: p.blobKey }, exec); // pointer flip
      // Square variants LAST, each blob strictly before its pointer: an
      // interrupted run degrades to "some devices still get the old square",
      // never to a pointer naming a blob that does not exist. The blob key is
      // photo:<target>-<hash8> with NO size suffix -- BLOB_KEY_RE rejects a
      // second dash, which silently drops the square on serve.
      for (const s of p.squareBufs) {
        const sPath = join(tmp, `${s.key.replace(/[^a-z0-9]/gi, "_")}.jpg`);
        writeFileSync(sPath, s.buf);
        wranglerPut(env, s.key, { path: sPath }, exec);
        wranglerPut(env, pointerKey(p.e.kind, p.e.target, s.size), { value: s.key }, exec);
      }
      status.live.push(p.e.target);
    } catch (err) {
      // The rows before this one are LIVE -- their pointers flipped. Name this
      // one and the rest, stop writing, and STILL VERIFY: the status should say
      // what KV actually holds, not only what this loop believes it wrote.
      // The manifest is not written either way.
      writeFailed = true;
      status.failed = [p.e.target];
      status.notReached = toWrite.slice(i + 1).map((q) => q.e.target);
      status.reason = `write failed on ${p.e.kind}:${p.e.target}: ${String(err instanceof Error ? err.message : err).slice(0, 300)}`;
      break;
    }
  }

  // ---- VERIFY: every row, every key, read back. ----
  //
  // KV is eventually consistent, so a pointer written seconds ago can read
  // stale. Re-read up to five times, 15 s apart, before calling a mismatch a
  // failure -- and report how many reads it took. After a write failure the
  // mismatches are expected (they are the failed and unreached rows), so one
  // read is enough to report them.
  const tv = Date.now();
  let res = null as ReturnType<typeof verify> | null;
  for (let attempt = 1; attempt <= (writeFailed ? 1 : 5); attempt++) {
    const controls = await readControls(target, MANIFEST_KEY);
    let pointers: Record<string, string | null> = {};
    let blobs = new Set<string>();
    try {
      pointers = await bulkGet(target, expectedPointers(publicManifest).map(([k]) => k));
      blobs = await listKeys(target, "photo:");
    } catch (err) {
      controls.knownPresentReadsBack = false;
      controls.error = String(err instanceof Error ? err.message : err);
    }
    res = verify({ controls, manifest: publicManifest, pointers, blobsPresent: blobs });
    status.verifyReads = attempt;
    if (res.verdict !== "FAIL") break;
    if (attempt < 5) {
      console.log(`verify read ${attempt}: ${res.reasons.join("; ").slice(0, 200)} -- re-reading in 15 s`);
      sleepSync(15_000);
    }
  }
  status.verifyMs = Date.now() - tv;
  status.pointersChecked = res!.pointersChecked;
  if (writeFailed) {
    // Refused whatever verify said: a publish that hit an error is not
    // certified, even if KV happens to look complete. Retry converges.
    status.verdict = res!.verdict === "UNTRUSTWORTHY" ? "UNTRUSTWORTHY" : "FAILED";
    status.reason += ` Verifier: ${res!.verdict === "PASS" ? "every key reads back, but the run hit an error; not certified" : res!.reasons.join(" ")}`;
    finish(args, status, 1);
  }
  if (res!.verdict !== "PASS") {
    status.verdict = res!.verdict === "UNTRUSTWORTHY" ? "UNTRUSTWORTHY" : "FAILED";
    status.reason = res!.reasons.join(" ");
    finish(args, status, res!.verdict === "UNTRUSTWORTHY" ? 3 : 1);
  }
  console.log(`verify: ${res!.pointersChecked}/${res!.pointersChecked} pointers and every blob they name, ` +
    `in ${status.verifyMs} ms over ${status.verifyReads} read(s)`);

  // ---- MANIFEST LAST: the record of what is live, written only on PASS. ----
  const manifestFile = join(tmp, "manifest.json");
  writeFileSync(manifestFile, JSON.stringify(publicManifest));
  wranglerPut(env, MANIFEST_KEY, { path: manifestFile });
  status.manifestWritten = true;
  status.verdict = "LIVE";
  status.liveAt = new Date().toISOString();
  console.log(`published ${MANIFEST_KEY} (${publicManifest.length} entries) + credits.html`);
  finish(args, status, 0);
}

// ---------------------------------------------------------------- status

interface PublishStatus {
  v: 1;
  env: string;
  sha: string;
  verdict: "LIVE" | "FAILED" | "UNTRUSTWORTHY" | "RUNNING";
  reason: string;
  toWrite: string[];
  live: string[];
  failed: string[];
  notReached: string[];
  skipped: number;
  pointersChecked: number;
  verifyMs: number;
  verifyReads: number;
  manifestWritten: boolean;
  liveAt: string;
  finishedAt: string;
}

function newStatus(env: string): PublishStatus {
  return {
    v: 1, env, sha: process.env.GITHUB_SHA ?? "", verdict: "RUNNING", reason: "",
    toWrite: [], live: [], failed: [], notReached: [], skipped: 0,
    pointersChecked: 0, verifyMs: 0, verifyReads: 0, manifestWritten: false, liveAt: "", finishedAt: "",
  };
}

// Every exit goes through here, so a publish can never end without saying what
// happened -- the six-week silence was a publish that reported nothing.
function finish(args: Args, status: PublishStatus, code: number): never {
  status.finishedAt = new Date().toISOString();
  const line = `publish ${status.env}: ${status.verdict}` +
    (status.reason ? ` -- ${status.reason}` : "") +
    ` | live: ${status.live.join(",") || "none"}` +
    (status.failed.length ? ` | failed: ${status.failed.join(",")}` : "") +
    (status.notReached.length ? ` | not reached: ${status.notReached.join(",")}` : "");
  (code === 0 ? console.log : console.error)(line);
  if (args.statusOut) writeFileSync(args.statusOut, JSON.stringify(status, null, 2) + "\n");
  // NOT process.exit(). With fetch's sockets still closing, process.exit() on
  // Windows trips a libuv assertion (UV_HANDLE_CLOSING) and the process dies
  // with 127 instead of the verdict's code -- observed on the first A11 smoke.
  // Setting exitCode and unwinding lets the event loop drain and exit cleanly.
  process.exitCode = code;
  throw new Finished();
}

// A dry run's status: what the render-drift job reads as its half (a). Only
// written with --env (without one there is nothing to compare against).
interface DryRunStatus {
  v: 1;
  mode: "dry-run";
  env: string;
  sha: string;
  /** READ: the comparison happened. UNREADABLE: it could not, and nothing below is a count. */
  verdict: "READ" | "UNREADABLE";
  reason: string;
  total: number;
  changed: number;
  same: number;
  changedRows: string[];
  publishedNotInRepo: string[];
  finishedAt: string;
}

function newDryRunStatus(env: string): DryRunStatus {
  return {
    v: 1, mode: "dry-run", env, sha: process.env.GITHUB_SHA ?? "", verdict: "READ", reason: "",
    total: 0, changed: 0, same: 0, changedRows: [], publishedNotInRepo: [], finishedAt: "",
  };
}

function finishDryRun(args: Args, status: DryRunStatus, code: number): never {
  status.finishedAt = new Date().toISOString();
  (code === 0 ? console.log : console.error)(
    `dry-run ${status.env}: ${status.verdict}` + (status.reason ? ` -- ${status.reason}` : "") +
      (status.verdict === "READ" ? ` | ${status.changed} of ${status.total} CHANGED, ${status.publishedNotInRepo.length} published row(s) not in the repo` : ""),
  );
  if (args.statusOut) writeFileSync(args.statusOut, JSON.stringify(status, null, 2) + "\n");
  process.exitCode = code;
  throw new Finished();
}

class Finished extends Error {}

main().catch((err) => {
  if (err instanceof Finished) return; // finish() already reported and set exitCode
  console.error(String(err instanceof Error ? err.stack : err));
  process.exitCode = 1;
});
