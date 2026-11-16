// SCRATCH (map-server): step8_26.pred Amendment 5c.3's PATH LIST (rule (b)): every input path of d2d3_26.mts and d4_26.mts, resolved
// EXACTLY as they resolve it. This file sits beside them in mirror/, so import.meta.url gives the same bases:
//   data:    S = new URL("../", import.meta.url) (the scratchpad); rd(p) and readFileSync(new URL(p, S)) read new URL(p, S)
//   imports: a module specifier resolves against the importing file, new URL(spec, import.meta.url)
// Each string below is the tool's own literal, with its line. Exists-only (statSync); nothing is opened for reading. Prints counts and
// any missing path; exit 0 only with 0 missing.
//   (from dry-clone/proxy) node --import tsx ../../mirror/p5c3_paths.mts
import { statSync } from "node:fs";
import { fileURLToPath } from "node:url";

const S = new URL("../", import.meta.url);
const H16 = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC", "BUFFALO", "ROUSES", "ZAPATA"];
const H11 = ["KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KDTW", "BUFFALO", "ROUSES", "ZAPATA"];
const items: Array<[string, URL]> = [];
const data = (tool: string, p: string) => items.push([`${tool} data ${p}`, new URL(p, S)]);
const mod = (tool: string, spec: string) => items.push([`${tool} import ${spec}`, new URL(spec, import.meta.url)]);
// d2d3_26.mts
for (const m of ["build.ts", "frame.ts", "store.ts", "query.ts", "format.ts"]) mod("d2d3 :15-19", `../dry-clone/proxy/src/basemap/${m}`);
data("d2d3 :34", "sea-obj/manifest.json");
data("d2d3 :36", "sea-obj/index.json");
data("d2d3 :37", "../bm-cache-qw4/dry-run/store/sea-regions.json");
for (const h of H16) { data("d2d3 :30 new", `tb26-out/b/${h}-tb.bmpk`); data("d2d3 :30 old", `cp-out/${h}-sea.bmpk`); }
// d4_26.mts (the 11, and KJFK's NEW for its controls, which is among them)
mod("d4 :19", "../dry-clone/proxy/src/basemap/format.ts");
for (const h of H11) {
  data("d4 new", `tb26-out/b/${h}-tb.bmpk`); data("d4 new report", `tb26-out/b/${h}-tb.report.json`);
  data("d4 ctl", `tb26-out/ctl/${h}-ctl.bmpk`); data("d4 ctl report", `tb26-out/ctl/${h}-ctl.report.json`); data("d4 old", `cp-out/${h}-sea.bmpk`);
}
let missing = 0;
for (const [label, u] of items) { try { if (!statSync(fileURLToPath(u)).isFile()) throw new Error(); } catch { missing++; console.log(`MISSING ${label}`); } }
console.log(`PATHS 5c.3: ${items.length - missing} present, ${missing} missing (of ${items.length})`);
process.exit(missing === 0 ? 0 : 1);
