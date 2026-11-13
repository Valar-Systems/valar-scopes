// SCRATCH, read-only: form (d)'s 8 notes, taken from the REHEARSED plan (mirror/rehearse-d/cache/plan.json: primary url + note), through
// the code PR's own writeManifest with the 8 entries as bm-obj records them (primary urls). The note is built the way the dry run builds
// it (ingest-basemap.ts L379: `${f.url}: ${f.note}`, byte-sorted). CONTROL first: the orchestrator's example wording, which carries a
// date ("... the record of 2026-10-28"), must be REFUSED by MI4b's no-wall-clock rule. Nothing fetched or written.
//   (from wt-main/proxy) node --import tsx ../../mirror/notes_d.mts
import { readFileSync } from "node:fs";
const L = await import(new URL("../wt-code/proxy/scripts/basemap/ingest-lib.ts", import.meta.url).href);
const plan = JSON.parse(readFileSync(new URL("./rehearse-d/cache/plan.json", import.meta.url), "utf8")).files as Array<{ layer: string; url: string; note?: string }>;
const noted = plan.filter((f) => f.note);
if (noted.length !== 8) throw new Error(`${noted.length} noted plan entries, not 8`);
const bm = new Map((JSON.parse(readFileSync(new URL("../bm-obj/manifest.json", import.meta.url), "utf8")).files as any[]).map((f) => [f.url, f]));
const files = noted.map((f) => { const b = bm.get(f.url); if (!b) throw new Error(`bm-obj lacks ${f.url}`); return { bytes: b.bytes, layer: b.layer, sha256: b.sha256, url: b.url, vintage: b.vintage }; });
const sortB = (xs: string[]) => xs.sort((a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)));
const notes = sortB(noted.map((f) => `${f.url}: ${f.note}`));
const dated = sortB(noted.map((f) => `${f.url}: fetched via the Census Bureau's mirror ${f.url.replace("//www2.", "//ftp2.")} (www2.census.gov's firewall refused it); bytes equal to the record of 2026-10-28`));
let refused = "";
try { L.writeManifest({ builder: "0".repeat(40), files, predicates: [], notes: dated }); } catch (e) { refused = (e as Error).message; }
console.log(`CONTROL (the dated wording): ${refused ? `refused -- ${refused}` : "ACCEPTED (the rule did not run)"}`);
if (!refused) process.exit(1);
const t = L.writeManifest({ builder: "0".repeat(40), files, predicates: [], notes });
const back = JSON.parse(t);
console.log(`written: keys ${Object.keys(back).join(",")}; notes ${back.notes.length}; round trip ${L.writeManifest(back) === t}; every note's url a primary url: ${back.notes.every((n: string) => n.startsWith("https://www2.census.gov/"))}`);
for (const n of back.notes) console.log(`  ${n}`);
