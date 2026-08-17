/**
 * photo-gaps.ts -- what to photograph next, ranked by what the fleet actually looks at.
 *
 *   CLOUDFLARE_API_TOKEN=... npm run photo-gaps [-- --days 7 --limit 40 --env production]
 *
 * WHY THIS EXISTS RATHER THAN THE BARE SQL IT REPLACES.
 *
 * The documented query used to be a raw Analytics Engine statement, and a raw
 * `enrich_gap` list is WRONG IN A WAY THAT LOOKS RIGHT. `recordEnrichGap` fires at
 * request time, so the dataset is a log of PAST STATES: a type that had no photo on
 * Tuesday is still in there on Friday with a photo published in between. Nothing about
 * the row says which.
 *
 * Measured 2026-08-14, the day the square variants were published: 34% of the raw
 * list BY LOOKUPS was already fixed, and the raw ranking put C152 first -- a type that
 * had had a photograph the whole time. Sourcing off that list starts by acquiring a
 * photo we already own, and nothing in the output hints at it.
 *
 * So re-validation is not a caveat printed beside the list, it is a step the list
 * cannot be produced without: every candidate is checked against the PUBLISHED
 * manifest (the artifact, not photos/manifest.json, which is intent) and against
 * TYPE_PHOTO_ALIAS parsed out of photos.ts.
 *
 * AND IT FAILS CLOSED. If the manifest cannot be read, this prints NO RANKING at all
 * and exits non-zero. An unvalidated list is indistinguishable from a validated one on
 * sight, and the whole failure being fixed here is someone acting on the wrong one.
 * See CLAUDE.md, "a guard that can tell you a result is untrustworthy should also be
 * able to tell you why".
 */
import { execSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { MANIFEST_KEY, type ManifestEntry } from "../src/photolicense";

const HERE = dirname(fileURLToPath(import.meta.url));
const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"; // same as wrangler.toml account_id

interface Args {
  days: number;
  limit: number;
  env: string;
  gap: "photo" | "name" | "type";
}

function parseArgs(argv: string[]): Args {
  const a: Args = { days: 7, limit: 40, env: "production", gap: "photo" };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--days") a.days = Number(argv[++i]) || 7;
    else if (argv[i] === "--limit") a.limit = Number(argv[++i]) || 40;
    else if (argv[i] === "--env") a.env = argv[++i] ?? "production";
    else if (argv[i] === "--gap") a.gap = (argv[++i] as Args["gap"]) ?? "photo";
  }
  return a;
}

const DATASET: Record<string, string> = {
  production: "blipscope_proxy",
  staging: "blipscope_proxy_staging",
};

/** Ranked gap rows straight from Analytics Engine -- unvalidated, never printed as-is. */
async function queryGaps(args: Args, token: string): Promise<{ code: string; n: number }[]> {
  const sql =
    `SELECT blob3 AS type, SUM(_sample_interval) AS lookups ` +
    `FROM ${DATASET[args.env] ?? DATASET.production} ` +
    `WHERE blob1 = 'enrich_gap' AND blob2 = '${args.gap}' ` +
    `AND timestamp > NOW() - INTERVAL '${args.days}' DAY ` +
    // No LIMIT. The cumulative figure is the entire point -- it answers "where do I
    // stop" -- and a LIMIT computes it against a truncated denominator, which reads
    // as a much steeper curve than the real one.
    `GROUP BY type ORDER BY lookups DESC`;
  const res = await fetch(`https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}/analytics_engine/sql`, {
    method: "POST",
    headers: { Authorization: `Bearer ${token}` },
    body: sql,
  });
  if (!res.ok) {
    throw new Error(`Analytics Engine returned ${res.status}: ${(await res.text()).slice(0, 400)}`);
  }
  const body = (await res.json()) as { data?: { type?: string; lookups?: string }[] };
  return (body.data ?? [])
    .map((r) => ({ code: String(r.type ?? "").trim().toUpperCase(), n: Number(r.lookups ?? 0) }))
    .filter((r) => r.code.length > 0);
}

/** The PUBLISHED library, or null. Null must stop the run -- see the header. */
function publishedTypes(env: string): Set<string> | null {
  try {
    const out = execSync(
      `npx wrangler kv key get "${MANIFEST_KEY}" --binding=ENRICH_KV --env=${env} --remote --text`,
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 64 * 1024 * 1024, cwd: join(HERE, "..") },
    );
    const start = out.indexOf("[");
    if (start < 0) return null;
    const rows = JSON.parse(out.slice(start)) as ManifestEntry[];
    if (!Array.isArray(rows) || rows.length === 0) return null;
    return new Set(rows.filter((r) => r.kind === "type").map((r) => r.target.toUpperCase()));
  } catch (err) {
    const e = err as { message?: string; stderr?: string };
    console.error(`  manifest read failed: ${e.message ?? String(err)}`);
    if (e.stderr) console.error(`  wrangler stderr: ${String(e.stderr).slice(0, 600)}`);
    return null;
  }
}

/**
 * TYPE_PHOTO_ALIAS, parsed from the source that serves it rather than restated here.
 * A type covered by an alias is NOT a gap, and a copy of the table in this file would
 * drift the first time someone edited one of them.
 */
function aliasTable(): Record<string, string> {
  const src = readFileSync(join(HERE, "..", "src", "photos.ts"), "utf8");
  const at = src.indexOf("TYPE_PHOTO_ALIAS");
  if (at < 0) return {};
  const block = src.slice(at, src.indexOf("};", at));
  const out: Record<string, string> = {};
  for (const m of block.matchAll(/([A-Z0-9]{2,8})\s*:\s*"([A-Z0-9]{2,8})"/g)) out[m[1]] = m[2];
  return out;
}

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
  const token = process.env.CLOUDFLARE_API_TOKEN ?? "";
  if (!token) {
    console.error("CLOUDFLARE_API_TOKEN is not set (needs Account Analytics Read + Workers KV Read).");
    process.exit(2);
  }

  // RE-VALIDATION FIRST, so the expensive query is never run against a library we
  // cannot check it against -- and so the failure arrives before any output that
  // could be mistaken for a work list.
  const have = publishedTypes(args.env);
  if (!have) {
    console.error("");
    console.error("REFUSING TO RANK: the published manifest could not be read.");
    console.error("  Every candidate has to be re-checked against the CURRENT library, because");
    console.error("  enrich_gap points record what was true when the request was served. On");
    console.error("  2026-08-14 that was 34% of the raw list, and it ranked a type we already");
    console.error("  owned first. An unvalidated ranking looks exactly like a valid one.");
    console.error("  Fix the KV read above and re-run.");
    process.exit(1);
  }
  const alias = aliasTable();
  const covered = (c: string): boolean => have.has(c) || (alias[c] !== undefined && have.has(alias[c]));

  const raw = await queryGaps(args, token);
  const stale = raw.filter((r) => covered(r.code));
  const real = raw.filter((r) => !covered(r.code));
  const staleN = stale.reduce((a, b) => a + b.n, 0);
  const realN = real.reduce((a, b) => a + b.n, 0);

  console.log(`\n=== ${args.gap} gaps, last ${args.days}d, ${args.env} ===`);
  console.log(`  library     : ${have.size} types published, ${Object.keys(alias).length} alias entries`);
  console.log(`  raw rows    : ${raw.length} types / ${staleN + realN} lookups`);
  console.log(`  ALREADY FIXED: ${stale.length} types / ${staleN} lookups` +
    (staleN ? `  (${((100 * staleN) / (staleN + realN)).toFixed(1)}% of the raw list -- dropped)` : ""));
  console.log(`  REAL GAPS   : ${real.length} types / ${realN} lookups\n`);

  if (real.length === 0) {
    console.log("  nothing to source.\n");
    return;
  }

  console.log("  rank  type    lookups   cum% of the remaining gap");
  let cum = 0;
  real.slice(0, args.limit).forEach((g, i) => {
    cum += g.n;
    console.log(
      "  " + String(i + 1).padStart(4) + "  " + g.code.padEnd(7) +
      String(g.n).padStart(7) + ((100 * cum) / realN).toFixed(1).padStart(22) + "%",
    );
  });

  // WHERE TO STOP is the output that matters; a ranked list alone implies the whole
  // list is work. These two numbers are what say it is not.
  let n50 = 0, n90 = 0, c = 0;
  for (let i = 0; i < real.length; i++) {
    c += real[i].n;
    if (!n50 && c >= realN * 0.5) n50 = i + 1;
    if (!n90 && c >= realN * 0.9) { n90 = i + 1; break; }
  }
  console.log(`\n  ${n50} type(s) cover 50% of the remaining gap; ${n90} cover 90%.`);
  if (real.length > args.limit) console.log(`  (${real.length - args.limit} more below the --limit cut)`);
  console.log("");
}

main().catch((err) => {
  console.error(String(err));
  process.exit(1);
});
