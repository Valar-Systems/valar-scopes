// SCRATCH (map-server): step8_26.pred Amendment 5d.2's PATH LIST (rule (b)) for terrain26.mts: every input it names, each its own
// literal, resolved as it resolves them (this file sits beside it in mirror/: data against S = new URL("../", import.meta.url), the
// import against the module file). Existence only. The fixed s4after_plants26.mts reads the same paths as before (p5d_paths.sh covers them).
//   (from dry-clone/proxy) node --import tsx ../../mirror/p5d2_paths.mts
import { statSync } from "node:fs";
import { fileURLToPath } from "node:url";

const S = new URL("../", import.meta.url);
const HOMES = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC", "BUFFALO", "ROUSES", "ZAPATA"];
const items: Array<[string, URL]> = [["terrain26 :14 import", new URL("../dry-clone/proxy/src/basemap/format.ts", import.meta.url)]];
for (const h of HOMES) { items.push([`terrain26 NEW ${h}`, new URL(`tb26-out/b/${h}-tb.bmpk`, S)]); items.push([`terrain26 OLD ${h}`, new URL(`cp-out/${h}-sea.bmpk`, S)]); }
let missing = 0;
for (const [label, u] of items) { try { if (!statSync(fileURLToPath(u)).isFile()) throw new Error(); } catch { missing++; console.log(`MISSING ${label}`); } }
console.log(`PATHS 5d.2: ${items.length - missing} present, ${missing} missing (of ${items.length})`);
process.exit(missing === 0 ? 0 : 1);
