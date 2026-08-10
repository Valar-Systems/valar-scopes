/**
 * audit-photo-subject.ts -- find library photos whose subject is too small to read.
 *
 * WHY. The licence gate proves a photo may be used. Nothing proved it is any GOOD,
 * and on 2026-08-10 that gap produced a PA24 card showing an aeroplane about 40 px
 * across on a 240 px disc -- a speck in an ocean of sky. It passed every check we
 * had, because every check we had was about rights and encoding.
 *
 * It was caught by accident, from the encoded size: 1 KB where its siblings took
 * 3-5 KB. Flat sky compresses to almost nothing, so a small file IS the signal.
 * This turns that accident into a sweep.
 *
 * THE METRIC THAT MATTERS IS `subj` -- the fraction of the square the aeroplane
 * actually occupies.
 *
 * The first version of this script ranked on encoded bytes and luminance stddev,
 * because that is what caught PA24. Rendering its top hits proved that wrong: it
 * flags a FLAT BACKGROUND, which is not the same thing as a small subject, and a
 * clean in-flight shot against open sky is the single best kind of photo for this
 * product -- the scrim has nothing to fight. Eight "worst" hits came back and six
 * were good photos. A metric that would have had us replace good photos is worse
 * than no metric, so it is no longer the ranking.
 *
 * `subj` estimates the background PER ROW, from the outer pixels at each end of
 * that row, then counts pixels far enough from it in RGB. Per row rather than one
 * global colour because sky is a vertical gradient, and a single background colour
 * scores the top of the frame as subject.
 *
 * bytes and stddev are still reported, as corroboration and because the PA24 catch
 * came from bytes. But they are secondary now.
 *
 * THIS SCRIPT REJECTS NOTHING -- it produces a shortlist a human looks at, the same
 * contract suggest-commons has, and for the same reason: the judgement is visual.
 *
 *   npx tsx scripts/audit-photo-subject.ts            # bottom 20 by each metric
 *   npx tsx scripts/audit-photo-subject.ts --all      # every row, sorted
 *   npx tsx scripts/audit-photo-subject.ts --top 40   # a longer shortlist
 */
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import type { ManifestEntry } from "../src/photolicense";

const PHOTOS_DIR = "photos";
const SIZE = 240; // the smallest panel -- worst case for a subject being legible
const SQUARE_PLACE = 0.38; // must match ingest-photos.ts

interface Row {
  target: string;
  kind: string;
  subj: number; // fraction of the square occupied by non-background pixels
  bytes: number;
  stddev: number;
  srcW: number;
  srcH: number;
}

// Fraction of the frame that is NOT background, estimating the background per row.
//
// Per row is the whole trick. Sky is a vertical gradient, so one global background
// colour marks the top of the frame as "subject" and reports 40% on a photo that is
// nothing but sky. Each row's own outer pixels are a good local estimate, and a
// centred aeroplane never reaches them.
const EDGE = 6; // pixels sampled at each end of a row
const DIST = 42; // RGB euclidean distance beyond which a pixel counts as subject
function subjectFraction(data: Buffer, w: number, h: number, channels: number): number {
  let hits = 0;
  const med = (xs: number[]) => xs.slice().sort((a, b) => a - b)[Math.floor(xs.length / 2)] ?? 0;
  for (let y = 0; y < h; y++) {
    const rs: number[] = [], gs: number[] = [], bs: number[] = [];
    for (let k = 0; k < EDGE; k++) {
      for (const x of [k, w - 1 - k]) {
        const i = (y * w + x) * channels;
        rs.push(data[i] ?? 0); gs.push(data[i + 1] ?? 0); bs.push(data[i + 2] ?? 0);
      }
    }
    const br = med(rs), bg = med(gs), bb = med(bs);
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * channels;
      const dr = (data[i] ?? 0) - br, dg = (data[i + 1] ?? 0) - bg, db = (data[i + 2] ?? 0) - bb;
      if (Math.sqrt(dr * dr + dg * dg + db * db) > DIST) hits++;
    }
  }
  return hits / (w * h);
}

async function main(): Promise<void> {
  const argv = process.argv.slice(2);
  const all = argv.includes("--all");
  const topIdx = argv.indexOf("--top");
  const top = topIdx >= 0 ? Number.parseInt(argv[topIdx + 1] ?? "20", 10) : 20;

  let sharp: typeof import("sharp");
  try {
    sharp = (await import("sharp")).default as unknown as typeof import("sharp");
  } catch {
    // Same rule as the ingest (#207): an audit that cannot look at the pixels is
    // not a partial audit, it is no audit. Refuse rather than report on nothing.
    throw new Error("sharp is required for this audit: npm i -D sharp");
  }

  const manifest: ManifestEntry[] = JSON.parse(
    readFileSync(join(PHOTOS_DIR, "manifest.json"), "utf8"),
  );

  const rows: Row[] = [];
  let missing = 0;
  for (const e of manifest) {
    if (!e.file || !existsSync(join(PHOTOS_DIR, e.file))) {
      missing++;
      continue;
    }
    const src = join(PHOTOS_DIR, e.file);
    const meta = await sharp(src).metadata();
    const w = meta.width ?? 0, h = meta.height ?? 0;
    if (!w || !h) {
      missing++;
      continue;
    }
    // Approximates the ingest's square geometry: cover-crop to 1:1 with the
    // subject lifted clear of the callsign band. Approximate is fine -- this
    // ranks candidates for a human, it does not gate anything.
    const side = Math.min(w, h);
    const left = Math.round((w - side) / 2);
    const top2 = Math.max(0, Math.min(h - side, Math.round(h * SQUARE_PLACE - side / 2)));
    const pipeline = sharp(src).extract({ left, top: top2, width: side, height: side }).resize(SIZE, SIZE, { fit: "cover" });

    const buf = await pipeline.clone().jpeg({ progressive: false, quality: 82 }).toBuffer();
    const stats = await pipeline.clone().greyscale().stats();
    const raw = await pipeline.clone().removeAlpha().raw().toBuffer({ resolveWithObject: true });
    rows.push({
      target: e.target,
      kind: e.kind,
      subj: subjectFraction(raw.data, raw.info.width, raw.info.height, raw.info.channels),
      bytes: buf.length,
      stddev: stats.channels[0]?.stdev ?? 0,
      srcW: w,
      srcH: h,
    });
  }

  const fmt = (r: Row) =>
    `subj=${(r.subj * 100).toFixed(1).padStart(5)}%  ${String(r.bytes).padStart(6)} B  ` +
    `sd=${r.stddev.toFixed(1).padStart(5)}  ` + `${r.srcW}x${r.srcH}`.padEnd(11) + `  ${r.kind}:${r.target}`;

  const bySubj = [...rows].sort((a, b) => a.subj - b.subj);
  if (all) {
    bySubj.forEach((r) => console.log(fmt(r)));
  } else {
    console.log(`\n== smallest ${top} SUBJECTS -- the shortlist ==`);
    bySubj.slice(0, top).forEach((r) => console.log(fmt(r)));
    console.log(`\n== for reference, the ${Math.min(5, rows.length)} LARGEST subjects ==`);
    bySubj.slice(-5).reverse().forEach((r) => console.log(fmt(r)));
  }

  const medS = bySubj[Math.floor(rows.length / 2)];
  console.log(
    `\n${rows.length} photos measured, ${missing} skipped (no source file). ` +
      `Median subject ${((medS?.subj ?? 0) * 100).toFixed(1)}% of the ${SIZE}px square.`,
  );
  console.log("Nothing was rejected: this is a shortlist to LOOK at, not a verdict.");
}

main().catch((err: unknown) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
