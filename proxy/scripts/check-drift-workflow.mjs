// check-drift-workflow.mjs -- the render-drift job stays READ-ONLY.
//
//   node scripts/check-drift-workflow.mjs             # check .github/workflows/photo-drift.yml
//   node scripts/check-drift-workflow.mjs --selftest  # prove every refusal can fire
//
// The job's read-only property is a claim about a YAML file that anyone can
// edit, and the natural edit -- "the read token is missing, borrow the one the
// publish uses" -- would hand a measurement job the credential that writes
// production. So the claim is a check that runs in CI, not a comment:
//
//   1. the only secret referenced is CLOUDFLARE_KV_READ_TOKEN;
//   2. CLOUDFLARE_API_TOKEN (which every script falls back to) is never set;
//   3. every ingest-photos invocation is a --dry-run;
//   4. nothing calls wrangler;
//   5. its concurrency group is not photos-production (a queued drift run there
//      could cancel a waiting publish).
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

export function problems(yml) {
  const out = [];
  // Comments are prose, not configuration; check only what runs.
  const code = yml.split("\n").map((l) => l.replace(/(^|\s)#.*$/, "")).join("\n");
  const secrets = [...code.matchAll(/secrets\.([A-Za-z0-9_]+)/g)].map((m) => m[1]);
  for (const s of new Set(secrets)) if (s !== "CLOUDFLARE_KV_READ_TOKEN") out.push(`references secrets.${s}`);
  if (!secrets.includes("CLOUDFLARE_KV_READ_TOKEN")) out.push("does not use secrets.CLOUDFLARE_KV_READ_TOKEN");
  if (/CLOUDFLARE_API_TOKEN/.test(code)) out.push("sets or reads CLOUDFLARE_API_TOKEN");
  for (const line of code.split("\n")) {
    if (/ingest-photos/.test(line) && !/--dry-run/.test(line)) out.push(`ingest without --dry-run: ${line.trim()}`);
  }
  if (/wrangler/.test(code)) out.push("calls wrangler");
  if (/group:\s*photos-production/.test(code)) out.push("shares the photos-production concurrency group");
  return out;
}

const here = dirname(fileURLToPath(import.meta.url));
const real = readFileSync(join(here, "..", "..", ".github", "workflows", "photo-drift.yml"), "utf8");

if (process.argv.includes("--selftest")) {
  const plants = [
    ["write token", (y) => y.replace("secrets.CLOUDFLARE_KV_READ_TOKEN }}", "secrets.CLOUDFLARE_KV_READ_TOKEN }}\n      X: ${{ secrets.CLOUDFLARE_API_TOKEN }}")],
    ["env var", (y) => y.replace("PHOTO_KV_READ_TOKEN: ${{", "CLOUDFLARE_API_TOKEN: ${{")],
    ["upload run", (y) => y.replace("ingest-photos.ts --dry-run --env", "ingest-photos.ts --env")],
    ["wrangler", (y) => y.replace("run: npm ci", "run: npm ci && npx wrangler whoami")],
    ["group", (y) => y.replace("group: photo-drift", "group: photos-production")],
  ];
  let bad = 0;
  const control = problems(real);
  console.log(`CONTROL photo-drift.yml as committed: ${control.length ? "REFUSED " + control.join("; ") : "passes"}`);
  if (control.length) bad++;
  for (const [name, plant] of plants) {
    const y = plant(real);
    if (y === real) { console.log(`PLANT DID NOT APPLY: ${name}`); bad++; continue; }
    const p = problems(y);
    console.log(`${p.length ? "refused" : "MISSED "} ${name}: ${p.join("; ") || "-"}`);
    if (!p.length) bad++;
  }
  console.log(bad ? `SELFTEST FAILED (${bad})` : `SELFTEST PASSED: control passes, ${plants.length}/${plants.length} plants refused`);
  process.exit(bad ? 1 : 0);
}

const p = problems(real);
for (const x of p) console.error(`photo-drift.yml: ${x}`);
console.log(p.length ? `photo-drift.yml is NOT read-only (${p.length})` : "photo-drift.yml: read-only (1 secret, a KV-read token; dry run only)");
process.exit(p.length ? 1 : 0);
