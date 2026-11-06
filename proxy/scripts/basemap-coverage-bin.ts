// coverage.bin, recomputed from the cached TIGER states file through coverage.ts's own coverageRows (MC's S-BBOX is
// graded against what PLANTED code produces). Its CONTROL: unplanted, it must reproduce the sealed store's coverage.bin
// byte for byte, or this is not the ingest's path and S-BBOX's grade means nothing (exit 2).
//
//   npx tsx scripts/basemap-coverage-bin.ts --cache <ingest cache> --out <file> [--control]
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { coverageRows, encodeCoverage } from "./basemap/coverage";
import { readShapefile, shapefileFromZip } from "./basemap/shapefile";
import { TERRITORY_FIPS } from "./basemap/sources";

const arg = (n: string) => { const i = process.argv.indexOf(n); return i > 0 ? process.argv[i + 1] : undefined; };
const cache = arg("--cache"), out = arg("--out");
if (!cache || !out) throw new Error("--cache <dir> --out <file>");
const store = join(cache, "dry-run", "store");
const manifest = JSON.parse(readFileSync(join(store, "manifest.json"), "utf8")) as { files: Array<{ layer: string; url: string; sha256: string }> };
const st = manifest.files.find((f) => f.layer === "states");
if (!st) throw new Error("no states file in the manifest");
const u = new URL(st.url), bytes = readFileSync(join(cache, "files", u.hostname, ...u.pathname.split("/").filter(Boolean)));
if (createHash("sha256").update(bytes).digest("hex") !== st.sha256) throw new Error("the cached states file is not the one the manifest pins");
const z = shapefileFromZip(new Uint8Array(bytes));
const states = readShapefile(z.shp, z.dbf).filter((r) => !TERRITORY_FIPS.has(r.attrs["STATEFP"]!));
if (states.length !== 51) throw new Error(`${states.length} states + DC, not 51`);
const cov = encodeCoverage(coverageRows(states.map((r) => r.parts.map((xy) => ({ xy })))));
writeFileSync(out, cov);
const sealed = readFileSync(join(store, "coverage.bin"));
const same = Buffer.compare(Buffer.from(cov), sealed) === 0;
console.log(`coverage.bin recomputed: ${cov.length} B, ${same ? "IDENTICAL to" : "DIFFERENT from"} the sealed store's`);
if (process.argv.includes("--control")) process.exit(same ? 0 : 2);
