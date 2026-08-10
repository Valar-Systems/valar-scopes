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
import { cropRect, scrimRGBA, subjectCrop, type Framing, type SubjectBox } from "../src/framing";
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
  force: boolean;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false, checkOnly: false, photosDir: "photos", square: true, force: false };
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
    // Re-upload every row even when KV already holds the identical key. The skip
    // is a claim about what is in KV, inferred from the published manifest; this
    // is how you act when you suspect that claim is wrong (a partial run, a
    // hand-edited key, a namespace restored from elsewhere).
    else if (v === "--force") a.force = true;
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
function fetchPublishedManifest(env: string): ManifestEntry[] | null {
  try {
    const out = execSync(
      `npx wrangler kv key get ${q(MANIFEST_KEY)} --binding=ENRICH_KV --env=${env} --remote --text`,
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 64 * 1024 * 1024 },
    );
    const start = out.indexOf("[");
    if (start < 0) return null;
    const parsed: unknown = JSON.parse(out.slice(start));
    return Array.isArray(parsed) ? (parsed as ManifestEntry[]) : null;
  } catch {
    return null;
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

// Where the aeroplane is, as a normalised box.
//
// The background is estimated PER ROW, from the outer pixels at each end of that
// row. Per row because sky is a vertical gradient: one global background colour
// scores the top of the frame as subject and reports 40% on a picture that is
// nothing but sky. Rows and columns carrying only a trickle of hits are dropped,
// so the box tracks the aeroplane rather than every non-sky pixel -- haze, a
// distant treeline, a watermark.
//
// Detection is deliberately dumb and local: no model, no network, nothing to
// version. It is checked by rendering the whole library and looking, which is
// how the fill cap was chosen.
const DETECT_W = 240;   // detection resolution -- the box is normalised, so this need not be large
const DETECT_EDGE = 6;  // pixels sampled at each end of a row for the background
const DETECT_DIST = 42; // RGB distance beyond which a pixel counts as subject
async function detectSubjectBox(
  sharp: typeof import("sharp"),
  src: Buffer,
): Promise<SubjectBox> {
  const { data, info } = await sharp(src)
    .resize(DETECT_W, null, { fit: "inside" })
    .removeAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true });
  const { width: w, height: h, channels: c } = info;
  const med = (xs: number[]) => xs.slice().sort((a, b) => a - b)[Math.floor(xs.length / 2)] ?? 0;
  const col = new Array<number>(w).fill(0);
  const row = new Array<number>(h).fill(0);
  for (let y = 0; y < h; y++) {
    const rs: number[] = [], gs: number[] = [], bs: number[] = [];
    for (let k = 0; k < DETECT_EDGE; k++) {
      for (const x of [k, w - 1 - k]) {
        const i = (y * w + x) * c;
        rs.push(data[i] ?? 0); gs.push(data[i + 1] ?? 0); bs.push(data[i + 2] ?? 0);
      }
    }
    const br = med(rs), bg = med(gs), bb = med(bs);
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * c;
      const dr = (data[i] ?? 0) - br, dg = (data[i + 1] ?? 0) - bg, db = (data[i + 2] ?? 0) - bb;
      if (Math.sqrt(dr * dr + dg * dg + db * db) > DETECT_DIST) { col[x]!++; row[y]!++; }
    }
  }
  const tC = Math.max(2, h * 0.02), tR = Math.max(2, w * 0.02);
  let x0 = 0, x1 = w - 1, y0 = 0, y1 = h - 1;
  while (x0 < x1 && (col[x0] ?? 0) < tC) x0++;
  while (x1 > x0 && (col[x1] ?? 0) < tC) x1--;
  while (y0 < y1 && (row[y0] ?? 0) < tR) y0++;
  while (y1 > y0 && (row[y1] ?? 0) < tR) y1--;
  return { x0: x0 / w, x1: x1 / w, y0: y0 / h, y1: y1 / h };
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

  // What KV already holds, fetched once. A single round trip buys the right to
  // skip up to ~940 of them.
  const priorByTarget = new Map<string, ManifestEntry>();
  if (!args.dryRun && args.env && !args.force) {
    const prior = fetchPublishedManifest(args.env);
    if (prior) {
      for (const p of prior) priorByTarget.set(`${p.kind}:${p.target}`, p);
      console.log(`published manifest read: ${prior.length} rows already in ${args.env}`);
    } else {
      // Say so. A silent fall-through to "upload everything" is the same shape as
      // a silent skip, just expensive instead of wrong.
      console.log(`could not read the published manifest -- uploading every row`);
    }
  }

  let resized = 0, squaresMade = 0, noSource = 0, skipped = 0;
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
      // Detect once per photo; the box does not depend on the panel size.
      // A hand-placed `focus`/`zoom` still wins -- an operator who has looked at
      // the picture beats a heuristic that has not.
      const box = e.focus || e.zoom ? null : await detectSubjectBox(sharp, src);
      const meta = await sharp(src).metadata();
      const rect = box
        ? subjectCrop(box, meta.width ?? 1, meta.height ?? 1)
        : await extractFor(sharp, src, 1, 1, e, SQUARE_PLACE);

      for (const size of SQUARE_SIZES) {
        const scrim = Buffer.from(scrimRGBA(size, size));
        const crop = await sharp(src).extract(rect).toBuffer();
        // Fit the WHOLE crop inside the square, then fill the remainder with a
        // blurred, darkened cover of the same crop. Never letterboxed, never
        // clipped beyond what subjectCrop already decided to give up -- and the
        // fill measured BETTER for text contrast than the photograph it replaced
        // (9.0:1 worst case against a 4.5:1 target; see the sourcing playbook).
        const fitted = await sharp(crop).resize(size, size, { fit: "inside" }).toBuffer();
        const fm = await sharp(fitted).metadata();
        const bg = await sharp(crop)
          .resize(size, size, { fit: "cover" })
          .blur(Math.max(4, Math.round(size / 13)))
          .modulate({ brightness: 0.5 })
          .toBuffer();
        const buf = await sharp(bg)
          .composite([
            {
              input: fitted,
              left: Math.round((size - (fm.width ?? size)) / 2),
              top: Math.round((size - (fm.height ?? size)) / 2),
            },
            { input: scrim, raw: { width: size, height: size, channels: 4 }, blend: "over" },
          ])
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

    // Every key this row would write, derived BEFORE any upload decision. Hashing
    // is free next to a KV round trip, and the skip check needs the square keys as
    // much as the rectangle's.
    const squareKeys: Record<string, string> = {};
    const squareBufs: { size: number; key: string; buf: Buffer }[] = [];
    for (const s of squares) {
      const sKey = await deriveBlobKey(e.target, new Uint8Array(s.buf));
      squareKeys[String(s.size)] = sKey;
      squareBufs.push({ size: s.size, key: sKey, buf: s.buf });
    }
    e.squareKeys = squareKeys;

    const sq = squares.map((s) => `${s.size}:${s.buf.length}B`).join(" ");
    if (args.dryRun || !args.env) {
      console.log(`${e.kind}:${e.target} -> ${blobKey} (${jpeg.length} B)${sq ? `  square ${sq}` : ""}`);
      continue;
    }

    // ALREADY THERE: nothing to do. Content-addressed keys make this provable
    // rather than a guess -- identical keys mean identical bytes, so the writes
    // would be no-ops. The row still goes into the republished manifest.
    if (alreadyPublished(priorByTarget.get(`${e.kind}:${e.target}`), blobKey, squareKeys)) {
      skipped++;
      continue;
    }
    console.log(`${e.kind}:${e.target} -> ${blobKey} (${jpeg.length} B)${sq ? `  square ${sq}` : ""}`);

    const blobPath = join(tmp, `${blobKey.replace(/[^a-z0-9]/gi, "_")}.jpg`);
    writeFileSync(blobPath, jpeg);
    wranglerPut(args.env, blobKey, { path: blobPath }); // immutable blob
    wranglerPut(args.env, ptr, { value: blobKey }); // pointer flip

    // Square variants LAST, and each blob strictly before its pointer. A run
    // interrupted anywhere leaves the legacy pointer already flipped and correct,
    // and a square pointer that exists always resolves to a blob that exists --
    // so a half-finished ingest degrades to "some devices still get rectangles",
    // never to a dangling pointer or a clipped card.
    for (const s of squareBufs) {
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
      const sPath = join(tmp, `${s.key.replace(/[^a-z0-9]/gi, "_")}.jpg`);
      writeFileSync(sPath, s.buf);
      wranglerPut(args.env, s.key, { path: sPath });
      wranglerPut(args.env, pointerKey(e.kind, e.target, s.size), { value: s.key });
    }
  }

  // A LAST LINE THAT STATES WHAT ACTUALLY HAPPENED (#207). The per-row output is
  // 213 lines long, so anything important said at the top is gone by the end. The
  // counts are what a reader would otherwise have to reconstruct by scrolling --
  // and "0 resized" is the shape of every silent-skip bug this script has had.
  console.log(
    `summary: ${resized} resized, ${squaresMade} square variant(s), ` +
      `${noSource} row(s) with no source file` +
      (skipped ? `, ${skipped} unchanged and SKIPPED (${skipped * 4} KV writes avoided)` : "") +
      (args.square ? "" : "  [--no-square: square variants were NOT built]") +
      (args.force ? "  [--force: skip check bypassed]" : "") +
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
