// The coverage lattice's scanline (scripts/basemap/coverage.ts). Not a frozen row: MC grades the real store
// after D1. These pin the half-open rule, holes, the union of states, and the lookup.
import { describe, expect, it } from "vitest";
import { covered, coverageRows, encodeCoverage } from "../scripts/basemap/coverage";
import { checkBody } from "../scripts/basemap/sources";

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

describe("checkBody (the fetcher refuses a firewall's rejection page)", () => {
  const page = new TextEncoder().encode("<html><head><title>Request Rejected</title></head><body>The requested URL was rejected.");
  it("the 247-byte page census.gov served under status 200 is refused as a zip, a CSV and a PNG", () => {
    expect(checkBody("https://x.invalid/a.zip", "text/html; charset=utf-8", page)).toMatch(/text\/html/);
    expect(checkBody("https://x.invalid/a.zip", null, page)).toMatch(/not a zip/);
    expect(checkBody("https://x.invalid/a.csv", null, page)).toMatch(/HTML/);
    expect(checkBody("https://x.invalid/9/1/2.png", null, page)).toMatch(/not a PNG/);
  });
  it("CONTROL: real magic passes", () => {
    expect(checkBody("https://x.invalid/a.zip", "application/zip", new Uint8Array([0x50, 0x4b, 0x03, 0x04, 0]))).toBeNull();
    expect(checkBody("https://x.invalid/9/1/2.png", "image/png", new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))).toBeNull();
    expect(checkBody("https://x.invalid/a.csv", "text/csv", new TextEncoder().encode("SUMLEV,STATE"))).toBeNull();
  });
});
