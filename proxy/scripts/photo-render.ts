/**
 * photo-render.ts -- the ONE place a stock photo becomes device bytes.
 *
 * Moved verbatim out of ingest-photos.ts so the ingest and the photo dashboard
 * render identically. Blobs are content-addressed: if these two ever drifted, the
 * dashboard would preview a crop no device shows, and any edit here changes the
 * key of every row -- a dry run against production must report 0 CHANGED after
 * any refactor of this file.
 */
import { cropRect, scrimRGBA, subjectCrop, type Framing, type SubjectBox } from "../src/framing";
import { isBaselineJpeg } from "../src/photolicense";

// LEGACY slot dims. Every device shipped up to FW 6 draws a 150x100 photo into a
// fixed slot, and its drawJpg call site passes no scale -- maxWidth/maxHeight
// CLIP rather than shrink -- so anything larger renders as its own top-left
// corner. This variant must keep existing, unchanged, for as long as one of those
// devices is in the field. Baseline (non-progressive) JPEG only: the on-device
// decoder (LovyanGFX drawJpg via TJpgDec) cannot decode progressive.
export const PHOTO_W = 150;
export const PHOTO_H = 100;

// SQUARE (full-bleed) dims, one per distinct panel size across the SKUs. The
// card became the whole disc in FW 7 (issue #209), so the artifact is the panel:
// 240 = Kit S3 / the retired C3 form factor, 412 = the 1.46B, 480 = the Pro 2.1.
//
// Emitted per size rather than emitted once and scaled on-device. drawJpg CAN
// scale (the float scale_x/scale_y overload; the jpeg_div_t one is deprecated),
// so this is a choice: upscaling a 240 artifact to 480 throws away exactly the
// detail a bigger panel exists to show, and the crop aspect is identical across
// sizes so there is nothing else to gain by sharing one.
export const SQUARE_SIZES = [240, 412, 480] as const;

// Where the subject sits vertically in the square. Below 0.5 lifts it, keeping
// the aeroplane clear of the callsign band along the bottom.
export const SQUARE_PLACE = 0.38;

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
export async function detectSubjectBox(
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
export async function extractFor(
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

export interface RenderRow {
  kind: string;
  target: string;
  focus?: [number, number];
  zoom?: number;
}

// The legacy 150x100 rectangle. Moved here unchanged from ingest-photos.ts's
// main loop, so the ingest and the dashboard's preview produce the SAME bytes --
// and therefore the same content-addressed keys. A preview made by separate code
// would only prove the separate code works.
export async function renderRect(sharp: typeof import("sharp"), src: Buffer, e: RenderRow): Promise<Buffer> {
  const jpeg = await sharp(src)
    .extract(await extractFor(sharp, src, PHOTO_W, PHOTO_H, e))
    .resize(PHOTO_W, PHOTO_H, { fit: "cover" })
    .jpeg({ progressive: false, quality: 82 })
    .toBuffer();
  if (!isBaselineJpeg(jpeg)) {
    throw new Error(`${e.kind}:${e.target}: encoded JPEG is not baseline (progressive?); aborting`);
  }
  return jpeg;
}

// The full-bleed squares, one per size. Also moved unchanged; see the ingest for
// why the scrim is baked in and why the whole crop is fitted rather than clipped.
export async function renderSquares(
  sharp: typeof import("sharp"),
  src: Buffer,
  e: RenderRow,
  sizes: readonly number[] = SQUARE_SIZES,
): Promise<{ size: number; buf: Buffer }[]> {
  const squares: { size: number; buf: Buffer }[] = [];
  // Detect once per photo; the box does not depend on the panel size.
  // A hand-placed `focus`/`zoom` still wins -- an operator who has looked at
  // the picture beats a heuristic that has not.
  const box = e.focus || e.zoom ? null : await detectSubjectBox(sharp, src);
  const meta = await sharp(src).metadata();
  const rect = box
    ? subjectCrop(box, meta.width ?? 1, meta.height ?? 1)
    : await extractFor(sharp, src, 1, 1, e, SQUARE_PLACE);

  for (const size of sizes) {
    const scrim = Buffer.from(scrimRGBA(size, size));
    const crop = await sharp(src).extract(rect).toBuffer();
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
  return squares;
}
