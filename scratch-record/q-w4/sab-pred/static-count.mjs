// Static: every literal test title in proxy/test/basemap-*.test.ts, through the driver's own caseOf, the skipIf row (MD2, skipped in
// workerd) left out -- the distinct case ids baseline[all] should see, and per file. Usage: node static-count.mjs <proxy dir>
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
const P = process.argv[2];
const src = readFileSync(join(P, "scripts/sabotage-basemap.mjs"), "utf8").split("\r\n").join("\n");
const caseOf = new Function(`${/const caseOf = [^\n]*/.exec(src)[0]}\nreturn caseOf;`)();
const files = readdirSync(join(P, "test")).filter((n) => n.startsWith("basemap-") && n.endsWith(".test.ts")).sort();
const ids = new Set(), per = {};
let titles = 0, skipped = 0;
for (const f of files) {
  const t = readFileSync(join(P, "test", f), "utf8");
  for (const m of t.matchAll(/\bit(\.skipIf\([^)]*\))?\(\s*(["'`])((?:(?!\2).)*)\2/g)) {
    if (m[1]) { skipped++; continue; }
    titles++; const id = caseOf(m[3]); ids.add(id); (per[f] ??= new Set()).add(id);
  }
}
console.log(`${files.length} files; ${titles} titles (+${skipped} skipIf); ${ids.size} distinct case ids; sea file ${per["basemap-sea.test.ts"]?.size ?? 0}`);
