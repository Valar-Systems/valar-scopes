/**
 * nophoto-share.ts -- how often an enrich lookup leaves the card with no photo, as ONE aggregate (#670).
 *
 *   npx tsx scripts/nophoto-share.ts            (from proxy/; production's dataset; the last 7 days)
 *   (token: the analytics-read line of the file $BLIPSCOPE_TOKEN_FILE names, see token-file.ts)
 *
 * Approved by the reviewer 2026-11-12 17:33Z as counts only: two totals and their ratio, no ids, and no per-device or
 * per-aircraft rows. The queries below are frozen in docs/card-nophoto-fix.md before the first run. Neither selects a
 * blob that holds a hex, a type or a device id; each returns one row with one number.
 *
 * WHAT COUNTS AS "NO PHOTO". recordEnrichGap (metrics.ts) writes the ROOT gap only, one point per lookup:
 *   "type"  -- no type at all, so no stock photo (photos are keyed by type);
 *   "photo" -- a type and a name, and no photo in the library.
 * Both leave the device's card without a photo, so both count. "name" (a type, no friendly name) says nothing about the
 * photo and is left out, which can only make the share an UNDERCOUNT. A per-hex photo override on a typeless airframe
 * would be an overcount; those are few.
 *
 * THE DENOMINATOR is every /enrich request point (both route templates), weighted. record() keeps cache HITs 1:10 and
 * carries the weight in double4. Analytics Engine's own sampling is _sample_interval, on both sides.
 */
import { readTokenFile } from "./token-file";

const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"; // same as wrangler.toml account_id (usage-stats.ts)
const DATASET = "blipscope_proxy";                   // production's (wrangler.toml [env.production])
const WINDOW = "INTERVAL '7' DAY";

export const NO_PHOTO_SQL =
  `SELECT SUM(_sample_interval) AS n FROM ${DATASET} ` +
  `WHERE index1 = 'enrich_gap' AND (blob2 = 'type' OR blob2 = 'photo') AND timestamp > NOW() - ${WINDOW}`;
export const ENRICH_SQL =
  `SELECT SUM(_sample_interval * double4) AS n FROM ${DATASET} ` +
  `WHERE (index1 = '/api/v1/blipscope/enrich' OR index1 = '/v1/enrich') AND timestamp > NOW() - ${WINDOW}`;

async function one(sql: string, token: string): Promise<number> {
  const res = await fetch(`https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}/analytics_engine/sql`,
                          { method: "POST", headers: { Authorization: `Bearer ${token}` }, body: sql });
  // The whole body on failure: a trimmed error is how an auth failure reads as an empty dataset.
  if (!res.ok) throw new Error(`Analytics Engine returned ${res.status}: ${await res.text()}`);
  const body = (await res.json()) as { data?: Array<{ n?: number | string | null }> };
  const rows = body.data ?? [];
  if (rows.length !== 1) throw new Error(`expected exactly one row, got ${rows.length}`);
  const n = Number(rows[0]!.n ?? NaN);
  if (!Number.isFinite(n) || n < 0) throw new Error(`not a count: ${JSON.stringify(rows[0])}`);
  return n;
}

async function main(): Promise<void> {
  let noPhoto: number, all: number;
  try {
    const token = readTokenFile("analytics-read");
    noPhoto = await one(NO_PHOTO_SQL, token);
    all = await one(ENRICH_SQL, token);
  } catch (e) {
    console.log(`INSTRUMENT: ${e instanceof Error ? e.message : String(e)}`);
    process.exit(2);
  }
  console.log(`no-photo share, ${DATASET}, the 7 days to ${new Date().toISOString().slice(0, 16)}Z`);
  console.log(`  enrich lookups with no photo (root gap type or photo): ${Math.round(noPhoto)}`);
  console.log(`  enrich lookups, all:                                   ${Math.round(all)}`);
  // A zero on either side, or a numerator above the denominator, is a query that cannot see what it counts. It is
  // never reported as a share.
  if (noPhoto === 0 || all === 0 || noPhoto > all) {
    console.log("BLIND: a zero total, or a numerator above the denominator -- no share is stated");
    process.exit(2);
  }
  console.log(`  share: ${((100 * noPhoto) / all).toFixed(1)} %`);
}

void main();
