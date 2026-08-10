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
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { cropRect, scrimRGBA, type Framing } from "../src/framing";
import {
  MANIFEST_KEY,
  deriveBlobKey,
  isBaselineJpeg,
  pointerKey,
  renderCreditsHtml,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";

// LEGACY slot dims. Every device shipped up to FW 6 draws a 150x100 photo into a
// fixed slot, and its drawJpg call site passes no scale -- maxWidth/maxHeight
// CLIP rather than shrink -- so anything larger renders as its own top-left
// corner. This variant must keep existing, unchanged, for as long as one of those
// devices is in the field. Baseline (non-progressive) JPEG only: the on-device
// decoder (LovyanGFX drawJpg via TJpgDec) cannot decode progressive.
const PHOTO_W = 150;
const PHOTO_H = 100;

// SQUARE (full-bleed) dims, one per distinct panel size across the SKUs. The
// card became the whole disc in FW 7 (issue #209), so the artifact is the panel:
// 240 = Kit S3 / the retired C3 form factor, 412 = the 1.46B, 480 = the Pro 2.1.
//
// Emitted per size rather than emitted once and scaled on-device. drawJpg CAN
// scale (the float scale_x/scale_y overload; the jpeg_div_t one is deprecated),
// so this is a choice: upscaling a 240 artifact to 480 throws away exactly the
// detail a bigger panel exists to show, and the crop aspect is identical across
// sizes so there is nothing else to gain by sharing one.
const SQUARE_SIZES = [240, 412, 480] as const;

// Where the subject sits vertically in the square. Below 0.5 lifts it, keeping
// the aeroplane clear of the callsign band along the bottom.
const SQUARE_PLACE = 0.38;

interface Args {
  env?: string;
  dryRun: boolean;
  checkOnly: boolean;
  photosDir: string;
  square: boolean;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false, checkOnly: false, photosDir: "photos", square: true };
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
    else if (v === "--photos-dir") a.photosDir = argv[++i] ?? a.photosDir;
    else throw new Error(`unknown argument: ${v}`);
  }
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
// retry that aborts the whole idempotent run. Retry a few times with backoff;
// re-running a write is a no-op flip on a content-addressed blob.
function execWithRetry(cmd: string, label: string, attempts = 4): void {
  const backoffMs = [1500, 4000, 9000];
  for (let i = 0; i < attempts; i++) {
    try {
      execSync(cmd, { stdio: "inherit" });
      return;
    } catch (err) {
      if (i === attempts - 1) throw err;
      const wait = backoffMs[Math.min(i, backoffMs.length - 1)];
      console.error(`  ${label}: write failed (attempt ${i + 1}/${attempts}); retrying in ${wait / 1000}s ...`);
      // Synchronous sleep so the retry stays inline with the sequential upload.
      execSync(process.platform === "win32" ? `powershell -Command "Start-Sleep -Milliseconds ${wait}"` : `sleep ${wait / 1000}`);
    }
  }
}

function wranglerPut(env: string, key: string, opts: { value?: string; path?: string }): void {
  const parts = ["npx", "wrangler", "kv", "key", "put", q(key)];
  if (opts.value !== undefined) parts.push(q(opts.value));
  if (opts.path !== undefined) parts.push("--path", q(opts.path));
  parts.push("--binding=ENRICH_KV", `--env=${env}`, "--remote");
  execWithRetry(parts.join(" "), `put ${key}`);
}

// The source region to take for one entry, honouring its framing judgement.
// Kept next to the resize it feeds so the two cannot drift; the geometry itself
// is in src/framing.ts, where vitest can reach it without sharp.
//
// `place` is 0.5 for the rectangle (nothing is drawn over it) and lower for the
// square, where text lands on the lower third and the aeroplane must sit above it.
async function extractFor(
  sharp: typeof import("sharp"),
  src: Buffer,
  outW: number,
  outH: number,
  e: { focus?: [number, number]; zoom?: number; target: string },
  place?: number,
) {
  const meta = await sharp(src).metadata();
  const w = meta.width ?? 0;
  const h = meta.height ?? 0;
  if (!w || !h) throw new Error(`${e.target}: source has no dimensions`);
  const framing: Framing = { focus: e.focus, zoom: e.zoom, place };
  return cropRect(w, h, outW, outH, framing);
}

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
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

  let resized = 0, squaresMade = 0, noSource = 0;
  for (const e of entries) {
    if (!e.file) {
      console.warn(`skip ${e.kind}:${e.target}: no source file`);
      noSource++;
      continue;
    }
    const src = readFileSync(join(args.photosDir, e.file));
    // Resize to the exact sprite, cover-crop, baseline JPEG, metadata (EXIF) dropped.
    // NO mozjpeg: sharp's `mozjpeg: true` preset force-enables PROGRESSIVE encoding
    // (overriding `progressive: false`), and the on-device decoder (TJpgDec via
    // LovyanGFX drawJpg) cannot decode progressive -- the failure is a silent
    // "No photo available" after a successful 200 (found the hard way on the bench).
    //
    // THE OUTPUT SIZE IS FIXED AT PHOTO_W x PHOTO_H AND MUST STAY THERE. The
    // firmware calls drawJpg with maxWidth/maxHeight equal to its 150x100 sprite,
    // and those arguments CLIP rather than scale -- so an image any larger would
    // render as its own top-left corner on every device in the field, silently and
    // on the first card the owner opened. The full-bleed 240x240 variant therefore
    // ships as a SEPARATE artifact behind --square, not as a change to this one.
    // See src/framing.ts.
    const jpeg = await sharp(src)
      .extract(await extractFor(sharp, src, PHOTO_W, PHOTO_H, e))
      .resize(PHOTO_W, PHOTO_H, { fit: "cover" })
      .jpeg({ progressive: false, quality: 82 })
      .toBuffer();

    // The full-bleed square variants: one per panel size, cover-cropped to 1:1
    // with the subject lifted clear of the callsign band, and the graded scrim
    // COMPOSITED IN AT INGEST rather than drawn on-device. Baking it is what lets
    // the contrast guarantee be a measurement on a real artifact instead of a
    // property of renderer code -- and the device cannot cheaply alpha-blend a
    // gradient over a JPEG it has just decoded into an 8bpp sprite anyway.
    const squares: { size: number; buf: Buffer }[] = [];
    if (args.square) {
      for (const size of SQUARE_SIZES) {
        const rect = await extractFor(sharp, src, size, size, e, SQUARE_PLACE);
        const scrim = Buffer.from(scrimRGBA(size, size));
        const buf = await sharp(src)
          .extract(rect)
          .resize(size, size, { fit: "cover" })
          .composite([{ input: scrim, raw: { width: size, height: size, channels: 4 }, blend: "over" }])
          .jpeg({ progressive: false, quality: 82 })
          .toBuffer();
        if (!isBaselineJpeg(buf))
          throw new Error(`${e.kind}:${e.target}: square ${size} is not baseline JPEG; aborting`);
        squares.push({ size, buf });
      }
    }

    // Hard assertion: refuse to upload anything but a baseline JPEG (SOF0/SOF1).
    // SOF2 = progressive = undecodable on-device; guard here so no encoder-option
    // drift can ever ship a poison blob again.
    if (!isBaselineJpeg(jpeg)) {
      throw new Error(`${e.kind}:${e.target}: encoded JPEG is not baseline (progressive?); aborting`);
    }

    resized++;
    squaresMade += squares.length;
    const blobKey = await deriveBlobKey(e.target, new Uint8Array(jpeg));
    e.blobKey = blobKey;
    const ptr = pointerKey(e.kind, e.target);
    const sq = squares.map((s) => `${s.size}:${s.buf.length}B`).join(" ");
    console.log(`${e.kind}:${e.target} -> ${blobKey} (${jpeg.length} B)${sq ? `  square ${sq}` : ""}`);

    if (args.dryRun || !args.env) continue;
    const blobPath = join(tmp, `${blobKey.replace(/[^a-z0-9]/gi, "_")}.jpg`);
    writeFileSync(blobPath, jpeg);
    wranglerPut(args.env, blobKey, { path: blobPath }); // immutable blob
    wranglerPut(args.env, ptr, { value: blobKey }); // pointer flip

    // Square variants LAST, and each blob strictly before its pointer. A run
    // interrupted anywhere leaves the legacy pointer already flipped and correct,
    // and a square pointer that exists always resolves to a blob that exists --
    // so a half-finished ingest degrades to "some devices still get rectangles",
    // never to a dangling pointer or a clipped card.
    for (const s of squares) {
      // The TARGET, not `${target}-s${size}`. Blob keys are validated on serve by
      // BLOB_KEY_RE (photo:<target>-<hash8>, target alphanumeric), and a size
      // suffix puts a second dash in the target segment -- which the regex
      // rejects, so resolvePhoto drops the square and falls back to the rectangle
      // SILENTLY, with a 200 on the wire and nothing in any log. Caught only by
      // reading a key the ingest had actually written; the unit tests passed
      // because their fixtures were hand-written keys of the right shape rather
      // than keys this code produces.
      //
      // No suffix is needed anyway: the blobs are content-addressed, so three
      // sizes of one type hash to three different keys on their own, and the
      // POINTER key already carries the size.
      const sKey = await deriveBlobKey(e.target, new Uint8Array(s.buf));
      const sPath = join(tmp, `${sKey.replace(/[^a-z0-9]/gi, "_")}.jpg`);
      writeFileSync(sPath, s.buf);
      wranglerPut(args.env, sKey, { path: sPath });
      wranglerPut(args.env, pointerKey(e.kind, e.target, s.size), { value: sKey });
    }
  }

  // A LAST LINE THAT STATES WHAT ACTUALLY HAPPENED (#207). The per-row output is
  // 213 lines long, so anything important said at the top is gone by the end. The
  // counts are what a reader would otherwise have to reconstruct by scrolling --
  // and "0 resized" is the shape of every silent-skip bug this script has had.
  console.log(
    `summary: ${resized} resized, ${squaresMade} square variant(s), ` +
      `${noSource} row(s) with no source file` +
      (args.square ? "" : "  [--no-square: square variants were NOT built]") +
      (args.dryRun ? "  [--dry-run: nothing was written to KV]" : ""),
  );

  // --- publish the public manifest (drop local file paths) + credits page ---
  const publicManifest: ManifestEntry[] = entries.map(({ file, ...rest }) => rest);
  const creditsHtml = renderCreditsHtml(publicManifest);
  writeFileSync(join(args.photosDir, "credits.html"), creditsHtml);
  console.log(`wrote ${join(args.photosDir, "credits.html")}`);

  if (!args.dryRun && args.env) {
    const manifestJson = JSON.stringify(publicManifest);
    const manifestFile = join(tmp, "manifest.json");
    writeFileSync(manifestFile, manifestJson);
    wranglerPut(args.env, MANIFEST_KEY, { path: manifestFile });
    console.log(`published ${MANIFEST_KEY} (${publicManifest.length} entries) + credits.html`);
  }
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
