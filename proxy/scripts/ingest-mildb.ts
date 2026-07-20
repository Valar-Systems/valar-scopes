/**
 * ingest-mildb.ts -- the military airframe side-table loader (work queue P2).
 *
 * Downloads the Mictronics aircraft-database export (ODC-By 1.0 -- attribution
 * is carried on the device config page next to the adsb.lol ODbL line), filters
 * it to military airframes, and bulk-loads them into KV as `mil:<hex>` rows the
 * enrich path consults when the live DB record resolves empty. Re-running is
 * idempotent: rows are overwritten in place (no TTL), so a weekly re-run tracks
 * the upstream's refresh cadence.
 *
 *   npm run ingest:mildb -- --dry-run             # download + filter + stats, no KV
 *   npm run ingest:mildb -- --env staging         # bulk-load staging KV
 *   npm run ingest:mildb -- --env staging --file indexedDB_old.zip  # offline source
 *
 * Selection: an entry is loaded when the export flags it military (f[0]==='1')
 * OR its hex sits in a VRS military allocation (the same table the serve-time
 * operator floor uses -- imported from src/military.ts, never re-implemented),
 * AND it actually contributes something (a registration or a type). ~17k rows,
 * ~74k KV writes/month at the weekly cadence -- inside the paid plan's included
 * 1M/month, but far over the free tier's 1k/day (see the README cost model).
 */
import { execSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { unzipSync } from "fflate";
import { militaryOperator } from "../src/military";

const EXPORT_URL =
  "https://raw.githubusercontent.com/Mictronics/aircraft-database/master/indexedDB_old.zip";

// Cloudflare's KV bulk API takes at most 10,000 pairs per request; wrangler
// does not chunk for us.
const BULK_CHUNK = 10_000;

// Shape of one aircrafts.json value (uppercase-hex keys):
//   r = registration/serial, t = ICAO type, d = description,
//   f = two-char flag string, f[0]==='1' -> military.
interface SrcEntry {
  r?: string;
  t?: string;
  f?: string;
  d?: string;
}

interface Args {
  env?: string;
  dryRun: boolean;
  file?: string;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--env") a.env = argv[++i];
    else if (v === "--dry-run") a.dryRun = true;
    else if (v === "--file") a.file = argv[++i];
    else throw new Error(`unknown argument: ${v}`);
  }
  if (!a.dryRun && !a.env) throw new Error("an upload run needs --env <name> (or use --dry-run)");
  return a;
}

// Same shell-quoting workaround as ingest-photos.ts: Windows Node won't spawn
// the npx/wrangler .cmd shims via execFileSync, so build a quoted string.
function q(s: string): string {
  return `"${s.replace(/"/g, '\\"')}"`;
}

async function loadExport(file?: string): Promise<Record<string, SrcEntry>> {
  let zip: Uint8Array;
  if (file) {
    zip = new Uint8Array(readFileSync(file));
  } else {
    console.log(`downloading ${EXPORT_URL} ...`);
    const res = await fetch(EXPORT_URL);
    if (!res.ok) throw new Error(`download failed: ${res.status}`);
    zip = new Uint8Array(await res.arrayBuffer());
  }
  const files = unzipSync(zip);
  const raw = files["aircrafts.json"];
  if (!raw) throw new Error("aircrafts.json not found in the export zip");
  return JSON.parse(new TextDecoder().decode(raw)) as Record<string, SrcEntry>;
}

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
  const db = await loadExport(args.file);

  const rows: { key: string; value: string }[] = [];
  let flagged = 0;
  let blockOnly = 0;
  let skippedEmpty = 0;
  for (const [hexUpper, e] of Object.entries(db)) {
    const hex = hexUpper.toLowerCase();
    if (!/^[0-9a-f]{6}$/.test(hex)) continue;
    const isFlagged = typeof e.f === "string" && e.f[0] === "1";
    const inBlock = militaryOperator(hex) !== "";
    if (!isFlagged && !inBlock) continue;
    const r = (e.r ?? "").trim();
    // Keep only the leading [A-Z0-9] run: the source marks an unconfirmed type
    // with a trailing " ?" ("P8 ?"), which would break the type-keyed name/photo
    // joins downstream (see normType in src/enrich.ts).
    const t = ((e.t ?? "").toUpperCase().match(/[A-Z0-9]+/)?.[0]) ?? "";
    const tn = (e.d ?? "").trim();
    if (!r && !t) {
      skippedEmpty++;
      continue; // nothing the card could render -- the operator floor already covers it
    }
    if (isFlagged) flagged++;
    else blockOnly++;
    // Compact row: omit empty fields; the reader treats absences as "".
    const row: Record<string, string> = {};
    if (r) row.r = r;
    if (t) row.t = t;
    if (tn) row.tn = tn;
    rows.push({ key: `mil:${hex}`, value: JSON.stringify(row) });
  }

  console.log(
    `selected ${rows.length} rows (${flagged} mil-flagged, ${blockOnly} by allocation block only; ` +
      `${skippedEmpty} skipped with no reg/type) from ${Object.keys(db).length} export entries`,
  );

  if (args.dryRun || !args.env) {
    console.log("dry run: no KV writes");
    return;
  }

  const tmp = mkdtempSync(join(tmpdir(), "blip-mildb-"));
  for (let i = 0; i < rows.length; i += BULK_CHUNK) {
    const chunk = rows.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk));
    console.log(`bulk put ${chunk.length} rows (${i + chunk.length}/${rows.length}) ...`);
    execSync(
      ["npx", "wrangler", "kv", "bulk", "put", q(path), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(
        " ",
      ),
      { stdio: "inherit" },
    );
  }
  console.log(`done: ${rows.length} mil:<hex> rows loaded to ${args.env}`);
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
