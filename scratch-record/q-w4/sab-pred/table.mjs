// The full predicted table: every sabotage of the shipped driver (+ (c)'s four, - S-LADDER), its frozen set and its predicted set.
// build() plants (set "all"): (frozen - the rows pq4d grades) + pq4d's red rows. Others: frozen, by reasoning (the doc says which).
import { readFileSync } from "node:fs";
const lf = (t) => t.split("\r\n").join("\n");
const src = lf(readFileSync("wt-code/proxy/scripts/sabotage-basemap.mjs", "utf8"));
const head = src.slice(src.indexOf("const LIB ="), src.indexOf("const SABOTAGES = {"));
const body = src.slice(src.indexOf("const SABOTAGES = {"), src.indexOf("\nconst sha ="));
const SAB = new Function(`${head}\n${body}\nreturn SABOTAGES;`)();
const pred = JSON.parse(readFileSync("sab-pred/predicted.json", "utf8"));
const GRADED = new Set(["MB1", "MB2", "MB3", "MB4", "MB5", "MB6", "MB7", "MB8a", "MB8b", "MB9", "MN1", "MN2", "MN3", "MN4", "MN5", "MN6", "MW13",
  "T-SEA0", "T-SEA0c", "KB2", "BRIDGES", "EDGE2", "KB7C", "XISLAND", "XISLAND-TALL", "REFUSED5", "T-PROV-a", "T-PROV-b", "T-D2", "T-D2c", "T-SEALOG"]);
const BUILD = /src\/basemap\/(frame|layers|terrain|budget|build|provenance)\.ts$/;
const C = new Set(["S-DROPORDER", "S-NOCOUNT", "S-ROADORDER", "S-PRIMARYDROP"]);
let n = 0, changed = 0, flagged = [];
for (const [name, s] of Object.entries(SAB)) {
  if (name === "S-LADDER") { console.log(`| ${name} | ${s.expect.join(", ")} | RETIRED (R12 (c)) | |`); continue; }
  const set = s.set ?? "ingest", files = (s.files ?? [[s.file]]).map(([f]) => f);
  const frozen = s.expect === "BLIND" ? "BLIND" : [...s.expect].sort();
  let out, how;
  if (set === "sea") { out = frozen; how = "SEA-CI, registered"; }
  else if (s.expect === "BLIND") { out = "BLIND"; how = pred[name]?.loadFailed ? "pq4d: fails to load" : "reasoned"; }
  else if (set === "all" && files.every((f) => BUILD.test(f))) {
    const p = pred[name]; if (!p || !p.red) { flagged.push(name); out = "?"; how = "NO pq4d READING"; }
    else { out = [...new Set([...frozen.filter((x) => !GRADED.has(x)), ...p.red])].sort(); how = `pq4d${C.has(name) ? ", re-anchored" : ""}`; }
  } else { out = frozen; how = "reasoned"; if (frozen.some((x) => GRADED.has(x))) flagged.push(`${name} (frozen holds a graded row)`); }
  n++;
  const diff = JSON.stringify(out) !== JSON.stringify(frozen); if (diff) changed++;
  console.log(`| ${name} | ${set} | ${frozen === "BLIND" ? "BLIND" : frozen.join(", ")} | ${out === "BLIND" ? "BLIND" : out.join(", ") || "(empty)"} | ${how}${diff ? " **changed**" : ""} |`);
}
console.log(`\n${n} sabotages graded (S-LADDER retired); ${changed} predicted sets differ from frozen; flagged: ${flagged.length ? flagged.join("; ") : "none"}`);
