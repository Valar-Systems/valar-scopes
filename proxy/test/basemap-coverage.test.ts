// The coverage lattice's scanline (scripts/basemap/coverage.ts). Not a frozen row: MC grades the real store
// after D1. These pin the half-open rule, holes, the union of states, and the lookup.
import { describe, expect, it } from "vitest";
import { covered, coverageRows, encodeCoverage } from "../scripts/basemap/coverage";

const ring = (pts: number[]) => ({ xy: new Float64Array(pts) });

describe("coverage lattice", () => {
  it("a square from 0.00 to 0.02 covers lattice 0 and 1 on each axis, not 2 (half-open)", () => {
    const rows = coverageRows([[ring([0, 0, 0.02, 0, 0.02, 0.02, 0, 0.02])]]);
    const bin = encodeCoverage(rows);
    const got: string[] = [];
    for (let h = -1; h <= 3; h++) for (let c = -1; c <= 3; c++) if (covered(bin, h, c)) got.push(`${h},${c}`);
    expect(got).toEqual(["0,0", "0,1", "1,0", "1,1"]);
  });

  it("a hole is not covered; two touching states are one run", () => {
    const outer = ring([-0.1, -0.1, 0.1, -0.1, 0.1, 0.1, -0.1, 0.1]);
    const hole = ring([-0.035, -0.035, -0.035, 0.035, 0.035, 0.035, 0.035, -0.035]);
    const east = ring([0.1, -0.1, 0.2, -0.1, 0.2, 0.1, 0.1, 0.1]);
    const bin = encodeCoverage(coverageRows([[outer, hole], [east]]));
    expect(covered(bin, 0, 0)).toBe(false);   // inside the hole
    expect(covered(bin, 0, -5)).toBe(true);   // between the hole and the outer edge
    expect(covered(bin, 0, 10)).toBe(true);   // the shared edge belongs to the east state (half-open)
    expect(covered(bin, 0, 19)).toBe(true);
    expect(covered(bin, 0, 20)).toBe(false);
    expect(coverageRows([[outer, hole], [east]]).get(5)).toEqual([[-10, 19]]); // above the hole: one merged run
  });

  it("a ring whose closing vertex repeats its first is the same as one closed implicitly", () => {
    const open = coverageRows([[ring([0, 0, 0.02, 0, 0.02, 0.02, 0, 0.02])]]);
    const closed = coverageRows([[ring([0, 0, 0.02, 0, 0.02, 0.02, 0, 0.02, 0, 0])]]);
    expect([...closed]).toEqual([...open]);
  });
});
