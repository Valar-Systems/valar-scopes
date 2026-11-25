// BUILD-25 (the owner's ruling, 2026-11-15): six counties' TIGER2025 files stand in for their TIGER2024 files, which
// www2.census.gov and its ftp2 mirror refused this machine. Exactly those six, nowhere else. Named by case id.
import { describe, expect, it } from "vitest";
import { checkTigerVintages, TIGER2025_NOTE, TIGER2025_SIX, vintageOf, with2025, type SourceFile } from "../scripts/basemap/sources";
import { regionsFor } from "../scripts/basemap/regions";
import { renderCreditsHtml } from "../src/photolicense";

const T24 = "https://www2.census.gov/geo/tiger/TIGER2024/";
const T25 = "https://www2.census.gov/geo/tiger/TIGER2025/";
const SIX: Array<[string, "water" | "rivers"]> = [
  ["38021", "water"], ["39167", "water"], ["44007", "water"], ["56029", "water"], ["17005", "rivers"], ["19123", "rivers"],
];
const dir = (l: "water" | "rivers") => (l === "water" ? "AREAWATER" : "LINEARWATER");
const kind = (l: "water" | "rivers") => (l === "water" ? "areawater" : "linearwater");
const url24 = (g: string, l: "water" | "rivers") => `${T24}${dir(l)}/tl_2024_${g}_${kind(l)}.zip`;
const url25 = (g: string, l: "water" | "rivers") => `${T25}${dir(l)}/tl_2025_${g}_${kind(l)}.zip`;
const box = [-100, 40, -99, 41] as [number, number, number, number];

// A plan-shaped list: the six 2024 counterparts among other TIGER2024 files and a non-TIGER census file.
const plan24: SourceFile[] = [
  { layer: "water", url: url24("38019", "water") },
  ...SIX.map(([g, l]) => ({ layer: l, url: url24(g, l) })),
  { layer: "rivers", url: url24("38021", "rivers") },
  { layer: "roads", url: `${T24}PRISECROADS/tl_2024_38_prisecroads.zip` },
  { layer: "places", url: "https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip" },
];

describe("BUILD-25, the six TIGER2025 files", () => {
  it("T1: vintageOf reads 2025 from a TIGER2025 url, whatever its Last-Modified says", () => {
    expect(vintageOf({ layer: "water", url: url25("38021", "water") }, "Tue, 01 Sep 2026 00:00:00 GMT")).toBe("2025");
  });

  it("T2: a county takes its own 2025 file when it is one of the six", () => {
    const r = regionsFor({ "38021": box }, {}, [{ layer: "water", url: url25("38021", "water") }]);
    expect(r.map((x) => x.files)).toEqual([[url25("38021", "water")]]);
  });

  it("T3 (control): a 2025 file NOT among the six is still an orphan, refused", () => {
    expect(() => regionsFor({ "38045": box }, {}, [{ layer: "water", url: url25("38045", "water") }])).toThrow(/in no region/);
  });

  it("T4: with2025 replaces exactly the six counterparts, in place, each with the note; nothing else moves", () => {
    expect(TIGER2025_SIX.map((f) => f.url)).toEqual(SIX.map(([g, l]) => url25(g, l)));
    const out = with2025(plan24);
    expect(out.length).toBe(plan24.length);
    const changed = out.map((f, i) => (f.url !== plan24[i]!.url ? i : -1)).filter((i) => i >= 0);
    expect(changed).toEqual([1, 2, 3, 4, 5, 6]);
    for (const [k, [g, l]] of SIX.entries()) expect(out[k + 1]).toEqual({ layer: l, url: url25(g, l), note: TIGER2025_NOTE });
    for (const i of [0, 7, 8, 9]) expect(out[i]).toEqual(plan24[i]);
  });

  // Every refusal row names its own message: a bare toThrow() also passes on "with2025 is not a function" (RF1, 16:18Z).
  it("T5: with2025 refuses a missing counterpart, and a doubled one", () => {
    const why = /^with2025: .*counterpart (missing|appears \d+ times)$/;
    expect(() => with2025(plan24.filter((f) => f.url !== url24("44007", "water")))).toThrow(why);
    expect(() => with2025([...plan24, { layer: "rivers", url: url24("19123", "rivers") }])).toThrow(why);
  });

  it("T6: checkTigerVintages passes the substituted plan, refuses a seventh 2025 url and a missing one", () => {
    const why = /^checkTigerVintages: /;
    const ok = with2025(plan24);
    expect(() => checkTigerVintages(ok)).not.toThrow();
    expect(() => checkTigerVintages([...ok, { layer: "water", url: url25("38045", "water") }])).toThrow(why);
    expect(() => checkTigerVintages(ok.filter((f) => f.url !== url25("17005", "rivers")))).toThrow(why);
    expect(() => checkTigerVintages(plan24)).toThrow(why);   // the six 2024 urls still planned: none of the six present
  });

  it("T7: the public credit names TIGER/Line 2025 for the six and still TIGER/Line 2024", () => {
    const html = renderCreditsHtml([]);
    expect(html).toContain("TIGER/Line 2024 (TIGER/Line 2025 for six counties' water or river files)");
  });
});
