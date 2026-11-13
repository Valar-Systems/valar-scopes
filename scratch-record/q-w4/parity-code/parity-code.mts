// PARITY-CODE (registered at 894437c6): the SHIPPED sea module (wt-code/proxy/scripts/basemap/sea-pieces.ts at the PR head) on step 2's
// pinned inputs. Writes the pieces as JSON.stringify(byCell) and the stats in sea-ingest.mts's fields; counts only to stdout.
//   (from wt-main/proxy) node --import tsx ../../parity-code.mts <out.json> <stats.json>
import { readFileSync, writeFileSync } from "node:fs";
import { readShapefile, shapefileFromZip } from "./wt-code/proxy/scripts/basemap/shapefile.ts";
import { coverageSegments, seaExtent, seaPieces, seaSources } from "./wt-code/proxy/scripts/basemap/sea-pieces.ts";
const [outp, statp] = process.argv.slice(2);
const t0 = Date.now();
const shp = (p: string) => { const z = shapefileFromZip(new Uint8Array(readFileSync(p))); return readShapefile(z.shp, z.dbf); };
const cells = seaExtent(coverageSegments(new Uint8Array(readFileSync("../../bm-obj/coverage.bin"))));
const s = seaSources(shp("../../ne-ocean-dl/ne_10m_ocean.zip"), shp("../../ne-dl/ne_10m_lakes.zip"), shp("../../ne-admin0-dl/ne_10m_admin_0_countries.zip"));
const { byCell, stats } = seaPieces(cells, s);
writeFileSync(outp!, JSON.stringify(byCell));
const out = { extent_cells: cells.size, extent: [...cells].sort(), ocean_outers: s.ocean.length, ocean_holes: s.ocean.reduce((n, p) => n + p.holes.length, 0),
  lakes_read: s.stats.lakesRead, lakes_selected: s.lakes.length, lakes_inside_us: s.stats.inside, lakes_small: s.stats.small, selected_indices: s.lakes.map((l) => l.index),
  holes_not_in_one_outer: s.stats.notInOne, cells_with_sea: Object.keys(byCell).length, pieces: stats.pieces, holes_kept: stats.kept, holes_dropped: stats.dropped,
  vertices: stats.verts, ms: Date.now() - t0, opts: "none (the shipped module has no options)" };
writeFileSync(statp!, JSON.stringify(out));
const { extent, selected_indices, ...brief } = out; console.log(JSON.stringify(brief));
