// MR and ME4 (docs/basemap-route.md A2.5, A1.7): frame.ts against the ruled readings, each row's expected doubles
// exactly as frozen. Each test is named by its case id; scripts/sabotage-basemap.mjs grades its exact sets from them.
// MR5 (isOuter, through a water record) and MR6 (selectRivers) bind to layers.ts and land with it.
import { describe, expect, it } from "vitest";
import { type Pt, VertexRange, clipLine, clipRing, dedupeRing, quantise, simplify, within3km } from "../src/basemap/frame";

/** Bit-identity, not closeness: Object.is on every coordinate (so -0 and 0 differ too). */
const same = (got: Pt[], want: Pt[]) =>
  got.length === want.length && got.every((p, i) => Object.is(p[0], want[i]![0]) && Object.is(p[1], want[i]![1]));

describe("MR, the ruled readings", () => {
  it("MR1: an endpoint the clip did not move is the source vertex itself (RD22)", () => {
    const a: Pt = [32.636433333333336, 31.1622], b: Pt = [-58.9144, 69.11385714285714];
    const runs = clipLine([a, b], 97);
    expect(runs.length).toBe(1);
    expect(same(runs[0]!, [a, b]), JSON.stringify(runs)).toBe(true);
    // CONTROL: the recomputation this row exists to refuse really does move b, so the row can see it
    expect(a[0] + 1 * (b[0] - a[0])).toBe(-58.91440000000001);
  });

  it("MR2: Sutherland-Hodgman in pass order W, E, N, S, crossings from the previous vertex (RD23)", () => {
    const got = clipRing([[-110.3, 20.7], [10.1, -115.9], [115.7, 10.3], [5.9, 120.1]], 97);
    const want: Pt[] = [[-21.10422535211267, 97], [-97, 32.077108433734935], [-97, 5.610465116279071],
      [-6.558565153733539, -97], [25.914896988906506, -97], [97, -12.04791666666668], [97, 29.000000000000004], [29, 97]];
    expect(same(got, want), JSON.stringify(got)).toBe(true);
  });

  it("MR3: a ring's last vertex equal to its first is dropped after the consecutive dedupe (RD24)", () => {
    const want: Pt[] = [[1, 1], [5, 1], [5, 5]];
    expect(dedupeRing([[1, 1], [5, 1], [5, 5], [1, 1]])).toEqual(want);
    expect(dedupeRing([[1, 1], [1, 1], [5, 1], [5, 5], [1, 1], [1, 1]])).toEqual(want);
  });

  it("MR4: a vertex exactly at ε is dropped; one micrometre past it is kept, unmoved (RD25, RD7, RD31)", () => {
    const epsM = 100584 / 1000;
    const at: Pt[] = [[0, 0], [0.5, 0.100584], [1, 0]];
    const past: Pt[] = [[0, 0], [0.5, 0.100585], [1, 0]];
    expect(same(simplify(at, epsM), [at[0]!, at[2]!])).toBe(true);
    expect(same(simplify(past, epsM), past)).toBe(true);
  });

  it("MR7: the 3 km de-dup decides on sqrt(x*x + y*y), never hypot (RD31)", () => {
    const ex = 29999999999977143e-16, ey = 0.000003703499999999059;
    // CONTROL: this is the pair where the two disagree, on this engine, so the row can tell them apart
    expect(Math.sqrt(ex * ex + ey * ey)).toBe(3 + 2 ** -51);
    expect(Math.hypot(ex, ey)).toBe(3 - 2 ** -51);
    expect(within3km(ex, ey)).toBe(false);
  });
});

describe("ME, the encoder's range", () => {
  it("ME4: a vertex past R quantises to 32,768 and is thrown as vertex_range, never clamped to 32,767", () => {
    const rMm = 97_000_000;
    let got: unknown;
    try { got = quantise([1.00002 * rMm / 1e6, 0], rMm); } catch (e) { got = e; }
    expect(got).toBeInstanceOf(VertexRange);
    expect(String(got)).toContain("vertex_range");
    // CONTROL: the edge itself is in range
    expect(quantise([97, -97], rMm)).toEqual([32767, -32767]);
  });
});
