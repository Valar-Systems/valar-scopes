// WS2-WS4 (docs/wx-server.md), plus one extra check that is not frozen: the encoder equals the spike's.
import { env } from "cloudflare:test";
import { beforeAll, describe, expect, it } from "vitest";
import firmware from "../../src/AircraftManager.cpp?raw";
import { type CellBox, PAD_CELLS, VIEW, type ViewConsts, cropCellBox, encodeBox, quantise, viewDegrees } from "../src/wx/crop";
import { FrameBuilder, type FrameMeta, frameMeta, tileBytes, tileOffset, tiledCellAt, tilesFor } from "../src/wx/grid";
import type { LatestRecord } from "../src/wx/handler";
import { frameKey, latestKey, PRODUCTS } from "../src/wx/products";
import airportsText from "./fixtures/wx-airports.txt";   // a Text module (wrangler's default rule for .txt)
import { apiRequest, call } from "./helpers";
import { cropBox as spikeCropBox, encodeCrop as spikeEncodeCrop } from "./ref/spike-crop";
import { CONUS, flatCellAt, synthCodes } from "./wx-synth";

const cref = PRODUCTS[0]!;
const RANGES = [25, 80, 160, 300];
const airports = airportsText.trim().split(/\r?\n/).map((l) => {
  const [id, lat, lon] = l.trim().split(/\s+/);
  return { id: id!, lat: Number(lat), lon: Number(lon) };
});

const codes = synthCodes();
const cellFlat = flatCellAt(CONUS, codes);
const epoch = Math.floor(Date.now() / 1000) - 60;
const m: FrameMeta = frameMeta("cref", epoch, CONUS);

async function storeFrame() {
  const fb = new FrameBuilder(m);
  for (let r = 0; r < CONUS.nj; r++) fb.setRow(r, codes.subarray(r * CONUS.ni, (r + 1) * CONUS.ni));
  await env.WX!.put(frameKey(cref, epoch), fb.bytes);
  const rec: LatestRecord = { product: "cref", epoch, key: frameKey(cref, epoch), ingestedAt: Date.now(), ...CONUS, tile: m.tile };
  await env.WX!.put(latestKey(cref), JSON.stringify(rec));
}

describe("WS2: crops from R2 tiles equal crops from the flat grid", () => {
  beforeAll(storeFrame);

  it("CONTROL: 50 airports x 4 ranges = 200 cases", () => {
    expect(airports.length).toBe(50);
    expect(airports.every((a) => Number.isFinite(a.lat) && Number.isFinite(a.lon))).toBe(true);
  });

  it("200 / 200 crops through the worker are byte-identical to the flat reference", async () => {
    let same = 0, n = 0;
    for (const a of airports) for (const r of RANGES) {
      n++;
      const res = await call(apiRequest(`/api/v1/blipscope/wx/cref?lat=${a.lat.toFixed(2)}&lon=${a.lon.toFixed(2)}&r=${r}`));
      expect(res.status, `${a.id} r=${r}`).toBe(200);
      const got = new Uint8Array(await res.arrayBuffer());
      const want = encodeBox(CONUS, epoch, cropCellBox(CONUS, quantise(a.lat, a.lon, r)), cellFlat).bytes;
      if (got.length === want.length && got.every((b, i) => b === want[i])) same++;
    }
    console.log(`WS2 identical ${same} / ${n}`);
    expect(same).toBe(200);
  });

  it("SABOTAGE: ty/tx swapped in the tile reader must leave at least one crop differing", async () => {
    let differ = 0;
    for (const a of airports) for (const r of RANGES) {
      const box = cropCellBox(CONUS, quantise(a.lat, a.lon, r));
      const tiles = new Map<string, Uint8Array>();
      for (const [ty, tx] of tilesFor(m, box.r0, box.r0 + box.rows - 1, box.c0, box.c0 + box.cols - 1)) {
        let bytes = new Uint8Array(tileBytes(m.tile)).fill(0xee);           // swapped index off the grid: padding
        if (tx < m.tilesY && ty < m.tilesX) {
          const o = await env.WX!.get(frameKey(cref, epoch), { range: { offset: tileOffset(m, tx, ty), length: tileBytes(m.tile) } });
          bytes = new Uint8Array(await o!.arrayBuffer());
        }
        tiles.set(`${ty}-${tx}`, bytes);
      }
      const got = encodeBox(CONUS, epoch, box, tiledCellAt(m, tiles)).bytes;
      const want = encodeBox(CONUS, epoch, box, cellFlat).bytes;
      if (got.length !== want.length || got.some((b, i) => b !== want[i])) differ++;
    }
    console.log(`WS2 sabotage differing ${differ} / 200`);
    expect(differ).toBeGreaterThan(0);
  });
});

describe("EXTRA (not frozen): the encoder is the spike's", () => {
  it("on the spike's own boxes, 200 / 200 crops equal tools/wx/crop.ts's encodeCrop", () => {
    const g = { codes, ...CONUS, frameEpoch: epoch };
    let same = 0;
    for (const a of airports) for (const r of RANGES) {
      const want = spikeEncodeCrop(g, a.lat, a.lon, r).bytes;
      const got = encodeBox(CONUS, epoch, spikeCropBox(g, a.lat, a.lon, r), cellFlat).bytes;
      if (got.length === want.length && got.every((b, i) => b === want[i])) same++;
    }
    console.log(`EXTRA spike parity ${same} / 200`);
    expect(same).toBe(200);
  });
});

// ---- WS3 / WS4 sweep: lat 20-55 by 0.5, lon -129 to -61 by 1, seven ranges ----------------------------
const SWEEP_R = [1, 25, 80, 160, 222, 300, 500];
function* sweep() {
  for (let i = 0; i <= 70; i++) for (let j = 0; j <= 68; j++) for (const r of SWEEP_R)
    yield { lat: 20 + i * 0.5, lon: -129 + j, r };
}

describe("WS3: at most 4 tile reads per crop", () => {
  const maxTiles = (tile: number) => {
    const mm = frameMeta("cref", epoch, CONUS, tile);
    let max = 0, n = 0;
    for (const s of sweep()) {
      n++;
      const b = cropCellBox(CONUS, quantise(s.lat, s.lon, s.r));
      max = Math.max(max, tilesFor(mm, b.r0, b.r0 + b.rows - 1, b.c0, b.c0 + b.cols - 1).length);
    }
    return { max, n };
  };
  it("512-cell tiles: the maximum is 4", () => {
    const { max, n } = maxTiles(512);
    console.log(`WS3 cases ${n}, max tiles ${max} (512)`);
    expect(n).toBe(71 * 69 * 7);
    expect(max).toBe(4);
  });
  it("SABOTAGE: 256-cell tiles must give a maximum of 9", () => {
    const { max } = maxTiles(256);
    console.log(`WS3 sabotage max tiles ${max} (256)`);
    expect(max).toBe(9);
  });
});

// ---- WS4: coverage, against constants read out of the firmware --------------------------------------
function parseView(src: string): ViewConsts | null {
  const one = (re: RegExp) => { const all = [...src.matchAll(re)]; return all.length === 1 ? Number(all[0]![1]) : NaN; };
  const v = {
    kmPerDegree: one(/constexpr double KM_PER_DEGREE = ([0-9.]+);/g),
    maxDegrees: one(/constexpr double MAX_DEGREES = ([0-9.]+);/g),
    minDegrees: one(/constexpr double MIN_DEGREES = ([0-9.]+);/g),
    cosFloor: one(/if \(cosLat < ([0-9.]+)\) cosLat = \1;/g),
  };
  return Object.values(v).every(Number.isFinite) ? v : null;
}

/** Worst-case coverage of one case: the device sits anywhere within +-0.005 deg of the wire's lat/lon and
 *  its range anywhere within +-0.5 km of r. Returns uncovered edges and the largest overshoot in cells. */
function coverage(lat: number, lon: number, r: number, fw: ViewConsts, pad: number) {
  const q = quantise(lat, lon, r);
  const b: CellBox = cropCellBox(CONUS, q, pad, VIEW);
  const cropN = CONUS.la1 - b.r0 * CONUS.dj + CONUS.dj / 2, cropS = CONUS.la1 - (b.r0 + b.rows - 1) * CONUS.dj - CONUS.dj / 2;
  const cropW = CONUS.lo1 + b.c0 * CONUS.di - CONUS.di / 2, cropE = CONUS.lo1 + (b.c0 + b.cols - 1) * CONUS.di + CONUS.di / 2;
  let uncovered = 0, over = 0;
  const E = 1e-9;
  const corners: Array<{ N: number; S: number; W: number; E: number }> = [];
  for (const dLat of [-0.005, 0.005]) for (const dLon of [-0.005, 0.005]) for (const dR of [-0.5, 0.5]) {
    const pl = q.lat + dLat, pn = q.lon + dLon, rr = Math.max(0.5, q.rKm + dR);
    const { radLat, radLon } = viewDegrees(pl, rr, fw);
    const vN = pl + radLat, vS = pl - radLat, vW = pn - radLon, vE = pn + radLon;
    if (cropN < vN - E) uncovered++;
    if (cropS > vS + E) uncovered++;
    if (cropW > vW + E) uncovered++;
    if (cropE < vE - E) uncovered++;
    const o = { N: (cropN - vN) / CONUS.dj, S: (vS - cropS) / CONUS.dj, W: (vW - cropW) / CONUS.di, E: (cropE - vE) / CONUS.di };
    corners.push(o);
    over = Math.max(over, o.N, o.S, o.W, o.E);
  }
  return { uncovered, over, corners, q };
}

describe("WS4: the crop covers the radar's view", () => {
  const fw = parseView(firmware);

  it("CONTROL: the four constants are found exactly once each in src/AircraftManager.cpp (else BLIND)", () => {
    expect(fw, "BLIND: the projection constants were not found exactly once").not.toBeNull();
    // and the server's VIEW is the firmware's, so a projection change fails here first
    expect(fw).toEqual(VIEW);
  });

  // WS4's frozen "overshoot <= 4 cells" FAILED (worst 4.180) and stays a miss in the record (docs/wx-server.md).
  // By ruling it is re-registered as WS4' below; the worst overshoot is still printed here, not asserted.
  it("every sweep case is covered", () => {
    let bad = 0, worst = 0, n = 0;
    for (const s of sweep()) { n++; const c = coverage(s.lat, s.lon, s.r, fw!, PAD_CELLS); bad += c.uncovered; worst = Math.max(worst, c.over); }
    console.log(`WS4 cases ${n}, uncovered edges ${bad}, max overshoot ${worst.toFixed(3)} cells`);
    expect(bad).toBe(0);
  });

  it("SABOTAGE: padding 0 must leave some view uncovered", () => {
    let bad = 0;
    for (const s of sweep()) bad += coverage(s.lat, s.lon, s.r, fw!, 0).uncovered;
    console.log(`WS4 sabotage (pad 0) uncovered edges ${bad}`);
    expect(bad).toBeGreaterThan(0);
  });
});

// ---- WS4': the overshoot, against a per-case bound from the same terms (re-registered by ruling) ---------
/** bound(edge) = frozen padding + 1 (outward rounding) + the case's own wire offset, in cells. The offsets
 *  are the 0.005 deg lat/lon rounding plus the range's +-0.5 km, through ProjectAt's own rule (clamp, floor,
 *  cos floor) at the quantised lat, and -- for W/E -- at lat +- 0.005, so lat rounding's effect on cos(lat)
 *  is in it. Corners suffice: radLat/radLon are monotonic in r, and radLon in lat over 20-55 N. */
function bounds(q: { lat: number; rKm: number }, fw: ViewConsts) {
  const base = viewDegrees(q.lat, q.rKm, fw);
  let dLat = 0, dLon = 0;
  for (const dR of [-0.5, 0.5]) {
    const rr = Math.max(0.5, q.rKm + dR);
    dLat = Math.max(dLat, Math.abs(base.radLat - viewDegrees(q.lat, rr, fw).radLat));
    for (const dl of [-0.005, 0.005]) dLon = Math.max(dLon, Math.abs(base.radLon - viewDegrees(q.lat + dl, rr, fw).radLon));
  }
  const ns = PAD_CELLS + 1 + (0.005 + dLat) / CONUS.dj, ew = PAD_CELLS + 1 + (0.005 + dLon) / CONUS.di;
  return { N: ns, S: ns, W: ew, E: ew };
}

function ws4prime(pad: number, fw: ViewConsts) {
  let checks = 0, violations = 0, worstOver = 0, maxBound = 0, minMargin = Infinity;
  const TOL = 1e-9;
  for (const s of sweep()) {
    const c = coverage(s.lat, s.lon, s.r, fw, pad);
    const b = bounds(c.q, fw);
    maxBound = Math.max(maxBound, b.N, b.W);
    for (const o of c.corners) for (const e of ["N", "S", "W", "E"] as const) {
      checks++;
      worstOver = Math.max(worstOver, o[e]);
      minMargin = Math.min(minMargin, b[e] - o[e]);
      if (o[e] > b[e] + TOL) violations++;
    }
  }
  return { checks, violations, worstOver, maxBound, minMargin };
}

describe("WS4': the overshoot is within each case's own bound", () => {
  const fw = parseView(firmware);
  it("0 violations in 1,097,376 corner-edge checks", () => {
    expect(fw, "BLIND: the projection constants were not found exactly once").not.toBeNull();
    const r = ws4prime(PAD_CELLS, fw!);
    console.log(`WS4' checks ${r.checks}, violations ${r.violations}, worst overshoot ${r.worstOver.toFixed(3)}, ` +
      `max bound ${r.maxBound.toFixed(3)}, min margin ${r.minMargin.toExponential(3)} cells`);
    expect(r.checks).toBe(1_097_376);
    expect(r.violations).toBe(0);
  });
  it("SABOTAGE: padding 3 (bound still from padding 2) must give violations > 0", () => {
    const r = ws4prime(3, fw!);
    console.log(`WS4' sabotage (pad 3) violations ${r.violations} of ${r.checks}`);
    expect(r.violations).toBeGreaterThan(0);
  });
});
