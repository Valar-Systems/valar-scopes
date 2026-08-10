// Photo framing: the pure geometry behind the round-panel crop. No sharp, no
// filesystem, so the ingest script (Node) and vitest can share one implementation.
// The compositing half lives in scripts/ingest-photos.ts.
//
// WHY THIS EXISTS. A stock aircraft photo is composed for a rectangle: apron,
// sky and runway are most of the pixels, and the aeroplane is a band across the
// middle. A plain cover-crop faithfully preserves that composition, which is the
// wrong one for a 130 px disc -- measured on a real batch, a centre crop left two
// of three test aircraft unidentifiable. `focus` and `zoom` are the two knobs
// that fix it, and they are per-photo because no single setting works for all:
// a global zoom tuned to one photo is what produced those two failures.
//
// Both default to the safe value, so a row that specifies neither behaves
// exactly as it did before these fields existed.

export interface Framing {
  /** Subject centre in the SOURCE image, 0..1. Defaults to the centre. */
  focus?: [number, number];
  /** How far to push in past a plain cover-crop. Defaults to 1 (no push). */
  zoom?: number;
  /**
   * Where the subject lands vertically in the OUTPUT, 0..1. Defaults to centred.
   * Below 0.5 lifts it, which is what keeps a callsign off the fuselage on the
   * full-bleed card -- centring the subject and then writing text across the
   * middle puts the type badge on top of the thing the card exists to show.
   */
  place?: number;
}

export interface CropRect {
  left: number;
  top: number;
  width: number;
  height: number;
}

const clamp = (v: number, lo: number, hi: number) => (v < lo ? lo : v > hi ? hi : v);

/**
 * The region of the source to take, in source pixels, for a cover-crop to
 * outW x outH honouring `framing`.
 *
 * Always returns a rect fully inside the source: a focus near an edge slides the
 * window back in bounds rather than sampling past it, so an off-centre subject
 * degrades to "as close as the frame allows" instead of producing black bars.
 */
export function cropRect(
  srcW: number,
  srcH: number,
  outW: number,
  outH: number,
  framing: Framing = {},
): CropRect {
  if (srcW <= 0 || srcH <= 0 || outW <= 0 || outH <= 0) {
    throw new Error("cropRect: dimensions must be positive");
  }
  const zoom = Math.max(1, framing.zoom ?? 1);
  const [fx, fy] = framing.focus ?? [0.5, 0.5];
  const place = clamp(framing.place ?? 0.5, 0, 1);

  // Largest region of the source with the output's aspect ratio (= cover), then
  // divided by zoom. Rounded up so the extract never lands a sub-pixel short of
  // the aspect and forces a squash on the resize.
  const aspect = outW / outH;
  let w = Math.min(srcW, srcH * aspect);
  let h = w / aspect;
  w = Math.min(srcW, Math.ceil(w / zoom));
  h = Math.min(srcH, Math.ceil(h / zoom));

  // `place` positions the focus within the window: 0.5 centres it, 0.3 puts it
  // three tenths down, which lifts the subject in the output.
  const left = Math.round(clamp(fx, 0, 1) * srcW - w / 2);
  const top = Math.round(clamp(fy, 0, 1) * srcH - place * h);

  return {
    left: Math.round(clamp(left, 0, srcW - w)),
    top: Math.round(clamp(top, 0, srcH - h)),
    width: Math.round(w),
    height: Math.round(h),
  };
}

// ---------------------------------------------------------------------------
// Scrim: the graded darkening baked into the bottom of a full-bleed card.
//
// SIZED TO A CONTRAST TARGET, NOT TO TASTE. The card's green (#56EB3C) has a
// relative luminance of 0.617, so for 4.5:1 whatever sits behind it must come in
// at or below 0.098 -- which a pure-white pixel only reaches at alpha >= 0.654.
// PEAK sits above that so the guarantee survives a blown highlight.
//
// Verified against the most hostile photos in the live library (chosen by ranking
// every one of them on luminance in the rows the text occupies, not by eye): peak
// luminance under the text drops from 0.62-1.00 to 0.022-0.032, i.e. >= 8.1:1
// measured on the WORST SINGLE PIXEL, inside the disc. A mean would have passed on
// the one photo with a white fuselage crossing the callsign, which is precisely
// the aircraft that would have found this in the field.
//
// THE RAMP IS SOLVED AGAINST THE CARD'S TEXT, so it moved when the text did. It
// began at 0.40 when the full-bleed page carried four lines (callsign, type,
// operator, registration, telemetry) reaching up to y=140 of 240. That page now
// carries ONE line: the photo already answers type/operator/route, which are on
// the data page, and the callsign is the only thing a photograph cannot tell you.
// One line means the darkening starts at 0.66 instead of 0.40 -- 163 unobstructed
// rows of aeroplane instead of 96, a 70% gain, at identical worst-case contrast.
//
// If the card's text layout changes again, THESE NUMBERS ARE PART OF THAT CHANGE.
// A shorter ramp reaching full strength at a different height over a different
// region of the photograph is a different measurement, and inheriting the old
// one describes pixels the text is no longer on. FULLBLEED_TITLE_Y in
// src/AircraftManager.cpp is the first row that must be fully covered.
export const SCRIM_RAMP_START = 0.66; // fraction of height where darkening begins
export const SCRIM_RAMP_END = 0.80; // full strength by y=192 of 240; text starts at 194
export const SCRIM_PEAK = 0.8; // maximum alpha

/** Scrim alpha (0..1) for output row `y` of `h`. Smoothstep, so there is no seam. */
export function scrimAlpha(
  y: number,
  h: number,
  rampStart = SCRIM_RAMP_START,
  rampEnd = SCRIM_RAMP_END,
  peak = SCRIM_PEAK,
): number {
  if (h <= 0) return 0;
  const a = rampStart * h;
  const b = rampEnd * h;
  if (y <= a) return 0;
  if (y >= b || b <= a) return peak;
  const t = (y - a) / (b - a);
  return peak * (t * t * (3 - 2 * t));
}

/** Row-major RGBA bytes for the scrim overlay: black, alpha from `scrimAlpha`. */
export function scrimRGBA(w: number, h: number): Uint8Array {
  const out = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    const a = Math.round(255 * scrimAlpha(y, h));
    for (let x = 0; x < w; x++) out[(y * w + x) * 4 + 3] = a;
  }
  return out;
}

// ---- subject-aware square framing (2026-08-10) -------------------------------
//
// A 3:2 photograph cover-cropped to 1:1 keeps 67% of its width, and on an
// airliner that missing third is the nose and the tail. Measured across the
// library: today's crop keeps 49% of a 777 and 53% of a 737-9. A customer looking
// at a card sees a fuselage section, which identifies nothing.
//
// The fix is not "fit the whole frame" -- that shrinks a 777 from 23% of the disc
// to 12% and turns 58% of it into filler, trading a legible half-aeroplane for an
// illegible whole one. It is to crop to the SUBJECT and then spend sky:
//
//   1. crop tight to the subject's bounding box (+ a small margin)
//   2. GROW VERTICALLY into the sky until the box is square enough that the
//      leftover fill is within MAX_FILL. Sky is free space -- spending it costs
//      nothing, so it is spent first.
//   3. only when the sky runs out, narrow horizontally and accept clipping a
//      wingtip.
//
// The remainder is filled with a blurred, darkened copy of the same crop, so the
// disc is never empty and the card never letterboxes.
export const SQUARE_MAX_FILL = 0.35;

/** Normalised subject bounding box, all values 0..1 of the source dimensions. */
export interface SubjectBox {
  x0: number;
  x1: number;
  y0: number;
  y1: number;
}

/**
 * The source rectangle to take for a square variant, given the subject's box.
 *
 * Pure geometry so vitest can reach it without sharp -- detection needs pixels
 * and lives in the ingest; this decides what to do with the answer.
 */
export function subjectCrop(
  box: SubjectBox,
  srcW: number,
  srcH: number,
  maxFill: number = SQUARE_MAX_FILL,
  pad = 0.06,
): { left: number; top: number; width: number; height: number } {
  const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));
  let left = clamp(Math.round((box.x0 - pad) * srcW), 0, srcW - 1);
  const right = clamp(Math.round((box.x1 + pad) * srcW), left + 1, srcW);
  let top = clamp(Math.round((box.y0 - pad) * srcH), 0, srcH - 1);
  let bottom = clamp(Math.round((box.y1 + pad) * srcH), top + 1, srcH);
  let w = Math.max(16, right - left);
  let h = Math.max(16, bottom - top);

  // 2. grow into the sky
  const wantH = Math.round(w * (1 - maxFill));
  if (h < wantH) {
    const grow = Math.min(wantH - h, srcH - h);
    top = clamp(top - Math.round(grow / 2), 0, srcH - h - grow >= 0 ? srcH - h - grow : 0);
    bottom = Math.min(srcH, top + h + grow);
    h = bottom - top;
  }
  // 3. only now clip
  if (h / w < 1 - maxFill) {
    const maxW = Math.round(h / (1 - maxFill));
    const cx = left + w / 2;
    left = clamp(Math.round(cx - maxW / 2), 0, Math.max(0, srcW - 1));
    w = Math.min(maxW, srcW - left);
  }
  return { left, top, width: Math.max(1, w), height: Math.max(1, h) };
}
