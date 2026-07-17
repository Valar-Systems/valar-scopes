/**
 * ingest-airports.ts -- loads the OurAirports dataset into the KV tiles that
 * back GET /v1/airports (the airport overlay's long tail beyond the firmware's
 * baked majors table).
 *
 *   npm run ingest:airports -- --dry-run      # download + filter + stats, no KV
 *   npm run ingest:airports -- --env staging  # bulk-load staging KV
 *   npm run ingest:airports -- --env staging --file airports.csv  # offline source
 *
 * Source: https://ourairports.com/data/ (public domain / released to the
 * public without restriction -- no attribution gate needed, though we credit
 * it in the README). Selection: open large/medium/small airports only --
 * heliports, seaplane bases, balloonports and closed fields are dropped; a
 * desk flight radar draws fixed-wing geography. Code preference: IATA, else
 * the FAA-style local code, else the ident, first 4 chars -- what a local
 * spotter recognises (the RDM/BDN lesson).
 *
 * Tile format (1-degree grid, matching src/airports.ts):
 *   apt:<floor(lat)>:<floor(lon)> -> [[lat, lon, code, kind], ...]  kind L/M/S
 */
import { execSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const CSV_URL = "https://davidmegginson.github.io/ourairports-data/airports.csv";
const BULK_CHUNK = 10_000; // KV bulk API pair cap per request

const KINDS: Record<string, string> = {
  large_airport: "L",
  medium_airport: "M",
  small_airport: "S",
};

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

function q(s: string): string {
  return `"${s.replace(/"/g, '\\"')}"`;
}

// Minimal RFC-4180 CSV row parser (OurAirports quotes names with commas).
function parseCsvLine(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (inQuotes) {
      if (c === '"' && line[i + 1] === '"') {
        cur += '"';
        i++;
      } else if (c === '"') inQuotes = false;
      else cur += c;
    } else if (c === '"') inQuotes = true;
    else if (c === ",") {
      out.push(cur);
      cur = "";
    } else cur += c;
  }
  out.push(cur);
  return out;
}

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
  let csv: string;
  if (args.file) {
    csv = readFileSync(args.file, "utf8");
  } else {
    console.log(`downloading ${CSV_URL} ...`);
    const res = await fetch(CSV_URL);
    if (!res.ok) throw new Error(`download failed: ${res.status}`);
    csv = await res.text();
  }

  const lines = csv.split("\n");
  const header = parseCsvLine(lines[0]!);
  const col = (name: string) => {
    const i = header.indexOf(name);
    if (i < 0) throw new Error(`column ${name} missing from CSV header`);
    return i;
  };
  const cType = col("type");
  const cLat = col("latitude_deg");
  const cLon = col("longitude_deg");
  const cIata = col("iata_code");
  const cLocal = col("local_code");
  const cIdent = col("ident");

  const tiles = new Map<string, [number, number, string, string][]>();
  let total = 0;
  const kindCounts: Record<string, number> = { L: 0, M: 0, S: 0 };
  for (let i = 1; i < lines.length; i++) {
    const line = lines[i]!;
    if (!line.trim()) continue;
    const f = parseCsvLine(line);
    const kind = KINDS[f[cType] ?? ""];
    if (!kind) continue;
    const lat = parseFloat(f[cLat] ?? "");
    const lon = parseFloat(f[cLon] ?? "");
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) continue;
    const code = (f[cIata] || f[cLocal] || f[cIdent] || "").trim().toUpperCase().slice(0, 4);
    if (!code) continue;
    const key = `apt:${Math.floor(lat)}:${Math.floor(lon)}`;
    let tile = tiles.get(key);
    if (!tile) tiles.set(key, (tile = []));
    // Round to ~0.01 deg (~1 km): plenty at radar scale, keeps tiles compact.
    tile.push([Math.round(lat * 100) / 100, Math.round(lon * 100) / 100, code, kind]);
    total++;
    kindCounts[kind] = (kindCounts[kind] ?? 0) + 1;
  }

  console.log(
    `selected ${total} airports (L ${kindCounts.L} / M ${kindCounts.M} / S ${kindCounts.S}) into ${tiles.size} tiles`,
  );

  if (args.dryRun || !args.env) {
    console.log("dry run: no KV writes");
    return;
  }

  const rows = [...tiles.entries()].map(([key, list]) => ({ key, value: JSON.stringify(list) }));
  const tmp = mkdtempSync(join(tmpdir(), "blip-apt-"));
  for (let i = 0; i < rows.length; i += BULK_CHUNK) {
    const chunk = rows.slice(i, i + BULK_CHUNK);
    const path = join(tmp, `bulk-${i / BULK_CHUNK}.json`);
    writeFileSync(path, JSON.stringify(chunk));
    console.log(`bulk put ${chunk.length} tiles (${i + chunk.length}/${rows.length}) ...`);
    execSync(
      ["npx", "wrangler", "kv", "bulk", "put", q(path), "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(
        " ",
      ),
      { stdio: "inherit" },
    );
  }
  console.log(`done: ${rows.length} apt:* tiles loaded to ${args.env}`);
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
