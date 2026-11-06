// ML, MR5 and MR6 (docs/basemap-route.md A1.7, A2.5): layers.ts against the frozen rows. Synthetic features at origin
// (0, 0), R 97 km, ε 100,584 mm, made from km as A1.7 says: lat = lat0 - S/111, lon = lon0 + E/(111 c0).
// Each test is named by its case id; scripts/sabotage-basemap.mjs grades its exact sets from them.
import { describe, expect, it } from "vitest";
import { type Pt, makeFrame, within3km } from "../src/basemap/frame";
import {
  type SrcFeature, type Summit, isBusRoute, isRiverName, keepPlaces, keepWater, rankPeaks, roadClass, selectRivers, withinMm,
} from "../src/basemap/layers";
import { elevationM } from "../src/basemap/terrain";

const F = makeFrame(0, 0, 97_000_000, 100_584);

/** A feature from (lon, lat) pairs. */
const ll = (file: number, record: number, part: number, attrs: SrcFeature["attrs"], pts: Pt[]): SrcFeature =>
  ({ file, record, part, attrs, xy: Float64Array.from(pts.flat()) });
/** A feature from (E, S) km, through A1.7's conversion. */
const km = (file: number, record: number, part: number, attrs: SrcFeature["attrs"], pts: Pt[]): SrcFeature =>
  ll(file, record, part, attrs, pts.map(([e, s]) => [0 + e / (111 * F.c0), 0 - s / 111]));
/** A river: a straight E-W line at S, from E = a to E = b km. */
const river = (record: number, name: string, a: number, b: number, s = 0, part = 0) =>
  km(0, record, part, { FULLNAME: name }, [[a, s], [b, s]]);

const CW: Pt[] = [[0, 0], [0, 0.1], [0.1, 0.1], [0.1, 0]];      // shoelace on (lon, lat): -0.020000000000000004
const CCW: Pt[] = [[0, 0], [0.1, 0], [0.1, 0.1], [0, 0.1]];     // +0.020000000000000004
const LINE: Pt[] = [[0, 0], [0.05, 0], [0.1, 0]];               // 0
const closed = (r: Pt[]): Pt[] => [...r, r[0]!];
/** CW's quantised ring, by hand: 0.1 deg is 11.100000000000001 km, which quantises to 3750 (and -3750 southward). */
const CW_Q: Pt[] = [[0, 0], [0, -3750], [3750, -3750], [3750, 0]];

describe("ML, the layer rules", () => {
  it("ML1: water at AWATER 1,499,999 is dropped and at 1,500,000 kept", () => {
    const shifted = CW.map(([x, y]) => [x + 0.2, y] as Pt);
    const r = keepWater(F, [ll(0, 0, 0, { AWATER: 1_499_999 }, closed(shifted)), ll(0, 1, 0, { AWATER: 1_500_000 }, closed(CW))]);
    expect(r.rings).toEqual([CW_Q]);
  });

  it("ML2: one polygon, an outer ring and a hole, gives 1 ring (RD3)", () => {
    const hole: Pt[] = [[0.03, 0.03], [0.07, 0.03], [0.07, 0.07], [0.03, 0.07]]; // counter-clockwise
    const r = keepWater(F, [ll(0, 0, 0, { AWATER: 2_000_000 }, closed(CW)), ll(0, 0, 1, { AWATER: 2_000_000 }, closed(hole))]);
    expect(r.rings).toEqual([CW_Q]);
    expect(r.collapsed).toBe(0);
  });

  it("ML3: a river name ends in \" Riv\"", () => {
    const names = ["Deschutes Riv", "Little Deschutes Riv", "Riverside Dr", "Riv", "Big Riv "];
    expect(names.filter(isRiverName)).toEqual(["Deschutes Riv", "Little Deschutes Riv"]);
  });

  it("ML4: the 5 longest groups; a tie broken by FULLNAME byte order, not source order (RD8)", () => {
    const groups: Array<[string, number]> = [["A Riv", 70], ["B Riv", 60], ["C Riv", 50], ["D Riv", 40], ["F Riv", 30], ["E Riv", 30], ["H Riv", 10]];
    const r = selectRivers(F, groups.map(([n, l], i) => river(i, n, -l / 2, l / 2)));
    expect(r.names).toEqual(["A Riv", "B Riv", "C Riv", "D Riv", "E Riv"]);
  });

  it("ML5: a group's length is the sum of its parts", () => {
    const feats = [river(0, "P Riv", -10, 10, 0, 0), river(0, "P Riv", -12.5, 12.5, 5, 1),
      river(1, "Q Riv", -25, 25), river(2, "R Riv", -20, 20), river(3, "S Riv", -15, 15), river(4, "T Riv", -14.5, 14.5), river(5, "U Riv", -14, 14)];
    expect(selectRivers(F, feats).names).toEqual(["Q Riv", "P Riv", "R Riv", "S Riv", "T Riv"]);
  });

  it("ML6: the length is inside the disc, not the square", () => {
    const feats = [river(0, "Mid Riv", -50, 50), river(1, "Edge Riv", -200, 200, 90),
      river(2, "M80 Riv", -40, 40), river(3, "M75 Riv", -37.5, 37.5), river(4, "M74 Riv", -37, 37), river(5, "M73 Riv", -36.5, 36.5)];
    expect(selectRivers(F, feats).names).toEqual(["Mid Riv", "M80 Riv", "M75 Riv", "M74 Riv", "M73 Riv"]);
  });

  it("ML7: a business route is \"Bus\" as a whole word, case-sensitive", () => {
    const names = ["US Hwy 97 Bus", "Bus US Hwy 20", "President George Bush Tpke", "Busby Rd", "BUS", "Bus", "Bus Loop", "Bus-Loop 10", "Omnibus Rd", "OmniBus Rd", undefined];
    expect(names.map((n) => (isBusRoute(n) ? "drop" : "keep")))
      .toEqual(["drop", "drop", "keep", "keep", "keep", "drop", "drop", "drop", "keep", "keep", "keep"]);
  });

  it("ML8: S1100 is class 1, S1200 class 2, S1400 dropped", () => {
    expect(["S1100", "S1200", "S1400"].map(roadClass)).toEqual([1, 2, null]);
  });

  const summit = (name: string, fid: number, e: number, elevM: number): Summit & { elevM: number } =>
    ({ fid, name, p: [e, 0], lat: 0, lon: 0, elevM });

  it("ML9: summits by elevation descending, then feature_id ascending", () => {
    const c = [summit("A", 10, 0, 1000), summit("B", 30, 10, 1200), summit("C", 20, 20, 1200), summit("D", 40, 30, 900), summit("E", 50, 40, 1100), summit("F", 60, 50, 800)];
    expect(rankPeaks(F, c).map((k) => k.name)).toEqual(["C", "B", "E", "A", "D", "F"]);
  });

  it("ML10: elevationM rounds the Terrarium value half up (RD10)", () => {
    expect([[128, 0, 128], [127, 255, 128], [132, 176, 0], [128, 0, 127]].map(([r, g, b]) => elevationM(r!, g!, b!))).toEqual([1, 0, 1200, 0]);
  });

  it("ML11: only KEPT summits de-duplicate", () => {
    const c = [summit("P", 1, 10, 2000), summit("Q", 2, 12.999, 1990), summit("R", 3, 14.0, 1980), summit("T", 4, 6.999, 1970)];
    expect(rankPeaks(F, c).map((k) => k.name)).toEqual(["P", "R", "T"]);
  });

  it("ML11b: exactly 3,000.0 m is within, so dropped (RD9)", () => {
    expect(within3km(3.0, 0)).toBe(true);
    expect(rankPeaks(F, [summit("H", 1, 0, 2000), summit("L", 2, 3, 1000)]).map((k) => k.name)).toEqual(["H"]);
  });

  it("ML12: the first 16 survivors", () => {
    const c = [...Array(20).keys()].map((i) => summit(`S${i}`, i + 1, -95 + 10 * i, 3000 - 10 * i));
    expect(rankPeaks(F, c).map((k) => k.name)).toEqual([...Array(16).keys()].map((i) => `S${i}`));
  });

  it("ML13: places within R, in file order, populations unchanged", () => {
    const place = (record: number, name: string, e: number, pop: number) => km(0, record, 0, { GEOID: String(record), NAME: name, pop }, [[e, 0]]);
    const got = keepPlaces(F, [place(0, "Z", 96.9, 11), place(1, "A", 50, 22), place(2, "M", 97.1, 33), place(3, "B", 10, 44)]);
    expect(got.map((p) => [p.name, p.pop])).toEqual([["Z", 11], ["A", 22], ["B", 44]]);
  });

  it("ML13b: withinMm at d = R is true", () => {
    expect(withinMm([97, 0], 97_000_000)).toBe(true);
    // CONTROL: one ulp past R is outside, so the row can tell <= from <
    expect(withinMm([97.00000000000001, 0], 97_000_000)).toBe(false);
  });
});

describe("MR, the ruled readings (layers)", () => {
  it("MR5: only a negative shoelace sum is outer; a zero-sum part never enters (RD26)", () => {
    const r = keepWater(F, [ll(0, 0, 0, { AWATER: 2_000_000 }, closed(CW)), ll(0, 0, 1, { AWATER: 2_000_000 }, closed(CCW)),
      ll(0, 0, 2, { AWATER: 2_000_000 }, closed(LINE))]);
    expect(r.rings).toEqual([CW_Q]);
    expect(r.collapsed).toBe(0);
  });

  it("MR6: a group with 0 m in the disc is not a candidate, and none of its parts is drawn (RD29)", () => {
    const r = selectRivers(F, [river(0, "A Riv", -25, 25), river(1, "B Riv", -20, 20, 10), river(2, "C Riv", 30, 90, 95), river(3, "D Riv", -15, 15, -10)]);
    expect(r.names).toEqual(["A Riv", "B Riv", "D Riv"]);
    expect(r.lines.map((l) => l.name)).toEqual([0, 1, 2]);
  });
});
