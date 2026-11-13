// Read-only: for every sabotage in the driver, count each anchor's occurrences in the tree as CI (LF) sees it. Writes nothing.
//   node anchor-check.mjs <proxy dir>
import { readFileSync } from "node:fs";
import { join } from "node:path";
const PROXY = process.argv[2];
const lf = (t) => t.split("\r\n").join("\n");
const src = lf(readFileSync(process.argv[3] ?? join(PROXY, "scripts/sabotage-basemap.mjs"), "utf8"));
const head = src.slice(src.indexOf("const LIB ="), src.indexOf("const SABOTAGES = {"));
const body = src.slice(src.indexOf("const SABOTAGES = {"), src.indexOf("\nconst sha ="));
const S = new Function(`${head}\n${body}\nreturn SABOTAGES;`)();
let applies = 0, stale = 0;
for (const [name, s] of Object.entries(S)) {
  const counts = (s.files ?? [[s.file, s.edits]]).flatMap(([file, edits]) => { let text; try { text = lf(readFileSync(join(PROXY, file), "utf8")); } catch { return edits.map(() => "nofile"); } return edits.map(([a]) => text.split(a).length - 1); });
  const ok = counts.every((n) => n === 1);
  ok ? applies++ : stale++;
  console.log(`${name.padEnd(16)} ${(s.files ? s.files.map(([f]) => f).join("+") : s.file).padEnd(32)} ${counts.join(",").padEnd(8)} ${ok ? "applies" : "STALE"}`);
}
console.log(`total ${applies + stale}: ${applies} apply, ${stale} stale`);
