// RD-P6's regions as the builder reads them (docs/basemap-route.md A2.24, A2.27; MI8): each county's and state's box
// with the files that are READ when it meets the square. PURE.
//
// A file is matched to its region by its own name -- tl_2024_<GEOID>_areawater/linearwater, tl_2024_<FIPS>_prisecroads,
// DomesticNames_<USPS>_Text -- and every water, rivers, roads and peaks file must land in exactly one region: a file in
// none would never be read, and its features would silently never reach a package.
import type { Box, Region } from "../../src/basemap/store";

const REGIONAL = new Set(["water", "rivers", "roads", "peaks"]);

export function regionsFor(
  counties: Record<string, Box>, states: Record<string, { box: Box; usps: string }>, files: Array<{ layer: string; url: string }>,
): Region[] {
  const owner = new Map<string, string>(); // url -> the region that took it
  const take = (region: string, want: (f: { layer: string; url: string }) => boolean) =>
    files.filter(want).map((f) => {
      const prev = owner.get(f.url);
      if (prev) throw new Error(`${f.url} belongs to both ${prev} and ${region}`);
      owner.set(f.url, region);
      return f.url;
    }).sort();
  const out: Region[] = [];
  for (const g of Object.keys(counties).sort()) {
    const fs = take(`county ${g}`, (f) => (f.layer === "water" || f.layer === "rivers") && f.url.includes(`/tl_2024_${g}_`));
    out.push({ box: counties[g]!, files: fs });
  }
  for (const fp of Object.keys(states).sort()) {
    const s = states[fp]!;
    const fs = take(`state ${fp}`, (f) => (f.layer === "roads" && f.url.endsWith(`/tl_2024_${fp}_prisecroads.zip`)) || (f.layer === "peaks" && f.url.endsWith(`/DomesticNames_${s.usps}_Text.zip`)));
    out.push({ box: s.box, files: fs });
  }
  const orphans = files.filter((f) => REGIONAL.has(f.layer) && !owner.has(f.url)).map((f) => f.url);
  if (orphans.length) throw new Error(`${orphans.length} file(s) in no region, so never read: ${orphans.slice(0, 3).join(", ")}`);
  return out;
}
