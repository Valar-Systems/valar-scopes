// Read-only: apply every sabotage's edits IN MEMORY (LF text, as CI sees it) and parse each planted file with esbuild. Writes nothing.
//   node plant-parse.mjs <proxy dir>
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { join, resolve } from "node:path";
const PROXY = resolve(process.argv[2]);
const esbuild = createRequire(join(PROXY, "package.json"))("esbuild");
const lf = (t) => t.split("\r\n").join("\n");
const src = lf(readFileSync(join(PROXY, "scripts/sabotage-basemap.mjs"), "utf8"));
const head = src.slice(src.indexOf("const LIB ="), src.indexOf("const SABOTAGES = {"));
const body = src.slice(src.indexOf("const SABOTAGES = {"), src.indexOf("\nconst sha ="));
const S = new Function(`${head}\n${body}\nreturn SABOTAGES;`)();
let parsed = 0, failed = 0, skipped = 0;
for (const [name, s] of Object.entries(S)) {
  for (const [file, edits] of s.files ?? [[s.file, s.edits]]) {
    let t = lf(readFileSync(join(PROXY, file), "utf8"));
    if (!edits.every(([a]) => t.split(a).length - 1 === 1)) { skipped++; console.log(`${name} ${file}: anchors stale, not parsed`); continue; }
    for (const [a, b] of edits) t = t.replace(a, () => b);
    if (!/\.(ts|mjs|js)$/.test(file)) { skipped++; continue; }
    try { esbuild.transformSync(t, { loader: file.endsWith(".ts") ? "ts" : "js", format: "esm" }); parsed++; }
    catch (e) { failed++; console.log(`${name} ${file}: PARSE ERROR ${String(e.message).split("\n").slice(0, 3).join(" | ")}`); }
  }
}
console.log(`planted files: ${parsed} parse, ${failed} do not, ${skipped} skipped (stale or not code)`);
