// The four source bits per tile (scripts/basemap/tile-bits.ts, A1.4). Not frozen rows: MC4 grades the real
// store's credits after D1. These pin the box tests, the 60° N split, and the 2 km widening.
import { describe, expect, it } from "vitest";
import { BIT, Layer, footprint, segmentMeetsBox, tileBits, widen } from "../scripts/basemap/tile-bits";

const rect = (w: number, s: number, e: number, n: number) => new Float64Array([w, s, e, s, e, n, w, n]);

describe("tile source bits", () => {
  it("a segment meets a box when it crosses it, not when it passes by", () => {
    expect(segmentMeetsBox(-1, 0.5, 2, 0.5, [0, 0, 1, 1])).toBe(true);   // through
    expect(segmentMeetsBox(-1, 2, 2, 2, [0, 0, 1, 1])).toBe(false);     // above
    expect(segmentMeetsBox(1, 1, 3, 3, [0, 0, 1, 1])).toBe(true);       // touches the corner (closed box)
  });

  it("a box wholly inside a polygon meets it (the centre ray), and one wholly outside does not", () => {
    // tile (256, 254): x 0..0.703 deg, y about 0.70..1.41 deg north
    const [w, s, e, n] = footprint(256, 254);
    const big = new Layer([rect(w - 5, s - 5, e + 5, n + 5)]);
    expect(big.meets([w, s, e, n], [256 * 512 + 254])).toBe(true);
    const away = new Layer([rect(w + 10, s, e + 10, n)]);
    expect(away.meets([w, s, e, n], [256 * 512 + 254])).toBe(false);
  });

  it("land south and north of 60 N set SRTM and GMTED2010 separately", () => {
    // a tile straddling 60 N, with land only on the south side of it
    let y = 0; while (footprint(100, y)[1] > 60) y++;           // the first tile whose south edge is below 60
    const [w, s, e, n] = footprint(100, y);
    expect(s < 60 && n > 60).toBe(true);
    const tile = 100 * 512 + y;
    const landSouth = new Layer([rect(w, s, e, 59.99)]), none = new Layer([]);
    expect(tileBits(100, y, landSouth, none, none)).toBe(BIT.SRTM);
    const landNorth = new Layer([rect(w, 60.01, e, n)]);
    expect(tileBits(100, y, landNorth, none, none)).toBe(BIT.GMTED2010);
    expect(tile).toBeGreaterThan(0);
  });

  it("ocean and Canada count within 2 km of the footprint, and not at 3 km", () => {
    const x = 300, y = 180, [w, s, e, n] = footprint(x, y), none = new Layer([]);
    const pole = Math.max(Math.abs(s), Math.abs(n)), km = (d: number) => d / (111 * Math.cos(pole * Math.PI / 180));
    const near = new Layer([rect(e + km(1.5), s, e + km(5), n)]);   // 1.5 km east of the tile
    const far = new Layer([rect(e + km(3), s, e + km(5), n)]);      // 3 km east
    expect(tileBits(x, y, none, near, none)).toBe(BIT.ETOPO1);
    expect(tileBits(x, y, none, far, none)).toBe(0);
    expect(tileBits(x, y, none, none, near)).toBe(BIT.NRCAN);
    expect(widen([0, 0, 1, 1], 2)[3]).toBeCloseTo(1 + 2 / 111, 12);
  });
});
