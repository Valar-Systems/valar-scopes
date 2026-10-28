// MT (docs/basemap-route.md A1.7): terrain.ts against the frozen rows. Each test is named by its case id;
// scripts/sabotage-basemap.mjs grades its exact sets from them.
import { describe, expect, it } from "vitest";
import { makeFrame } from "../src/basemap/frame";
import { boxMean4, decodeTerrarium, hillshade, levelPixelToZ9, levelTiles, mosaicOf, shadeByte, terrainLevel } from "../src/basemap/terrain";

/** MT2: an 8 x 8 plane DEM through hillshade, boxMean4 and shadeByte at every pixel; the distinct bytes. */
function planeBytes(f: (x: number, y: number) => number, lat0: number): number[] {
  const dem = new Float64Array(64);
  for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) dem[y * 8 + x] = f(x, y);
  return [...new Set(Array.from(boxMean4(hillshade(dem, 8, 8, lat0), 8, 8), shadeByte))];
}

describe("MT, terrain", () => {
  it("MT1: decodeTerrarium, exactly", () => {
    expect([[128, 0, 128], [127, 255, 128], [132, 176, 0], [0, 0, 0]].map(([r, g, b]) => decodeTerrarium(r!, g!, b!))).toEqual([0.5, -0.5, 1200, -32768]);
  });
  it("MT2a: flat, lat0 0 -> 180 at all 64", () => expect(planeBytes(() => 1000, 0)).toEqual([180]));
  it("MT2b: rising east, lat0 0 -> 216 at all 64", () => expect(planeBytes((x) => 1000 + 64 * x, 0)).toEqual([216]));
  it("MT2c: rising north, lat0 0 -> 117 at all 64", () => expect(planeBytes((_, y) => 1000 + 64 * (7 - y), 0)).toEqual([117]));
  it("MT2d: facing north-west, lat0 0 -> 247 at all 64", () => expect(planeBytes((x, y) => 1000 + 64 * x + 64 * y, 0)).toEqual([247]));
  it("MT2e: rising east, lat0 60 -> 220 at all 64", () => expect(planeBytes((x) => 1000 + 64 * x, 60)).toEqual([220]));

  it("MT-impulse: the 4 x 4 mean of a single 1 at (10, 10) is 0.0625 at x, y 9..12 and 0 elsewhere", () => {
    const s = new Float64Array(256); s[10 * 16 + 10] = 1;
    const m = boxMean4(s, 16, 16);
    for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
      expect(m[y * 16 + x], `(${x},${y})`).toBe(x >= 9 && x <= 12 && y >= 9 && y <= 12 ? 0.0625 : 0);
    }
  });
  it("MT-corner: the same at (0, 0), edge padding 2", () => {
    const s = new Float64Array(256); s[0] = 1;
    const m = boxMean4(s, 16, 16), at = (x: number, y: number) => m[y * 16 + x];
    expect([at(0, 0), at(1, 0), at(0, 1), at(2, 0), at(1, 1), at(2, 1), at(2, 2), at(3, 0)])
      .toEqual([0.5625, 0.375, 0.375, 0.1875, 0.25, 0.125, 0.0625, 0]);
  });

  it("MT3: a level over the rule's mosaic and over one a tile larger gives the same 60,516 values", () => {
    const f = makeFrame(0, 0, 97_000_000, 100_584);
    const levelMm = 98_974_656, n = 246;
    const [tx0, ty0, tx1, ty1] = levelTiles(f, levelMm);
    // seeded smooth synthetic terrain over the larger rectangle, as Terrarium RGB
    const seed = 0x5eed, ph = [(seed % 97) / 13, (seed % 89) / 17, (seed % 83) / 19];
    const h = (X: number, Y: number) => 1500 + 400 * Math.sin(X / 37.3 + ph[0]!) + 300 * Math.cos(Y / 53.1 + ph[1]!) + 60 * Math.sin((X + Y) / 11.7 + ph[2]!);
    const tiles = new Map<string, Uint8Array>();
    for (let x = tx0 - 1; x <= tx1 + 1; x++) for (let y = ty0 - 1; y <= ty1 + 1; y++) {
      const t = new Uint8Array(256 * 256 * 3);
      for (let py = 0; py < 256; py++) for (let px = 0; px < 256; px++) {
        const v = h(x * 256 + px, y * 256 + py) + 32768, o = (py * 256 + px) * 3;
        t[o] = Math.floor(v / 256); t[o + 1] = Math.floor(v) % 256; t[o + 2] = Math.floor((v - Math.floor(v)) * 256);
      }
      tiles.set(`${x}/${y}`, t);
    }
    const rule = terrainLevel(f, mosaicOf(tiles, tx0, ty0, tx1, ty1), levelMm, n);
    const larger = terrainLevel(f, mosaicOf(tiles, tx0 - 1, ty0 - 1, tx1 + 1, ty1 + 1), levelMm, n);
    expect(rule.length).toBe(60_516);
    let diff = 0;
    for (let k = 0; k < rule.length; k++) if (rule[k] !== larger[k]) diff++;
    expect(diff).toBe(0);
    // CONTROL: the field is not uniform, so "identical" is not trivially true. (First written as "more than 50
    // distinct bytes", a guess, which read 45; "not uniform" is more than 1. Recorded in A2.18.)
    expect(new Set(rule).size).toBeGreaterThan(1);
  });

  it("MT4: levelPixelToZ9 truncates", () => {
    const f = makeFrame(0, 0, 97_000_000, 100_584);
    expect([levelPixelToZ9(f, 1_000_000, 4, 0, 0), levelPixelToZ9(f, 1_000_000, 4, 3, 3), levelPixelToZ9(f, 1_000_000, 4, 1, 2), levelPixelToZ9(f, 1_250_000, 5, 2, 2)])
      .toEqual([[65533, 65533], [65538, 65538], [65535, 65536], [65536, 65536]]);
  });
  it("MT5: shadeByte", () => expect([0, 1, 0.5, 127.49 / 255].map(shadeByte)).toEqual([0, 255, 128, 127]));
});
