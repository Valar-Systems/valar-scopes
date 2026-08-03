#!/usr/bin/env node
/**
 * embed-fonts.mjs -- bundle fonts/*.woff2 into the Worker as a servable map.
 *
 * Workers have no filesystem, so the bytes have to be in the bundle. base64 is
 * ~33 percent larger in source, but it is decoded ONCE at module scope (per
 * isolate, not per request) into a Uint8Array, so the request path just hands
 * back a buffer it already has.
 *
 * WOFF2 is brotli-compressed already, so this does not compress further -- the
 * ~86 KB is real bundle weight, against a 1 MB (free) / 10 MB (paid) limit.
 *
 *   node scripts/embed-fonts.mjs           regenerate
 *   node scripts/embed-fonts.mjs --check   fail if stale (runs in CI)
 */
import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const FONTS = join(HERE, "..", "fonts");
const OUT = join(HERE, "..", "src", "fonts.generated.ts");

const files = readdirSync(FONTS).filter((f) => f.endsWith(".woff2")).sort();
if (!files.length) {
  console.error("no .woff2 in fonts/ -- run scripts/fetch-fonts.py first");
  process.exit(2);
}

let body =
  "// GENERATED FILE -- DO NOT EDIT.\n" +
  "// Source: proxy/fonts/*.woff2 (regenerate those with scripts/fetch-fonts.py,\n" +
  "// then this with `node scripts/embed-fonts.mjs`). CI fails if it is stale.\n" +
  "//\n" +
  "// Decoded once at module scope: an isolate pays this on first use, not per\n" +
  "// request. Served from /fonts/<name> with a one-year immutable cache, so\n" +
  "// CHANGING A FONT MEANS CHANGING ITS FILENAME -- an immutable response that\n" +
  "// changes content is a cache nobody can clear.\n\n" +
  "const B64: Record<string, string> = {\n";

let total = 0;
for (const f of files) {
  const buf = readFileSync(join(FONTS, f));
  total += buf.length;
  body += `  ${JSON.stringify(f)}: ${JSON.stringify(buf.toString("base64"))},\n`;
}

body +=
  "};\n\n" +
  "function decode(b64: string): Uint8Array {\n" +
  "  const bin = atob(b64);\n" +
  "  const out = new Uint8Array(bin.length);\n" +
  "  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);\n" +
  "  return out;\n" +
  "}\n\n" +
  "export const FONTS: Record<string, Uint8Array> = Object.fromEntries(\n" +
  "  Object.entries(B64).map(([k, v]) => [k, decode(v)]),\n" +
  ");\n";

if (process.argv.includes("--check")) {
  let current = "";
  try {
    current = readFileSync(OUT, "utf8");
  } catch {
    console.error(`${OUT} is missing. Run: node scripts/embed-fonts.mjs`);
    process.exit(1);
  }
  if (current.replace(/\r\n/g, "\n") !== body.replace(/\r\n/g, "\n")) {
    console.error("src/fonts.generated.ts is STALE. Run: node scripts/embed-fonts.mjs");
    process.exit(1);
  }
  console.log(`fonts.generated.ts is current (${files.length} font(s), ${total} B)`);
  process.exit(0);
}

writeFileSync(OUT, body);
console.log(`embedded ${files.length} font(s), ${total} B (${(total / 1024).toFixed(1)} KB)`);
for (const f of files) console.log(`  /fonts/${f}`);
