import { describe, expect, it } from "vitest";
import {
  cropRect,
  scrimAlpha,
  scrimRGBA,
  SCRIM_PEAK,
  SCRIM_RAMP_END,
  SCRIM_RAMP_START,
} from "../src/framing";

describe("cropRect", () => {
  it("defaults to a plain centre cover-crop, which is what an unset row must keep doing", () => {
    // 1200x800 -> 150x100: same aspect, so the whole frame is taken.
    expect(cropRect(1200, 800, 150, 100)).toEqual({ left: 0, top: 0, width: 1200, height: 800 });
    // 1200x800 -> 240x240: a square out of the middle.
    const r = cropRect(1200, 800, 240, 240);
    expect(r.width).toBe(800);
    expect(r.height).toBe(800);
    expect(r.left).toBe(200); // centred horizontally
  });

  it("moves the window to the focus point", () => {
    const centre = cropRect(1200, 800, 240, 240);
    const left = cropRect(1200, 800, 240, 240, { focus: [0.25, 0.5] });
    expect(left.left).toBeLessThan(centre.left);
    expect(left.width).toBe(centre.width); // focus moves the window, never resizes it
  });

  it("zoom shrinks the source region, which is what enlarges the subject", () => {
    const one = cropRect(1200, 800, 240, 240, { zoom: 1 });
    const two = cropRect(1200, 800, 240, 240, { zoom: 2 });
    expect(two.width).toBeLessThan(one.width);
    expect(two.width / two.height).toBeCloseTo(1, 2); // aspect preserved: no squash
  });

  it("zoom below 1 is clamped rather than letterboxing", () => {
    expect(cropRect(1200, 800, 240, 240, { zoom: 0.2 })).toEqual(cropRect(1200, 800, 240, 240));
  });

  it("slides a near-edge focus back in bounds instead of sampling past it", () => {
    // A focus in the corner must still yield a rect fully inside the source --
    // otherwise sharp's extract throws and one bad row aborts the whole ingest.
    for (const focus of [[0, 0], [1, 1], [1, 0], [0, 1], [-3, 4]] as [number, number][]) {
      const r = cropRect(1200, 800, 240, 240, { focus, zoom: 1.4 });
      expect(r.left).toBeGreaterThanOrEqual(0);
      expect(r.top).toBeGreaterThanOrEqual(0);
      expect(r.left + r.width).toBeLessThanOrEqual(1200);
      expect(r.top + r.height).toBeLessThanOrEqual(800);
    }
  });

  it("`place` lifts the subject without changing the window size", () => {
    const centred = cropRect(1200, 900, 240, 240, { focus: [0.5, 0.5], zoom: 1.3 });
    const lifted = cropRect(1200, 900, 240, 240, { focus: [0.5, 0.5], zoom: 1.3, place: 0.3 });
    // Taking the window from LOWER in the source puts the subject HIGHER in the output.
    expect(lifted.top).toBeGreaterThan(centred.top);
    expect(lifted.height).toBe(centred.height);
  });

  it("rejects non-positive dimensions rather than emitting a bad extract", () => {
    expect(() => cropRect(0, 800, 240, 240)).toThrow();
    expect(() => cropRect(1200, 800, 240, 0)).toThrow();
  });
});

describe("scrimAlpha", () => {
  it("is fully transparent above the ramp and fully opaque below it", () => {
    expect(scrimAlpha(0, 240)).toBe(0);
    expect(scrimAlpha(SCRIM_RAMP_START * 240, 240)).toBe(0);
    expect(scrimAlpha(SCRIM_RAMP_END * 240, 240)).toBeCloseTo(SCRIM_PEAK, 5);
    expect(scrimAlpha(239, 240)).toBeCloseTo(SCRIM_PEAK, 5);
  });

  it("rises monotonically, so there is no visible band edge", () => {
    let prev = -1;
    for (let y = 0; y < 240; y++) {
      const a = scrimAlpha(y, 240);
      expect(a).toBeGreaterThanOrEqual(prev);
      prev = a;
    }
  });

  it("reaches full strength BEFORE the first text row", () => {
    // The full-bleed card draws its first glyph at FULLBLEED_TITLE_Y =
    // SCREEN_SIZE - 46, i.e. y=194 of 240 (src/AircraftManager.cpp). If the ramp
    // were still climbing there, the callsign would sit on a lighter background
    // than the contrast target assumes -- the guarantee is only worth anything if
    // it holds where the text actually is.
    //
    // This test is the coupling between the two files. It failed when the ramp
    // moved for the one-line layout and had to be re-derived rather than nudged,
    // which is the point: the constant is solved against the layout, not chosen.
    expect(scrimAlpha(194, 240)).toBeCloseTo(SCRIM_PEAK, 5);
    // ...and the last row any glyph occupies (the "tap: details" hint) too.
    expect(scrimAlpha(226, 240)).toBeCloseTo(SCRIM_PEAK, 5);
  });

  it("leaves the upper two thirds of the photograph completely untouched", () => {
    // The reason the one-line layout exists. Anything above the ramp must be
    // exactly zero -- not "nearly zero" -- or the aeroplane is being dimmed for
    // text that is not there.
    for (const y of [0, 60, 120, 157]) expect(scrimAlpha(y, 240)).toBe(0);
    expect(scrimAlpha(158, 240)).toBe(0); // 0.66 * 240, the last clear row
    expect(scrimAlpha(159, 240)).toBeGreaterThan(0);
  });

  it("holds the contrast target against a pure-white photo, on every text row", () => {
    // The whole reason PEAK is what it is. sRGB white behind alpha `a` composites
    // to (1-a); relative luminance of that must be <= 0.098 for 4.5:1 against the
    // card's green (luminance 0.617).
    //
    // Swept across the WHOLE text band rather than sampled at one row. The single
    // sample this replaced was taken at y=160 -- correct while the four-line
    // layout put text there, and silently meaningless the moment the ramp moved,
    // because y=160 is now two rows into the ramp and carries no glyphs. It
    // failed when the geometry changed, which is the only reason the new numbers
    // were derived instead of assumed.
    for (let y = 194; y <= 226; y++) {
      const srgb = 1 - scrimAlpha(y, 240);
      const lum = srgb <= 0.04045 ? srgb / 12.92 : ((srgb + 0.055) / 1.055) ** 2.4;
      const ratio = (0.617 + 0.05) / (lum + 0.05);
      expect(ratio, `row ${y}`).toBeGreaterThanOrEqual(4.5);
    }
  });
});

describe("scrimRGBA", () => {
  it("is black with a vertical alpha ramp and nothing else", () => {
    const w = 4;
    const h = 240;
    const buf = scrimRGBA(w, h);
    expect(buf.length).toBe(w * h * 4);
    for (let y = 0; y < h; y++) {
      const i = y * w * 4;
      expect(buf[i]).toBe(0); // R
      expect(buf[i + 1]).toBe(0); // G
      expect(buf[i + 2]).toBe(0); // B
      expect(buf[i + 3]).toBe(Math.round(255 * scrimAlpha(y, h)));
    }
    expect(buf[3]).toBe(0); // top row untouched
    expect(buf[(h - 1) * w * 4 + 3]).toBe(Math.round(255 * SCRIM_PEAK));
  });
});
