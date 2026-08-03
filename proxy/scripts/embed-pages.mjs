#!/usr/bin/env node
/**
 * embed-pages.mjs -- turn pages/*.html into a TypeScript module the Worker serves.
 *
 * WHY THE HTML IS A REAL FILE. The leaderboard page is edited as a page: opened
 * in a browser, tweaked, reloaded. Keeping it inside a TS string literal would
 * mean editing HTML through escaping, and every diff would be one enormous line.
 * So it lives at pages/leaderboard.html, and this generates the module.
 *
 * ESCAPING IS THE WHOLE JOB. The page's own JS uses template literals, so the
 * source contains 22 backticks and 16 `${` sequences. Dropped into a TS template
 * literal unescaped, the first backtick ends the string and the file becomes
 * syntactically broken in a way whose error message points nowhere near the HTML.
 * Order matters: backslashes first, or the escapes we add get re-escaped.
 *
 *   node scripts/embed-pages.mjs           regenerate
 *   node scripts/embed-pages.mjs --check   fail if the generated file is stale
 *
 * --check runs in CI. Without it the failure mode is silent and slow: someone
 * edits the HTML, forgets to regenerate, tests still pass against the OLD page,
 * and the deploy ships markup nobody has looked at.
 */
import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const PAGES = join(HERE, "..", "pages");
const OUT = join(HERE, "..", "src", "pages.generated.ts");

const escapeForTemplate = (s) =>
  // Normalise line endings first so the OUTPUT is identical on every platform.
  // The working tree is CRLF on Windows and LF in CI, and without this the
  // generated module differs by checkout rather than by content -- which would
  // make --check fail in CI on a file nobody touched.
  s.replace(/\r\n/g, "\n")
   .replace(/\\/g, "\\\\")   // backslashes before the escapes we are about to add
   .replace(/`/g, "\\`")
   .replace(/\$\{/g, "\\${");

const files = readdirSync(PAGES).filter((f) => f.endsWith(".html")).sort();
if (!files.length) {
  console.error("no .html files in pages/ -- nothing to embed");
  process.exit(2);
}

const constName = (f) =>
  f.replace(/\.html$/, "").replace(/[^a-zA-Z0-9]+(.)/g, (_, c) => c.toUpperCase()) + "Html";

let out =
  "// GENERATED FILE -- DO NOT EDIT.\n" +
  "// Source: proxy/pages/*.html; regenerate with `node scripts/embed-pages.mjs`.\n" +
  "// CI fails if this is out of date (scripts/embed-pages.mjs --check), so editing\n" +
  "// it here instead of the .html would be overwritten and is never what you want.\n\n";

for (const f of files) {
  const html = readFileSync(join(PAGES, f), "utf8");
  out += `export const ${constName(f)} = \`${escapeForTemplate(html)}\`;\n\n`;
}

if (process.argv.includes("--check")) {
  let current = "";
  try {
    current = readFileSync(OUT, "utf8");
  } catch {
    console.error(`${OUT} is missing. Run: node scripts/embed-pages.mjs`);
    process.exit(1);
  }
  // Normalise line endings so a CRLF checkout does not read as a stale file.
  if (current.replace(/\r\n/g, "\n") !== out.replace(/\r\n/g, "\n")) {
    console.error(
      "src/pages.generated.ts is STALE -- pages/*.html changed without regenerating.\n" +
      "Run: node scripts/embed-pages.mjs",
    );
    process.exit(1);
  }
  console.log(`pages.generated.ts is current (${files.length} page(s))`);
  process.exit(0);
}

writeFileSync(OUT, out);
for (const f of files) {
  const n = readFileSync(join(PAGES, f), "utf8").length;
  console.log(`embedded ${f} -> ${constName(f)} (${n.toLocaleString()} chars)`);
}
