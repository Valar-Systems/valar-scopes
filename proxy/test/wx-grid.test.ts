// WS1 (docs/wx-server.md): the tiled frame round-trips, cell for cell, and its size is what the plan froze.
import { describe, expect, it } from "vitest";
import {
  FrameBuilder, type FrameMeta, WXG1_HEADER_BYTES, cellInTile, frameMeta, objectBytes, readHeader,
  tileBytes, tileOffset, tiledCellAt,
} from "../src/wx/grid";
import { CONUS, synthCodes } from "./wx-synth";

function build(): { m: FrameMeta; bytes: Uint8Array; codes: Uint8Array } {
  const codes = synthCodes();
  const m = frameMeta("cref", 1760990400, CONUS);
  const fb = new FrameBuilder(m);
  for (let r = 0; r < CONUS.nj; r++) fb.setRow(r, codes.subarray(r * CONUS.ni, (r + 1) * CONUS.ni));
  return { m, bytes: fb.bytes, codes };
}

/** Every tile cut out of the object at the offsets the reader uses. */
function allTiles(m: FrameMeta, bytes: Uint8Array, offset: (ty: number, tx: number) => number) {
  const tiles = new Map<string, Uint8Array>();
  for (let ty = 0; ty < m.tilesY; ty++) for (let tx = 0; tx < m.tilesX; tx++) {
    const o = offset(ty, tx);
    tiles.set(`${ty}-${tx}`, bytes.subarray(o, o + tileBytes(m.tile)));
  }
  return tiles;
}

function mismatches(m: FrameMeta, codes: Uint8Array, at: (r: number, c: number) => number): number {
  let bad = 0;
  for (let r = 0; r < m.nj; r++) for (let c = 0; c < m.ni; c++) if (at(r, c) !== codes[r * m.ni + c]) bad++;
  return bad;
}

describe("WS1: WXG1 round trip", () => {
  const { m, bytes, codes } = build();

  it("is 98 tiles and 12,845,120 bytes, and its header reads back", () => {
    expect(m.tilesX * m.tilesY).toBe(98);
    expect(bytes.length).toBe(12_845_120);
    expect(objectBytes(m)).toBe(12_845_120);
    expect(readHeader(bytes.subarray(0, WXG1_HEADER_BYTES))).toEqual(m);
  });

  it("CONTROL: the synthetic grid has every code 0-15", () => {
    const seen = new Set(codes);
    expect([...seen].sort((a, b) => a - b)).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
  });

  it("24,500,000 / 24,500,000 cells read back identically through the tile reader", () => {
    const at = tiledCellAt(m, allTiles(m, bytes, (ty, tx) => tileOffset(m, ty, tx)));
    const bad = mismatches(m, codes, at);
    console.log(`WS1 cells ${m.ni * m.nj}, mismatched ${bad}`);
    expect(bad).toBe(0);
  });

  it("SABOTAGE: tile offsets computed with tilesY in place of tilesX must mismatch", () => {
    const wrong = (ty: number, tx: number) => WXG1_HEADER_BYTES + (ty * m.tilesY + tx) * tileBytes(m.tile);
    const at = tiledCellAt(m, allTiles(m, bytes, wrong));
    const bad = mismatches(m, codes, at);
    console.log(`WS1 sabotage mismatched ${bad}`);
    expect(bad).toBeGreaterThan(0);
  });

  it("edge tiles are padded with 14 beyond the grid", () => {
    const last = bytes.subarray(tileOffset(m, m.tilesY - 1, m.tilesX - 1));
    // grid row 3499 is local row 3499 - 6*512 = 427 of the last tile row; local row 428 is padding
    expect(cellInTile(last, m.tile, 428, 0)).toBe(14);
    expect(cellInTile(last, m.tile, 0, 7000 - 13 * 512)).toBe(14);   // first column past ni
  });

  it("readHeader refuses what writeHeader would not write", () => {
    const bad = bytes.slice(0, WXG1_HEADER_BYTES);
    bad[3] = 0x32;
    expect(() => readHeader(bad)).toThrow(/not a WXG1/);
    const v2 = bytes.slice(0, WXG1_HEADER_BYTES);
    v2[4] = 2;
    expect(() => readHeader(v2)).toThrow(/version/);
  });
});
