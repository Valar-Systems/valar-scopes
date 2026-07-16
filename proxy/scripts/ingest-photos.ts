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
import {
  MANIFEST_KEY,
  deriveBlobKey,
  isBaselineJpeg,
  pointerKey,
  renderCreditsHtml,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";

// Device sprite dims -- keep in sync with PHOTO_W / PHOTO_H in
// src/AircraftManager.cpp. Baseline (non-progressive) JPEG only: the on-device
// decoder (LovyanGFX drawJpg) will not decode progressive.
const PHOTO_W = 150;
const PHOTO_H = 100;

interface Args {
  env?: string;
  dryRun: boolean;
  checkOnly: boolean;
  photosDir: string;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false, checkOnly: false, photosDir: "photos" };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--env") a.env = argv[++i];
    else if (v === "--dry-run") a.dryRun = true;
    else if (v === "--check") a.checkOnly = true;
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

function wranglerPut(env: string, key: string, opts: { value?: string; path?: string }): void {
  const parts = ["npx", "wrangler", "kv", "key", "put", q(key)];
  if (opts.value !== undefined) parts.push(q(opts.value));
  if (opts.path !== undefined) parts.push("--path", q(opts.path));
  parts.push("--binding=ENRICH_KV", `--env=${env}`, "--remote");
  execSync(parts.join(" "), { stdio: "inherit" });
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
  let sharp: typeof import("sharp") | undefined;
  if (!args.dryRun) {
    try {
      sharp = (await import("sharp")).default as unknown as typeof import("sharp");
    } catch {
      throw new Error("sharp is required for uploads: npm i -D sharp");
    }
  }

  for (const e of entries) {
    if (!e.file) {
      console.warn(`skip ${e.kind}:${e.target}: no source file`);
      continue;
    }
    const src = readFileSync(join(args.photosDir, e.file));
    // Resize to the exact sprite, cover-crop, baseline JPEG, metadata (EXIF) dropped.
    // NO mozjpeg: sharp's `mozjpeg: true` preset force-enables PROGRESSIVE encoding
    // (overriding `progressive: false`), and the on-device decoder (TJpgDec via
    // LovyanGFX drawJpg) cannot decode progressive -- the failure is a silent
    // "No photo available" after a successful 200 (found the hard way on the bench).
    const jpeg = sharp
      ? await sharp(src)
          .resize(PHOTO_W, PHOTO_H, { fit: "cover" })
          .jpeg({ progressive: false, quality: 82 })
          .toBuffer()
      : Buffer.from(src); // --dry-run: hash the source so keys are stable-ish for logging

    // Hard assertion: refuse to upload anything but a baseline JPEG (SOF0/SOF1).
    // SOF2 = progressive = undecodable on-device; guard here so no encoder-option
    // drift can ever ship a poison blob again.
    if (sharp && !isBaselineJpeg(jpeg)) {
      throw new Error(`${e.kind}:${e.target}: encoded JPEG is not baseline (progressive?); aborting`);
    }

    const blobKey = await deriveBlobKey(e.target, new Uint8Array(jpeg));
    e.blobKey = blobKey;
    const ptr = pointerKey(e.kind, e.target);
    console.log(`${e.kind}:${e.target} -> ${blobKey} (${jpeg.length} B)`);

    if (args.dryRun || !args.env) continue;
    const blobPath = join(tmp, `${blobKey.replace(/[^a-z0-9]/gi, "_")}.jpg`);
    writeFileSync(blobPath, jpeg);
    wranglerPut(args.env, blobKey, { path: blobPath }); // immutable blob
    wranglerPut(args.env, ptr, { value: blobKey }); // pointer flip
  }

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
