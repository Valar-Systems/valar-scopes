// The sea ingest's CI rows (docs/basemap-dense-code.md, "The ingest port: I1-I3 ruled, and its CI rows registered before they are
// written", 883c52b1): KB7-a, asserted at the ingest, the last stage that still knows each lake's pieces; and C-BORDER-S, the US
// containment's edge test seen refusing. Synthetic geometry, so the values are analytic. Each test is named by its registered id;
// scripts/sabotage-basemap.mjs grades its exact red sets from them.
import { describe, expect, it } from "vitest";
import { type Pt, clipPolygon } from "../scripts/basemap/rectclip";
import { type ShpRecord, seaPieces, seaSources } from "../scripts/basemap/sea-pieces";
import { container, whollyInside } from "../scripts/basemap/uscontain";

/** A closed CLOCKWISE rectangle (an outer ring, the shapefile's orientation) as a part's flat [lon, lat] array. */
const rect = (x0: number, y0: number, x1: number, y1: number) => new Float64Array([x0, y0, x0, y1, x1, y1, x1, y0, x0, y0]);
const shoelace = (r: Pt[]) => { let s = 0; for (let i = 0; i < r.length - 1; i++) s += r[i]![0] * r[i + 1]![1] - r[i + 1]![0] * r[i]![1]; return Math.abs(s) / 2; };
const vset = (r: Pt[]) => [...new Set(r.map(([x, y]) => `${x},${y}`))].sort();

describe("the sea ingest (KB7-a, C-BORDER-S)", () => {
  it("KB7-a: a lake crossing the border is stored whole, its pieces together equal to it; a lake wholly inside the US has no piece", () => {
    const us: ShpRecord = { index: 0, parts: [rect(-1, -1, 0.5, 2)], attrs: { ADM0_A3: "USA" } };
    const ocean: ShpRecord = { index: 0, parts: [], attrs: {} };
    const border: ShpRecord = { index: 0, parts: [rect(0.3, 0.3, 0.7, 0.6)], attrs: { name: "Border Lake" } };
    const inside: ShpRecord = { index: 1, parts: [rect(0.1, 0.1, 0.2, 0.2)], attrs: { name: "Inside Lake" } };
    const s = seaSources([ocean], [border, inside], [us]);
    expect([s.stats.inside, s.lakes.map((l) => l.name)]).toEqual([1, ["Border Lake"]]);
    const cell = seaPieces(new Set(["0/0"]), s).byCell["0/0"] ?? [];
    expect(cell.filter((p) => p.src === "Inside Lake")).toEqual([]);
    const pieces = cell.filter((p) => p.src === "Border Lake");
    expect(pieces.length).toBe(1);
    const lake: Pt[] = [[0.3, 0.3], [0.3, 0.6], [0.7, 0.6], [0.7, 0.3], [0.3, 0.3]];
    const equalsLake = (outer: Pt[]) => Math.abs(shoelace(outer) - shoelace(lake)) <= 1e-9 * shoelace(lake) && JSON.stringify(vset(outer)) === JSON.stringify(vset(lake));
    expect(equalsLake(pieces[0]!.outer)).toBe(true);
    // CONTROL: design (b)'s geometry, the lake minus the US (here the part east of x = 0.5), fails the same equality
    const b = clipPolygon(lake.slice(0, -1), [], { x0: 0.5, y0: -1, x1: 2, y1: 2 });
    expect(b.length).toBe(1);
    expect(equalsLake([...b[0]!.outer, b[0]!.outer[0]!]), "CONTROL: (b)'s geometry is not the lake").toBe(false);
  });

  it("C-BORDER-S: a lake whose vertices are all inside the US but whose edges cross its notch is NOT inside; vertices-only says inside", () => {
    const US: Pt[] = [[0, 0], [0, 10], [4, 10], [5, 5], [6, 10], [10, 10], [10, 0], [0, 0]];
    const lake: Pt[] = [[3, 6.5], [3, 7], [7, 7], [7, 6.5], [3, 6.5]];
    const C = container([US]);
    expect(whollyInside(C, [lake])).toBe(false);
    expect(whollyInside(C, [lake], "vertices"), "CONTROL: without the edge test the same lake reads inside").toBe(true);
  });
});
