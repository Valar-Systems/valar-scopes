/**
 * make-fullbleed-demo.ts -- BENCH TOOL, not part of the ingest pipeline.
 *
 * Renders the full-bleed round card (issue #209) at TRUE 240x240, round-masked,
 * in two competing text treatments, and measures the scrim's contrast guarantee
 * separately for each. Output: a contact sheet to judge from, and a C header the
 * demo firmware embeds so the same thing can be seen on real glass.
 *
 * THE TWO TREATMENTS
 *
 *   FOUR  -- callsign, type, operator, reg, and two telemetry lines over the
 *            photo. Needs a tall scrim (ramp from 40% of height) because text
 *            reaches up to y=140.
 *   ONE   -- callsign only. Everything the photograph already answers (type,
 *            operator, route) is demoted to the card's EXISTING data page. One
 *            line means the scrim collapses to a band at the bottom (ramp from
 *            68%), so the photo is unobstructed for 163 rows instead of 96.
 *
 * WHY THE CONTRAST IS MEASURED TWICE, NOT INHERITED. The 8.1:1 measured for the
 * four-line layout says nothing about the one-line layout: it is a shorter ramp,
 * reaching full strength at a different height, over a DIFFERENT REGION OF THE
 * PHOTOGRAPH. Sky in rows 140-230 tells you nothing about what is in rows
 * 200-222 -- that is where fuselage, runway and landing lights live. Each
 * treatment is measured on its own rows, on the worst single pixel, on every
 * photo in the set.
 *
 * It imports cropRect()/scrimAlpha() from src/framing.ts -- the SAME geometry
 * the ingest will use -- deliberately. A mock that re-implemented the crop would
 * be showing a design that is not the one that ships, which is exactly the
 * failure the playbook's "the check operates on the artifact that ships" rule
 * exists to prevent.
 *
 *   npx tsx scripts/make-fullbleed-demo.ts
 */
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import sharp from "sharp";
import { cropRect, scrimAlpha } from "../src/framing";

const OUT = 240;
const PHOTOS = "photos/src";

// The card's green (#56EB3C). The scrim's peak alpha was solved against this
// colour's relative luminance; using another colour invalidates the guarantee.
const GREEN_LUM = 0.617;
const TARGET_CONTRAST = 4.5;

// place < 0.5 lifts the subject, keeping the aeroplane out of the text band.
const PLACE = 0.38;

interface Treatment {
  id: "four" | "one";
  label: string;
  rampStart: number;  // fraction of height where darkening begins
  rampEnd: number;    // where it reaches full strength
  peak: number;       // maximum alpha
  textTop: number;    // first row any glyph occupies
  textBottom: number; // last row any glyph occupies
}

const FOUR: Treatment = {
  id: "four", label: "four lines (current #209 scope)",
  rampStart: 0.40, rampEnd: 0.575, peak: 0.8, textTop: 140, textBottom: 230,
};

// Callsign only, sat low on the disc. At y=222 the chord is 126 px wide, and a
// size-2 "SKW6042" is 84 px -- it fits with room, and longer callsigns are rare
// (ICAO flight ids are <= 8 chars).
const ONE: Treatment = {
  id: "one", label: "callsign only (proposed)",
  rampStart: 0.68, rampEnd: 0.82, peak: 0.8, textTop: 200, textBottom: 222,
};

interface Pick {
  key: string; callsign: string; type: string;
  operator: string; reg: string; line1: string; line2: string;
}

// The set is chosen to be hostile, not flattering: e175/a388/a10 each contain a
// pure-white pixel (luminance 1.000) in the four-line text band, and u2 reaches
// 0.982. Those are the aircraft that would have found a weak scrim in the field.
const PICKS: Pick[] = [
  { key: "e175", callsign: "SKW6042", type: "Embraer E175",      operator: "SkyWest Airlines", reg: "N204SY",  line1: "12,450 ft   288 kt", line2: "14.2 km   HDG 291" },
  { key: "a388", callsign: "UAE231",  type: "Airbus A380-800",   operator: "Emirates",         reg: "A6-EOW",  line1: "37,000 ft   488 kt", line2: "62.8 km   HDG 074" },
  { key: "a10",  callsign: "HOG21",   type: "A-10C Thunderbolt", operator: "U.S. Air Force",   reg: "80-0221", line1: "8,900 ft   301 kt",  line2: "22.4 km   HDG 155" },
  { key: "c172", callsign: "N3160A",  type: "Cessna 172",        operator: "Private",          reg: "N3160A",  line1: "3,200 ft   104 kt",  line2: "6.1 km   HDG 018" },
  { key: "b52",  callsign: "DOOM11",  type: "B-52H Stratofort.", operator: "U.S. Air Force",   reg: "60-0044", line1: "28,000 ft   402 kt", line2: "48.0 km   HDG 262" },
  { key: "u2",   callsign: "DRAGON1", type: "U-2S Dragon Lady",  operator: "U.S. Air Force",   reg: "80-1071", line1: "64,000 ft   373 kt", line2: "35.6 km   HDG 009" },
];

function lum(r: number, g: number, b: number): number {
  const f = (c: number) => {
    const s = c / 255;
    return s <= 0.04045 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  };
  return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b);
}

/** Scrim RGBA for a given treatment's ramp. */
function scrimFor(t: Treatment): Buffer {
  const out = Buffer.alloc(OUT * OUT * 4);
  for (let y = 0; y < OUT; y++) {
    const a = Math.round(255 * scrimAlpha(y, OUT, t.rampStart, t.rampEnd, t.peak));
    for (let x = 0; x < OUT; x++) out[(y * OUT + x) * 4 + 3] = a;
  }
  return out;
}

/**
 * Worst (brightest) pixel in the rows this treatment's glyphs occupy, and only
 * inside the visible disc -- a corner pixel is not on the panel and must not be
 * allowed to fail (or pass) the measurement.
 */
async function peakInBand(png: Buffer, t: Treatment): Promise<number> {
  const { data, info } = await sharp(png).raw().toBuffer({ resolveWithObject: true });
  const ch = info.channels;
  const r = OUT / 2;
  let peak = 0;
  for (let y = t.textTop; y <= Math.min(t.textBottom, info.height - 1); y++) {
    const dy = y - r + 0.5;
    const halfChord = Math.sqrt(Math.max(0, r * r - dy * dy));
    const x0 = Math.max(0, Math.floor(r - halfChord));
    const x1 = Math.min(info.width - 1, Math.ceil(r + halfChord));
    for (let x = x0; x <= x1; x++) {
      const i = (y * info.width + x) * ch;
      const l = lum(data[i], data[i + 1], data[i + 2]);
      if (l > peak) peak = l;
    }
  }
  return peak;
}

const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

/** The card's text, as an SVG overlay. Layout mirrors the firmware demo. */
function textSvg(p: Pick, t: Treatment): Buffer {
  const mono = "font-family='DejaVu Sans Mono, Consolas, monospace'";
  let body: string;
  if (t.id === "one") {
    body = `<text x="120" y="216" ${mono} font-size="22" font-weight="bold" fill="#56EB3C" text-anchor="middle">${esc(p.callsign)}</text>`;
  } else {
    body = `
      <text x="120" y="156" ${mono} font-size="22" font-weight="bold" fill="#56EB3C" text-anchor="middle">${esc(p.callsign)}</text>
      <text x="120" y="174" ${mono} font-size="12" fill="#56EB3C" text-anchor="middle">${esc(p.type)}</text>
      <text x="120" y="187" ${mono} font-size="12" fill="#2E9E20" text-anchor="middle">${esc(p.operator)}</text>
      <text x="120" y="200" ${mono} font-size="12" fill="#2E9E20" text-anchor="middle">${esc(p.reg)}</text>
      <text x="120" y="213" ${mono} font-size="12" fill="#56EB3C" text-anchor="middle">${esc(p.line1)}</text>
      <text x="120" y="226" ${mono} font-size="12" fill="#56EB3C" text-anchor="middle">${esc(p.line2)}</text>`;
  }
  return Buffer.from(`<svg width="${OUT}" height="${OUT}" xmlns="http://www.w3.org/2000/svg">${body}</svg>`);
}

/** Circular mask + green frame ring: the panel is a disc, not a square. */
function discSvg(): Buffer {
  return Buffer.from(
    `<svg width="${OUT}" height="${OUT}" xmlns="http://www.w3.org/2000/svg">` +
    `<defs><mask id="m"><rect width="${OUT}" height="${OUT}" fill="black"/>` +
    `<circle cx="120" cy="120" r="119" fill="white"/></mask></defs>` +
    `<rect width="${OUT}" height="${OUT}" fill="#000" mask="url(#m)" fill-opacity="0"/>` +
    `<circle cx="120" cy="120" r="118" fill="none" stroke="#00C800" stroke-width="1"/>` +
    `</svg>`,
  );
}

async function roundMask(img: Buffer): Promise<Buffer> {
  const mask = Buffer.from(
    `<svg width="${OUT}" height="${OUT}" xmlns="http://www.w3.org/2000/svg">` +
    `<circle cx="120" cy="120" r="119" fill="#fff"/></svg>`,
  );
  return sharp(img)
    .composite([{ input: mask, blend: "dest-in" }, { input: discSvg(), blend: "over" }])
    .png()
    .toBuffer();
}

async function main() {
  const rows: string[] = [];
  const bytes: { key: string; len: number }[] = [];
  const tiles: Record<string, Buffer[]> = { four: [], one: [] };

  const scrims = { four: scrimFor(FOUR), one: scrimFor(ONE) };

  console.log(`disc-masked, worst single pixel inside the disc, target ${TARGET_CONTRAST}:1\n`);
  console.log("type      treatment  band       bare    scrimmed   contrast");
  console.log("-".repeat(66));

  let worst = { id: "", contrast: Infinity };

  for (const p of PICKS) {
    const src = readFileSync(join(PHOTOS, `${p.key}.jpg`));
    const meta = await sharp(src).metadata();
    const rect = cropRect(meta.width!, meta.height!, OUT, OUT, { place: PLACE });
    const base = sharp(src).extract(rect).resize(OUT, OUT, { fit: "cover" });
    const barePng = await base.clone().png().toBuffer();

    const perTreatment: Record<string, Buffer> = {};

    for (const t of [FOUR, ONE]) {
      const scrimmed = await sharp(barePng)
        .composite([{ input: scrims[t.id], raw: { width: OUT, height: OUT, channels: 4 }, blend: "over" }])
        .png()
        .toBuffer();

      // Measured on photo+scrim only -- the glyphs are composited afterwards, so
      // the text can never flatter its own background.
      const bare = await peakInBand(barePng, t);
      const after = await peakInBand(scrimmed, t);
      const contrast = (GREEN_LUM + 0.05) / (after + 0.05);
      if (contrast < worst.contrast) worst = { id: `${p.key}/${t.id}`, contrast };

      console.log(
        `${p.key.padEnd(9)} ${t.id.padEnd(10)} ${String(t.textTop).padStart(3)}-${String(t.textBottom).padEnd(4)} ` +
        `${bare.toFixed(3)}   ${after.toFixed(3)}      ${contrast.toFixed(1)}:1 ` +
        `${contrast >= TARGET_CONTRAST ? "OK" : "*** FAILS ***"}`,
      );
      rows.push(`${p.key}|${t.id}|${bare.toFixed(3)}|${after.toFixed(3)}|${contrast.toFixed(1)}`);

      const withText = await sharp(scrimmed)
        .composite([{ input: textSvg(p, t), blend: "over" }])
        .png()
        .toBuffer();
      const tile = await roundMask(withText);
      tiles[t.id].push(tile);
      perTreatment[t.id] = scrimmed;
    }

    // Encoded size of the artifact this treatment would ship, so the payload cost
    // is a measurement rather than the "10-20 KB, probably" this issue opened with.
    const jpeg = await sharp(perTreatment["one"]).jpeg({ progressive: false, quality: 82 }).toBuffer();
    bytes.push({ key: p.key, len: jpeg.length });
  }

  console.log("-".repeat(66));
  console.log(`worst case across the whole set: ${worst.id} at ${worst.contrast.toFixed(1)}:1`);

  // Contact sheet: one row per treatment, same photo order, so the comparison is
  // vertical and immediate.
  const GAP = 12;
  const W = PICKS.length * OUT + (PICKS.length + 1) * GAP;
  const H = 2 * OUT + 3 * GAP + 40;
  const sheet = sharp({ create: { width: W, height: H, channels: 4, background: "#101410" } });
  const comps: sharp.OverlayOptions[] = [];
  (["four", "one"] as const).forEach((id, r) => {
    tiles[id].forEach((buf, c) => {
      comps.push({ input: buf, left: GAP + c * (OUT + GAP), top: GAP + 20 + r * (OUT + GAP) });
    });
  });
  comps.push({
    input: Buffer.from(
      `<svg width="${W}" height="${H}" xmlns="http://www.w3.org/2000/svg">` +
      `<text x="${GAP}" y="16" font-family="sans-serif" font-size="13" fill="#9fb89f">A — four lines over the photo (scrim from 40% of height)</text>` +
      `<text x="${GAP}" y="${GAP + 20 + OUT + GAP + 14}" font-family="sans-serif" font-size="13" fill="#9fb89f">B — callsign only (scrim from 68%); type / operator / route move to the existing data page</text>` +
      `</svg>`,
    ),
    left: 0, top: 0,
  });
  const sheetBuf = await sheet.composite(comps).png().toBuffer();
  writeFileSync("photos/fullbleed-ab.png", sheetBuf);
  console.log(`\nwrote photos/fullbleed-ab.png (${W}x${H})`);
  console.log(
    `square payload: ${Math.min(...bytes.map((b) => b.len))}-${Math.max(...bytes.map((b) => b.len))} B ` +
    `across ${bytes.length} types (today's 150x100 range is 2,520-4,265 B)`,
  );

  // Deliberately emits NO device artifacts. The real ones come from
  // scripts/ingest-photos.ts --square, through the same framing.ts geometry, and
  // a second producer of the same thing is exactly how a mock and a product drift
  // into showing different pictures.
}

main().catch((e) => { console.error(e); process.exit(1); });
