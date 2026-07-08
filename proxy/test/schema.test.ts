import { describe, expect, it } from "vitest";
import {
  blipsBody,
  bucketForRadiusKm,
  mapCategory,
  normalizeReadsbAc,
  quantizeTile,
  upstreamDistNm,
  SENTINEL,
} from "../src/schema";
import { makeAc } from "./helpers";

describe("radius buckets", () => {
  it("snaps up to the next bucket and clamps at the top", () => {
    expect(bucketForRadiusKm(5)).toBe(10);
    expect(bucketForRadiusKm(10)).toBe(10);
    expect(bucketForRadiusKm(10.1)).toBe(20);
    expect(bucketForRadiusKm(40)).toBe(40);
    expect(bucketForRadiusKm(41)).toBe(80);
    expect(bucketForRadiusKm(200)).toBe(160);
  });
});

describe("tile quantization", () => {
  it("snaps to 0.05 degree tiles with a canonical 2-decimal string", () => {
    expect(quantizeTile(47.399)).toBe("47.40");
    expect(quantizeTile(47.401)).toBe("47.40");
    expect(quantizeTile(8.55)).toBe("8.55");
    expect(quantizeTile(-0.024)).toBe("0.00");
    expect(quantizeTile(-0.026)).toBe("-0.05");
  });
});

describe("upstream distance", () => {
  it("pads for the tile offset and converts to whole NM", () => {
    expect(upstreamDistNm(10)).toBe(8); // (10+4)/1.852 = 7.56
    expect(upstreamDistNm(40)).toBe(24); // 44/1.852 = 23.76
    expect(upstreamDistNm(160)).toBe(89); // 164/1.852 = 88.55
  });
});

describe("category mapping", () => {
  it("maps emitter categories to the OpenSky integer enum", () => {
    expect(mapCategory("A3")).toBe(4);
    expect(mapCategory("a7")).toBe(8); // case-insensitive
    expect(mapCategory("B1")).toBe(9);
    expect(mapCategory("C4")).toBe(19);
    expect(mapCategory("Z9")).toBe(0);
    expect(mapCategory(undefined)).toBe(0);
  });
});

describe("normalizeReadsbAc", () => {
  it("drops ground, stale-position, and positionless entries", () => {
    expect(normalizeReadsbAc(makeAc({ alt_baro: "ground" }))).toBeNull();
    expect(normalizeReadsbAc(makeAc({ seen_pos: 61 }))).toBeNull();
    expect(normalizeReadsbAc(makeAc({ lat: undefined }))).toBeNull();
    expect(normalizeReadsbAc(null)).toBeNull();
  });

  it("maps a full entry, trimming the callsign and wrapping the track", () => {
    const a = normalizeReadsbAc(makeAc({ track: 359.7 }));
    expect(a).not.toBeNull();
    expect(a!.cs).toBe("SWR123");
    expect(a!.trackDeg).toBe(0); // 359.7 rounds to 360, wraps to 0
    expect(a!.altFt).toBe(37000);
    expect(a!.gsKt).toBe(451);
    expect(a!.vrateFpm).toBe(-704);
    expect(a!.category).toBe(4);
    expect(a!.ageS).toBe(2);
  });

  it("falls back alt_geom/geom_rate and leaves unknowns null", () => {
    const a = normalizeReadsbAc({
      hex: "AAAAAA",
      lat: 1,
      lon: 2,
      alt_geom: 1200.4,
      geom_rate: 640,
    });
    expect(a!.hex).toBe("aaaaaa");
    expect(a!.altFt).toBe(1200);
    expect(a!.vrateFpm).toBe(640);
    expect(a!.gsKt).toBeNull();
    expect(a!.trackDeg).toBeNull();
    expect(a!.ageS).toBeNull();
  });
});

describe("blipsBody", () => {
  it("serializes rows in frozen field order with sentinels, no nulls", () => {
    const bare = normalizeReadsbAc({ hex: "aaaaaa", lat: 47.41, lon: 8.56 })!;
    const body = JSON.parse(blipsBody(1751970245, [bare]));
    expect(body.v).toBe(1);
    expect(body.t).toBe(1751970245);
    expect(body.n).toBe(1);
    expect(body.a[0]).toEqual([
      "aaaaaa",
      "",
      474100,
      85600,
      SENTINEL.ALT_FT,
      SENTINEL.GS_KT,
      SENTINEL.TRACK_DEG,
      SENTINEL.VRATE_FPM,
      SENTINEL.CATEGORY,
      SENTINEL.AGE_S,
    ]);
    expect(JSON.stringify(body)).not.toContain("null");
  });
});
