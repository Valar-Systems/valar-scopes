// SCRATCH, read-only: the 8 manifest notes, through the code PR's own writeManifest (MI4b's checkNotes). Prints the notes block as
// written. CONTROL first: the same notes reversed must be REFUSED, so a pass is the check's, not a bypass. Nothing fetched or written.
//   (from wt-main/proxy) node --import tsx ../../mirror/notes.mts
import { readFileSync } from "node:fs";
const L = await import(new URL("../wt-code/proxy/scripts/basemap/ingest-lib.ts", import.meta.url).href);
const WHY = "fetched from the Census Bureau's mirror; www2.census.gov's firewall refused it";
const eight = readFileSync(new URL("../seastore/logs/2-fetch.err", import.meta.url), "utf8").split("\n").filter((l) => l.startsWith("FAILED ")).map((l) => l.split(" ")[1]!.replace(/:$/, ""));
const bm = new Map((JSON.parse(readFileSync(new URL("../bm-obj/manifest.json", import.meta.url), "utf8")).files as any[]).map((f) => [f.url, f]));
const files = eight.map((u) => { const b = bm.get(u)!; return { bytes: b.bytes, layer: b.layer, sha256: b.sha256, url: u.replace("https://www2.census.gov/geo/tiger/", "https://ftp2.census.gov/geo/tiger/"), vintage: b.vintage }; });
const notes = files.map((f) => `${f.url}: ${WHY}`).sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
let refused = "";
try { L.writeManifest({ builder: "0".repeat(40), files, predicates: [], notes: [...notes].reverse() }); } catch (e) { refused = (e as Error).message; }
console.log(`CONTROL (reversed): ${refused ? `refused -- ${refused}` : "ACCEPTED (the check did not run)"}`);
if (!refused) process.exit(1);
const t = L.writeManifest({ builder: "0".repeat(40), files, predicates: [], notes });
const back = JSON.parse(t);
console.log(`written: keys ${Object.keys(back).join(",")}; notes ${back.notes.length}; round trip ${L.writeManifest(back) === t}`);
for (const n of back.notes) console.log(`  ${n}`);
