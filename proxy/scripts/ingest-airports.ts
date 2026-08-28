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
import { createHash, randomBytes } from "node:crypto";
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
  /** tiles | ap | both -- which key family to load. */
  only: "tiles" | "ap" | "both";
  /** Re-write these ap: shards even if their hash is unchanged (repair step). */
  forceShard?: string;
  /**
   * Write meta:airports and finish. RULE 1: the meta key is the CLAIM that a
   * build is good, so it is written last and by a separate invocation -- after
   * verify-airports has enumerated the namespace. A meta key written by the same
   * run that did the loading can only ever attest to what that run believed.
   */
  seal: boolean;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { dryRun: false, only: "both", seal: false };
  for (let i = 0; i < argv.length; i++) {
    const v = argv[i];
    if (v === "--env") a.env = argv[++i];
    else if (v === "--dry-run") a.dryRun = true;
    else if (v === "--file") a.file = argv[++i];
    else if (v === "--only") a.only = (argv[++i] as Args["only"]) ?? "both";
    else if (v === "--force-shard") a.forceShard = argv[++i];
    else if (v === "--seal") a.seal = true;
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


// ===========================================================================
// THE `ap:<CODE>` FAMILY -- one airport, by code, with elevation
// ===========================================================================
//
// A different question from the tiles above, so a different key family: the
// overlay answers "what is near me" (pre-tiled), this answers "where is LHR"
// (a key lookup). Follow's arc and globe need endpoint coordinates for codes
// the firmware's baked ~250 does not carry, and C5 needs the ELEVATION that no
// other source on the device has.
//
//   ap:<CODE> -> [lat, lon, elevFt|null]
//
// BOTH CODE FORMS ARE EMITTED, and that is deliberate. The route mirror carries
// IATA (DFW-BUR) and ICAO (EGYD-EGYD) alike -- RouteLabel.h's own examples --
// and the firmware's baked table is IATA only, so a four-letter code currently
// misses on purpose. Writing `ap:LHR` AND `ap:EGLL` for the same field closes
// that gap without the device needing to know which form it holds.
//
// ---------------------------------------------------------------------------
// A COLLISION DROPS THE CODE. IT DOES NOT PICK A WINNER.
//
// The two error directions are not symmetric, and this is the same asymmetry
// CLAUDE.md records for exclusion lists. A MISSING code costs an honest
// code-only arc -- the degradation the face is already built to render. A WRONG
// code draws an aircraft over the wrong continent, confidently, with nothing on
// screen suggesting anything is amiss.
//
// Last-write-wins would silently choose. So when two airports claim one code,
// neither gets it and the collision is counted and reported.
//
// ---------------------------------------------------------------------------
// ELEVATION IS SANITY-BANDED, AND THE FLOOR IS A REAL AIRFIELD
//
// [-1500, 18000] ft. The floor is not round: Bar Yehuda (Masada, Israel) sits
// at -1,266 ft and is an operating airfield, so any floor tight enough to look
// tidy rejects a real place. -1,500 is the same constant FollowState.h uses for
// the same reason. The ceiling clears Daocheng Yading at 14,472 ft with room.
//
// A row outside the band keeps its POSITION and loses its ELEVATION (null). The
// position is still useful to the arc; a wrong elevation is worse than none,
// because AGL = altitude - elevation and the error lands silently in a number
// the customer cannot check.
const AP_ELEV_MIN_FT = -1500;
const AP_ELEV_MAX_FT = 18000;
const AP_CODE_RE = /^[A-Z0-9]{3,4}$/;

// A code no real airport can hold, carrying a per-run nonce, probed through the
// LIVE path before and after the write. See rule 3 in the routes ingest: a
// read-back that goes straight to KV proves only that we can read our own
// write, and airport codes are the kind of thing an endpoint could plausibly
// answer from somewhere else.
const AP_SENTINEL = "ZZZZ";

interface ApRow {
  code: string;
  lat: number;
  lon: number;
  elev: number | null;
}

interface ApBuild {
  rows: Map<string, ApRow>;
  /** Same-namespace conflicts: unresolvable, so the code is dropped. */
  collisions: string[];
  /** Cross-namespace conflicts resolved in IATA's favour, with the loser named. */
  resolved: string[];
  rejected: { code: string; why: string }[];
  elevDropped: number;
}

/**
 * Build the ap: rows from the parsed CSV, applying every assertion.
 *
 * Exported shape rather than inline so the assertions can be reasoned about in
 * one place: each `continue` below is a row that will NOT be in KV, and the
 * counts are printed so a source change shows up as a moved number rather than
 * as silence.
 */
function buildApRows(
  lines: string[],
  cols: { type: number; lat: number; lon: number; iata: number; local: number; ident: number; elev: number },
): ApBuild {
  const rows = new Map<string, ApRow>();
  // code -> who claimed it, and FROM WHICH NAMESPACE. The namespace is what
  // makes the tie-break a fact rather than a preference; see the conflict block.
  const claimed = new Map<string, { ident: string; src: "iata" | "icao" }>();
  const collisions: string[] = [];
  const resolved: string[] = [];
  const rejected: { code: string; why: string }[] = [];
  let elevDropped = 0;

  for (let i = 1; i < lines.length; i++) {
    const line = lines[i]!;
    if (!line.trim()) continue;
    const f = parseCsvLine(line);
    if (!KINDS[f[cols.type] ?? ""]) continue; // fixed-wing fields only, as the tiles do

    const lat = parseFloat(f[cols.lat] ?? "");
    const lon = parseFloat(f[cols.lon] ?? "");
    const ident = (f[cols.ident] ?? "").trim().toUpperCase();

    // lat/lon in range, and finite. A NaN here would serialise as null and the
    // device would place the endpoint at the origin.
    if (!Number.isFinite(lat) || !Number.isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) {
      rejected.push({ code: ident, why: "position out of range" });
      continue;
    }
    // Null Island. 0,0 is a real coordinate and a very common data-entry
    // default, and an airport in the Gulf of Guinea is the shape that
    // mistake takes. Same reasoning as hasLocation on the device.
    if (lat === 0 && lon === 0) {
      rejected.push({ code: ident, why: "0,0 (unset, not a location)" });
      continue;
    }

    const rawElev = parseFloat(f[cols.elev] ?? "");
    let elev: number | null = null;
    if (Number.isFinite(rawElev)) {
      if (rawElev >= AP_ELEV_MIN_FT && rawElev <= AP_ELEV_MAX_FT) elev = Math.round(rawElev);
      else elevDropped++;
    }

    // IATA AND ICAO ONLY. local_code IS DELIBERATELY EXCLUDED, and the first
    // draft's collision count is why.
    //
    // Including it produced 2,931 collisions against 41,065 codes -- and almost
    // none were two airports genuinely sharing a code. local_code is a NATIONAL
    // namespace, unique only within a country, so a US local code routinely
    // equals an Argentine one and both lose. Worse, it knocked out real IATA
    // codes: `ANG (AGG vs ANG)` is an IATA code dropped because some other
    // field's local code collided with it.
    //
    // So the collision rule was right and its INPUT was wrong -- the rule is
    // what made that visible, by counting instead of picking a winner. IATA and
    // ICAO are globally unique by definition, which is what a global key space
    // needs, and they are also the two forms the route mirror actually emits.
    //
    // The provenance is carried alongside each form because the conflict rule
    // below needs to know which namespace a string came from.
    const iata = (f[cols.iata] ?? "").trim().toUpperCase();
    const forms: { code: string; src: "iata" | "icao" }[] = [];
    if (iata) forms.push({ code: iata, src: "iata" });
    if (ident && ident !== iata) forms.push({ code: ident, src: "icao" });

    for (const { code, src } of forms) {
      if (!AP_CODE_RE.test(code)) continue; // silently: most rows have blanks here
      const prior = claimed.get(code);
      if (prior && prior.ident !== ident) {
        // ---------------------------------------------------------------
        // TWO FIELDS WANT ONE CODE. IATA WINS -- AND THAT IS A FACT ABOUT
        // THE QUERY NAMESPACE, NOT A COIN FLIP.
        //
        // These keys are read by one caller asking one kind of question: the
        // firmware looking up a route endpoint. The codes in that traffic come
        // from the route mirror, where a three-letter string means IATA. So
        // when a string is one airport's IATA code and another's ICAO ident,
        // the IATA holder is the one our queries are actually about.
        //
        // The concrete case that settles it: GIG (Rio Galeao) is in the
        // firmware's baked majors table. Dropping it would make a code the
        // device resolves TODAY stop resolving -- a live regression, traded for
        // tidiness.
        //
        // THE LOSER IS NAMED IN THE OUTPUT. A resolved conflict is still a
        // dropped record, and a drop nobody can see is the failure mode this
        // whole rule exists to avoid.
        // ---------------------------------------------------------------
        if (src === "iata" && prior.src === "icao") {
          resolved.push(`${code}: IATA ${ident} wins, ICAO ident ${prior.ident} dropped`);
          claimed.set(code, { ident, src });
          rows.set(code, {
            code,
            lat: Math.round(lat * 1000) / 1000,
            lon: Math.round(lon * 1000) / 1000,
            elev,
          });
          continue;
        }
        if (src === "icao" && prior.src === "iata") {
          resolved.push(`${code}: IATA ${prior.ident} wins, ICAO ident ${ident} dropped`);
          continue; // the incumbent keeps it
        }
        // SAME NAMESPACE on both sides: two IATA codes or two ICAO idents that
        // are equal. That is a source defect, not an ambiguity we can reason
        // about, so neither gets the key.
        rows.delete(code);
        collisions.push(`${code} (${prior.src} ${prior.ident} vs ${src} ${ident})`);
        continue;
      }
      claimed.set(code, { ident, src });
      rows.set(code, {
        code,
        // ~0.001 deg (~110 m). Finer than the tiles because these are ROUTE
        // ENDPOINTS on a globe, not markers on a radar ring.
        lat: Math.round(lat * 1000) / 1000,
        lon: Math.round(lon * 1000) / 1000,
        elev,
      });
    }
  }
  return { rows, collisions, resolved, rejected, elevDropped };
}

/** Stable hash of a shard's contents, for the diff. */
function shardHash(codes: string[], rows: Map<string, ApRow>): string {
  const h = createHash("sha256");
  for (const c of codes.sort()) {
    const r = rows.get(c)!;
    h.update(`${c}|${r.lat}|${r.lon}|${r.elev ?? ""}\n`);
  }
  return h.digest("hex").slice(0, 16);
}


/**
 * Load the ap: family, following the four rules the routes ingest established.
 *
 *   1. meta:airports is written LAST, after every data key AND the sentinel.
 *   2. The count is of what we WROTE, not what we parsed.
 *   3. Read back through the LIVE Worker path, with a sentinel that carries a
 *      per-run nonce so a stale one cannot pass.
 *   4. Diff by shard, do not blind-upsert 34k keys on every run.
 */
async function loadApFamily(
  lines: string[],
  cols: { type: number; lat: number; lon: number; iata: number; local: number; ident: number; elev: number },
  args: Args,
): Promise<void> {
  const built = buildApRows(lines, cols);
  const { rows } = built;

  console.log("");
  console.log(`ap: built ${rows.size} codes from ${lines.length - 1} source rows`);
  console.log(`    elevation dropped (outside ${AP_ELEV_MIN_FT}..${AP_ELEV_MAX_FT} ft): ${built.elevDropped}`);
  console.log(`    rows rejected outright: ${built.rejected.length}`);
  for (const r of built.rejected.slice(0, 5)) console.log(`      - ${r.code || "(no ident)"}: ${r.why}`);
  if (built.rejected.length > 5) console.log(`      ... and ${built.rejected.length - 5} more`);
  console.log(`    conflicts resolved to IATA (loser named): ${built.resolved.length}`);
  for (const c of built.resolved) console.log(`      - ${c}`);
  console.log(`    CODE COLLISIONS (same namespace -- dropped, not resolved): ${built.collisions.length}`);
  for (const c of built.collisions.slice(0, 10)) console.log(`      - ${c}`);
  if (built.collisions.length > 10) console.log(`      ... and ${built.collisions.length - 10} more`);

  // The sentinel must not be a code the source can produce, or the control it
  // provides is meaningless. Assert rather than assume.
  if (rows.has(AP_SENTINEL)) {
    throw new Error(
      `the sentinel code ${AP_SENTINEL} exists in the source -- pick another, ` +
        `or the read-back control proves nothing`,
    );
  }

  // A few named fields that must be present and right, checked against
  // independently known values. These are a LIVENESS check, not the control:
  // the sentinel is the control, because these could in principle be served
  // from somewhere that is not our write.
  const canaries: [string, number, number][] = [
    ["LHR", 51.47, -0.46],
    ["EGLL", 51.47, -0.46], // the same field by its ICAO code -- the gap this closes
    ["DEN", 39.86, -104.67],
    ["DEL", 28.57, 77.1],
  ];
  let canaryFails = 0;
  for (const [code, lat, lon] of canaries) {
    const r = rows.get(code);
    if (!r) {
      console.log(`    CANARY MISSING: ${code}`);
      canaryFails++;
      continue;
    }
    if (Math.abs(r.lat - lat) > 0.05 || Math.abs(r.lon - lon) > 0.05) {
      console.log(`    CANARY MOVED: ${code} is ${r.lat},${r.lon}, expected ~${lat},${lon}`);
      canaryFails++;
    }
  }
  console.log(`    canaries: ${canaries.length - canaryFails}/${canaries.length}`);

  // Shard by first character: ~36 shards over ~34k codes. A dropped bulk chunk
  // is CONSECUTIVE keys in sorted order, so it lands inside a shard or two --
  // which is what makes a per-shard report able to show a concentrated failure
  // that a global percentage would bury.
  const shards = new Map<string, string[]>();
  for (const code of rows.keys()) {
    const k = code[0]!;
    let list = shards.get(k);
    if (!list) shards.set(k, (list = []));
    list.push(code);
  }
  const hashes: Record<string, string> = {};
  for (const [k, codes] of shards) hashes[k] = shardHash(codes, rows);

  if (args.dryRun || !args.env) {
    console.log(`ap: ${shards.size} shards; dry run, no KV writes`);
    console.log(`ap: would write ${rows.size} keys + 1 sentinel + meta:airports`);
    return;
  }

  // RULE 4 -- diff. Read the previous shard hashes out of the existing meta.
  let prior: Record<string, string> = {};
  try {
    const raw = execSync(
      ["npx", "wrangler", "kv", "key", "get", q("meta:airports"),
       "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] },
    );
    prior = (JSON.parse(raw) as { shards?: Record<string, string> }).shards ?? {};
    console.log(`ap: previous build has ${Object.keys(prior).length} shards`);
  } catch {
    console.log("ap: no previous meta (or unreadable) -- writing every shard");
  }

  const changed = [...shards.keys()].filter((k) => prior[k] !== hashes[k]);
  const forced = args.forceShard ? args.forceShard.split(",").map((x) => x.trim().toUpperCase()) : [];
  for (const f of forced) if (!changed.includes(f) && shards.has(f)) changed.push(f);
  console.log(`ap: ${changed.length}/${shards.size} shards changed${forced.length ? ` (+${forced.length} forced)` : ""}`);

  // RULE 2 -- count what we WRITE.
  let written = 0;
  const tmp = mkdtempSync(join(tmpdir(), "blip-ap-"));
  for (const k of changed) {
    const codes = shards.get(k)!;
    const pairs = codes.map((c) => {
      const r = rows.get(c)!;
      return { key: `ap:${c}`, value: JSON.stringify([r.lat, r.lon, r.elev]) };
    });
    for (let i = 0; i < pairs.length; i += BULK_CHUNK) {
      const chunk = pairs.slice(i, i + BULK_CHUNK);
      const path = join(tmp, `ap-${k}-${i}.json`);
      writeFileSync(path, JSON.stringify(chunk));
      execSync(
        ["npx", "wrangler", "kv", "bulk", "put", q(path),
         "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
        { stdio: "inherit" },
      );
      written += chunk.length;
    }
    console.log(`ap: shard ${k} -> ${codes.length} keys`);
  }

  // RULE 3 -- the sentinel, with a nonce this run and no other could produce.
  const nonce = `${Date.now().toString(36)}-${randomBytes(4).toString("hex")}`;
  const sentinelPath = join(tmp, "ap-sentinel.json");
  writeFileSync(
    sentinelPath,
    JSON.stringify([{ key: `ap:${AP_SENTINEL}`, value: JSON.stringify([1.5, 2.5, 42, nonce]) }]),
  );
  execSync(
    ["npx", "wrangler", "kv", "bulk", "put", q(sentinelPath),
     "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
    { stdio: "inherit" },
  );
  console.log(`ap: sentinel written (nonce ${nonce})`);

  console.log("");
  console.log(`ap: ${written} keys written across ${changed.length} shard(s).`);
  console.log("ap: meta:airports NOT written. Next:");
  console.log(`      npm run verify:airports -- --env ${args.env}`);
  console.log(`      npx tsx scripts/ingest-airports.ts --env ${args.env} --only ap --seal`);
  console.log("    Rule 1: the meta key is the CLAIM that the build is good, so it is");
  console.log("    written last and by a separate run -- after something other than the");
  console.log("    loader has enumerated the namespace.");

  if (canaryFails > 0) {
    throw new Error(`${canaryFails} canary failure(s) -- meta not written, previous build still current`);
  }
}

/**
 * Write meta:airports. Separate invocation, after verify-airports passes.
 *
 * The shard hashes go in because the next run's diff reads them back -- so the
 * meta key is both the health claim and the diff's memory, and refusing to write
 * it on a failed build correctly forces the next run to rewrite everything.
 */
async function sealApFamily(
  lines: string[],
  cols: { type: number; lat: number; lon: number; iata: number; local: number; ident: number; elev: number },
  args: Args,
): Promise<void> {
  if (!args.env) throw new Error("--seal needs --env");
  const built = buildApRows(lines, cols);
  const shards = new Map<string, string[]>();
  for (const code of built.rows.keys()) {
    const k = code[0]!;
    let list = shards.get(k);
    if (!list) shards.set(k, (list = []));
    list.push(code);
  }
  const hashes: Record<string, string> = {};
  for (const [k, codes] of shards) hashes[k] = shardHash(codes, built.rows);

  const meta = {
    rows: built.rows.size,
    shards: hashes,
    built: new Date().toISOString(),
    source: CSV_URL,
    collisionsDropped: built.collisions.length,
    conflictsResolvedToIata: built.resolved.length,
    elevationDropped: built.elevDropped,
  };
  const tmp = mkdtempSync(join(tmpdir(), "blip-apmeta-"));
  const path = join(tmp, "meta.json");
  writeFileSync(path, JSON.stringify([{ key: "meta:airports", value: JSON.stringify(meta) }]));
  execSync(
    ["npx", "wrangler", "kv", "bulk", "put", q(path),
     "--binding=ENRICH_KV", `--env=${args.env}`, "--remote"].join(" "),
    { stdio: "inherit" },
  );
  console.log(`ap: meta:airports sealed -- ${meta.rows} rows, ${Object.keys(hashes).length} shards`);
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
  const cElev = col("elevation_ft");

  const doTiles = args.only === "both" || args.only === "tiles";
  const doAp = args.only === "both" || args.only === "ap";

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

  if (doAp && args.seal) {
    await sealApFamily(lines, {
      type: cType, lat: cLat, lon: cLon,
      iata: cIata, local: cLocal, ident: cIdent, elev: cElev,
    }, args);
    return;
  }

  if (doAp) {
    await loadApFamily(lines, {
      type: cType, lat: cLat, lon: cLon,
      iata: cIata, local: cLocal, ident: cIdent, elev: cElev,
    }, args);
  }

  if (!doTiles) return;

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
