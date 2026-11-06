// MI8 (docs/basemap-route.md A2.27): RD-P6's regions, with the files each one reads. Named by its case id.
import { describe, expect, it } from "vitest";
import { regionsFor } from "../scripts/basemap/regions";

const T = "https://www2.census.gov/geo/tiger/TIGER2024/";
const G = "https://prd-tnm.s3.amazonaws.com/StagedProducts/GeographicNames/DomesticNames/";
const files = [
  { layer: "water", url: `${T}AREAWATER/tl_2024_41017_areawater.zip` }, { layer: "water", url: `${T}AREAWATER/tl_2024_41019_areawater.zip` },
  { layer: "rivers", url: `${T}LINEARWATER/tl_2024_41017_linearwater.zip` }, { layer: "rivers", url: `${T}LINEARWATER/tl_2024_41019_linearwater.zip` },
  { layer: "roads", url: `${T}PRISECROADS/tl_2024_41_prisecroads.zip` }, { layer: "peaks", url: `${G}DomesticNames_OR_Text.zip` },
  { layer: "places", url: "https://www2.census.gov/geo/docs/maps-data/data/gazetteer/2025_Gazetteer/2025_Gaz_place_national.zip" },
  { layer: "population", url: "https://www2.census.gov/programs-surveys/popest/datasets/2020-2024/cities/totals/sub-est2024.csv" },
];
const counties = { "41017": [-122, 43, -120, 45] as [number, number, number, number], "41019": [-124, 42, -122, 44] as [number, number, number, number] };
const states = { "41": { box: [-125, 41, -116, 47] as [number, number, number, number], usps: "OR" } };

describe("MI8, the regions", () => {
  it("MI8: each county reads its AREAWATER and LINEARWATER, the state its PRISECROADS and GNIS; national files in none; an orphan refused", () => {
    const r = regionsFor(counties, states, files);
    expect(r.map((x) => x.files.map((u) => u.split("/").pop()))).toEqual([
      ["tl_2024_41017_areawater.zip", "tl_2024_41017_linearwater.zip"],
      ["tl_2024_41019_areawater.zip", "tl_2024_41019_linearwater.zip"],
      ["DomesticNames_OR_Text.zip", "tl_2024_41_prisecroads.zip"],
    ]);
    expect(r.map((x) => x.box)).toEqual([counties["41017"], counties["41019"], states["41"].box]);
    expect(() => regionsFor(counties, states, [...files, { layer: "water", url: `${T}AREAWATER/tl_2024_41999_areawater.zip` }])).toThrow(/in no region/);
  });
});
