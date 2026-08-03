import type { Env } from "./types";
import { SCHEMA_V } from "./schema";
import { TYPE_NAMES } from "./typenames";
import { leaderboardHtml } from "./pages.generated";
import { errorResponse, jsonResponse } from "./util";

// The public spotting leaderboard.
//
// SCORING IS ACTIVE, NOT PASSIVE (v2, 2026-08-03). It used to derive entirely
// from the device's Logbook tallies -- every aircraft that flew past an antenna
// scored, with no human involved. That ranked AIRSPACE: whoever lived under an
// approach path with a good antenna won, and none of that is something the owner
// chose or could change. A scope in rural Oregon could not compete no matter how
// closely its owner watched it.
//
// Now the only thing that scores is a CLAIM: the owner opened that aircraft's
// detail card on the device. Reception still fills the logbook (and the device
// still reports both numbers, so "47 of 153 types claimed" is visible), but the
// board measures the spotter. Per-type-once, so it rewards collection breadth
// rather than volume -- a busy sky gives you more chances, not more points.
//
// Storage (KV; D1 only if/when the fleet outgrows a per-device-row scan):
//   lb:dev:<id>       one device's row (counts, per-type claim month, ...)
//   lb:name:<lower>   display-name claim -> device id (claim-on-first-submit)
//   lb:firsttype:<T>  the first device ever to CLAIM ICAO type T ("First!" badge)
//   lb:board          the cached, rendered board (rebuilt lazily when stale)
//
// The board (rarity multipliers, ranks, per-category leaders) is aggregated
// across all device rows. At bench-fleet scale that's a cheap KV list; the
// scale path is a cron-built board + D1, noted in the README.

// ---- scoring weights (server-side only; tune without a firmware change) ------

const PTS_TYPE = 10; // per CLAIMED type, times its rarity multiplier
const PTS_AIRLINE = 5;
const PTS_COUNTRY = 25; // genuinely hard to grow
const PTS_AIRPORT = 2;
// raw contacts deliberately score 0: pure uptime/density is not skill. As of v2
// the same reasoning retires passive airline/country/airport counts from scoring
// -- they were the back door through which a busy sky still out-scored an
// attentive owner. The device claims them as riders on a tap (one tap credits
// the aircraft's type, airline, country and route airports together), so these
// weights now apply to CLAIMED tallies and every point on the board traces to a
// deliberate action.

// Rarity multiplier for a type, by the fraction of opted-in devices that have
// logged it. The equalizer: a warbird or an odd-routed heavy is worth real
// points anywhere, so a dense-airspace device can't just out-volume everyone.
// Exported for direct threshold testing.
export function rarityMultiplier(seenFraction: number): number {
  if (seenFraction < 0.05) return 5;
  if (seenFraction < 0.25) return 2;
  return 1;
}

// Badge type-code sets (small, curated -- extend freely; server-side only).
const WIDEBODY_TYPES = new Set([
  "B742", "B744", "B748", "B752", "B762", "B763", "B764", "B772", "B77W", "B77L",
  "B788", "B789", "B78X", "A306", "A310", "A332", "A333", "A338", "A339", "A342",
  "A343", "A345", "A346", "A359", "A35K", "A388",
]);
const MIL_TYPES = new Set([
  "C5", "C5M", "C17", "C30J", "C130", "K35R", "K46", "E3TF", "E6", "E8", "P8", "H60", "H64",
  "H47", "H53S", "V22", "F16", "F15", "F35", "F22", "F18S", "F18H", "A10", "B52",
  "B1", "B2", "U2", "TEX2", "T38", "EUFI",
]);

// ---- shapes ------------------------------------------------------------------

interface Counts {
  types: number;
  airlines: number;
  countries: number;
  airports: number;
}

interface DeviceRow {
  id: string;
  name: string;
  model: string;
  verified: boolean; // cloud-fed device (v1: every submitter is, they POST via the proxy)
  radiusKm: number; // the scoring radius the tallies came from (fairness context)
  counts: Counts; // SEEN -- context only, scores nothing
  claimed: Counts; // CLAIMED -- what actually scores
  types: Record<string, string>; // ICAO type -> claim month "YYYY-MM"
  seasonMonth: string; // the month `monthStart` counts belong to
  monthStartCounts: Counts; // snapshot at the month roll, for counts-only season deltas
  streakDays: number;
  streakLastDay: string; // "YYYY-MM-DD" of the last day a NEW claim landed
  createdAt: number;
  updatedAt: number;
  legacy: boolean; // submitted by pre-claim firmware: recorded, never ranked
}

interface Submission {
  id: string;
  name: string;
  radiusKm: number;
  counts: Counts;
  claimed: Counts;
  claimedTypes: string[];
  // A submission with NO claimedTypes field came from firmware that predates
  // tap-to-claim. Distinguished from "claimed nothing yet" on purpose: the old
  // firmware's type list means SEEN, and scoring it would hand every un-updated
  // device the exact antenna advantage this rework removes.
  legacy: boolean;
}

// ---- submit ------------------------------------------------------------------

const NAME_MAX = 24;
const TYPE_CODES_MAX = 400;
const TYPE_RE = /^[A-Z0-9]{2,4}$/;
// A real sky can't add more than ~40 new types or ~10 new countries in a day;
// larger jumps between submissions are clamped (a spoof can't rocket the board).
const MAX_TYPE_GROWTH_PER_DAY = 40;
const MAX_COUNTRY_GROWTH_PER_DAY = 10;

function monthOf(nowMs: number): string {
  return new Date(nowMs).toISOString().slice(0, 7); // "YYYY-MM"
}
function dayOf(nowMs: number): string {
  return new Date(nowMs).toISOString().slice(0, 10); // "YYYY-MM-DD"
}

function sanitizeName(raw: unknown): string {
  const s = (typeof raw === "string" ? raw : "").trim().slice(0, NAME_MAX);
  // Keep it printable-ASCII-ish; the public page renders it, so drop control
  // and angle-bracket chars defensively (the renderer escapes too).
  return s.replace(/[\x00-\x1f<>]/g, "").trim();
}

function parseSubmission(raw: unknown): Submission | null {
  if (raw === null || typeof raw !== "object") return null;
  const b = raw as Record<string, unknown>;
  const id = typeof b.id === "string" ? b.id.trim() : "";
  if (!/^[0-9a-f]{8,32}$/.test(id)) return null;
  const count = (v: unknown): number => {
    const n = typeof v === "number" && Number.isFinite(v) ? Math.floor(v) : 0;
    return n < 0 ? 0 : Math.min(n, 100000);
  };
  const counts = (o: unknown): Counts => {
    const c = (o ?? {}) as Record<string, unknown>;
    return {
      types: count(c.types),
      airlines: count(c.airlines),
      countries: count(c.countries),
      airports: count(c.airports),
    };
  };
  // Presence, not emptiness, is what separates new firmware from old: a v4
  // device that has claimed nothing sends `claimedTypes: []`, which is a real
  // (zero) score. A v3 device sends no such field at all.
  const legacy = !Array.isArray(b.claimedTypes);
  const claimedTypes = (Array.isArray(b.claimedTypes) ? b.claimedTypes : [])
    .filter((t): t is string => typeof t === "string")
    .map((t) => t.trim().toUpperCase())
    .filter((t) => TYPE_RE.test(t))
    .slice(0, TYPE_CODES_MAX);
  const radiusKm = typeof b.radiusKm === "number" && Number.isFinite(b.radiusKm) ? b.radiusKm : 0;
  return {
    id,
    name: sanitizeName(b.name),
    radiusKm: Math.max(0, Math.min(radiusKm, 1000)),
    counts: counts(b.counts),
    claimed: counts(b.claimed),
    claimedTypes,
    legacy,
  };
}

// Claim a display name for a device: first come first served, later collisions
// get a numeric suffix. Returns the resolved (possibly suffixed) name.
async function claimName(env: Env, id: string, wanted: string): Promise<string> {
  if (!wanted) return id.slice(0, 8); // no name given: fall back to a short id
  for (let n = 0; n < 50; n++) {
    const candidate = n === 0 ? wanted : `${wanted} ${n + 1}`;
    const key = `lb:name:${candidate.toLowerCase()}`;
    const owner = await env.ENRICH_KV.get(key);
    if (owner === null) {
      await env.ENRICH_KV.put(key, id);
      return candidate;
    }
    if (owner === id) return candidate; // already ours (re-submit / rename to same)
  }
  return id.slice(0, 8);
}

export async function handleLeaderboardSubmit(request: Request, env: Env): Promise<Response> {
  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return errorResponse(400, "bad_json");
  }
  const sub = parseSubmission(body);
  if (!sub) return errorResponse(400, "bad_submission");

  const now = Date.now();
  const nowMonth = monthOf(now);
  const nowDay = dayOf(now);
  const model = (request.headers.get("X-Blip-Model") ?? "").trim().slice(0, 16);

  const prev = await env.ENRICH_KV.get<DeviceRow>(`lb:dev:${sub.id}`, "json");
  const name = await claimName(env, sub.id, sub.name);

  // Merge type CLAIM months: a type already claimed keeps its month; a newly
  // claimed one is stamped this month (that's what a season race is made of).
  // Cap the number of new claims accepted per day so a spoofed jump can't rocket.
  // A legacy submission contributes nothing here -- its type list means "seen".
  const types: Record<string, string> = { ...(prev?.types ?? {}) };
  let newTypes = 0;
  for (const t of sub.claimedTypes) {
    if (types[t] === undefined) {
      if (newTypes >= MAX_TYPE_GROWTH_PER_DAY && prev) continue; // clamp (first submit is exempt)
      types[t] = nowMonth;
      newTypes++;
    }
  }

  // Counts are monotonic (they can only grow) and rate-limited for countries.
  const zero: Counts = { types: 0, airlines: 0, countries: 0, airports: 0 };
  const prevCounts = prev?.claimed ?? zero;
  const mono = (cur: number, was: number, cap = Infinity): number =>
    Math.max(was, Math.min(cur, was + cap));
  // What scores: the CLAIMED tallies. `types` is authoritative from the (capped)
  // claim map rather than the device's own number, so a device cannot claim a
  // total it has not enumerated.
  const claimedCounts: Counts = {
    types: Object.keys(types).length,
    airlines: mono(sub.claimed.airlines, prevCounts.airlines),
    countries: mono(sub.claimed.countries, prevCounts.countries, prev ? MAX_COUNTRY_GROWTH_PER_DAY : Infinity),
    airports: mono(sub.claimed.airports, prevCounts.airports),
  };
  // What is kept for context only. Seen counts are shown on the profile page as
  // the denominator ("47 of 153") and score nothing, so they are not rate-limited
  // -- there is nothing to gain by inflating them.
  const prevSeen = prev?.counts ?? zero;
  const seenCounts: Counts = {
    types: Math.max(prevSeen.types, sub.counts.types),
    airlines: Math.max(prevSeen.airlines, sub.counts.airlines),
    countries: Math.max(prevSeen.countries, sub.counts.countries),
    airports: Math.max(prevSeen.airports, sub.counts.airports),
  };

  // Month roll: snapshot the counts-only categories so season deltas are
  // measured from the start of the current month.
  let seasonMonth = prev?.seasonMonth ?? nowMonth;
  let monthStartCounts = prev?.monthStartCounts ?? { ...claimedCounts };
  if (seasonMonth !== nowMonth) {
    seasonMonth = nowMonth;
    monthStartCounts = { ...prevCounts };
  }

  // Streak: a day on which at least one new CLAIM landed extends it. Under v1
  // this counted days the antenna caught something new, which a device did on
  // its own while nobody was home; now it counts days somebody actually looked.
  let streakDays = prev?.streakDays ?? 0;
  let streakLastDay = prev?.streakLastDay ?? "";
  const addedSomething = newTypes > 0 || claimedCounts.airlines > prevCounts.airlines ||
    claimedCounts.countries > prevCounts.countries || claimedCounts.airports > prevCounts.airports;
  if (addedSomething && streakLastDay !== nowDay) {
    const yesterday = dayOf(now - 86400000);
    streakDays = streakLastDay === yesterday ? streakDays + 1 : 1;
    streakLastDay = nowDay;
  }

  // "First!" badge: first device ever to CLAIM a type, not merely to receive one.
  for (const t of sub.claimedTypes) {
    if (prev && types[t] !== nowMonth) continue; // only newly-claimed types are worth checking
    const owner = await env.ENRICH_KV.get(`lb:firsttype:${t}`);
    if (owner === null) await env.ENRICH_KV.put(`lb:firsttype:${t}`, sub.id);
  }

  const row: DeviceRow = {
    id: sub.id,
    name,
    model,
    verified: true, // v1: everyone submitting reaches us through the proxy
    radiusKm: sub.radiusKm,
    counts: seenCounts,
    claimed: claimedCounts,
    types,
    seasonMonth,
    monthStartCounts,
    streakDays,
    streakLastDay,
    createdAt: prev?.createdAt ?? now,
    updatedAt: now,
    legacy: sub.legacy,
  };
  await env.ENRICH_KV.put(`lb:dev:${sub.id}`, JSON.stringify(row));

  // Respond with this device's standing so the scope can show a rank block.
  // Force a rebuild (not the cached read): the response must reflect the row we
  // just stored, and ranking the fleet is inherent to computing this device's
  // rank anyway. Public page reads still hit the 5-min cache this refreshes.
  const board = await buildBoard(env);
  const me = board.rows.find((r) => r.id === sub.id);
  return jsonResponse({
    v: SCHEMA_V,
    ok: true,
    name,
    // A legacy device is absent from the board, so every figure here is 0 and
    // its Stats block simply shows no standing -- the same as a device that has
    // opted in but not yet been ranked. `legacy` says why, so an owner who asks
    // can be told "update the firmware" rather than "it's broken".
    legacy: sub.legacy,
    rank: me?.rank ?? 0,
    points: me?.points ?? 0,
    total: board.rows.length,
    seasonRank: me?.seasonRank ?? 0,
    seasonPoints: me?.seasonPoints ?? 0,
    rarestType: me?.rarestType ?? "",
    rarestPct: me?.rarestPct ?? 0,
  });
}

// ---- board aggregation -------------------------------------------------------

interface ScoredRow {
  id: string;
  name: string;
  verified: boolean;
  points: number;
  seasonPoints: number;
  rank: number;
  seasonRank: number;
  counts: Counts; // claimed
  seen: Counts;   // context
  badges: string[];
  rarestType: string; // the device's least-common logged type (fleet-wide), "" if none
  rarestPct: number;  // percent of opted-in devices that have also logged it
  radiusKm: number;   // the scoring radius the tallies came from (fairness transparency)
}

// The standard scoring radius (ROADMAP "Radius fairness"): 30 miles. Full
// background normalization to this radius is DEFERRED on the device side -- it
// widens the tracked-aircraft set, which is the render/heap path under
// investigation for the overnight slowdown, so it must not land mid-bug-hunt.
// Until then the board is transparent about each device's play radius, and the
// rarity multiplier already does most of the radius equalizing.
const STANDARD_RADIUS_KM = 48;

// One category leader, with the number that earned it. The old shape was a bare
// array of five names -- "Most types: Redmond Radar" with nothing to say how
// many, which is a claim the page cannot substantiate.
interface Leader {
  name: string;
  count: number;
}
type Leaders = { types: Leader; airlines: Leader; countries: Leader; airports: Leader };

interface Board {
  builtAt: number;
  season: string;
  rows: ScoredRow[];
  leaders: Leaders;       // lifetime: biggest claimed tally per category
  seasonLeaders: Leaders; // this month: biggest GROWTH per category
}

// Lazy rebuild cadence. 60s, not the original 5 min: the JSON is served with
// max-age=60, and a board rebuilt every 5 min behind a 60s cache is false
// precision -- the header implies a freshness the data does not have. A rebuild
// is a small KV list at bench-fleet scale, so matching them costs little.
// Revisit at the D1 point, when a per-device-row scan stops being cheap; that is
// the same threshold that forces the cron-built board.
const BOARD_STALE_MS = 60 * 1000;

// Every badge now reads off CLAIMS. `row.types` is the claim map, so the
// widebody/warbird tests changed meaning without changing shape: you earn them
// by opening those aircraft, not by having them fly over.
function computeBadges(row: DeviceRow, firstTypeCount: number): string[] {
  const badges: string[] = [];
  const typeCodes = Object.keys(row.types);
  const cl = row.claimed ?? { types: 0, airlines: 0, countries: 0, airports: 0 };
  if (cl.types >= 100) badges.push("century");
  if (typeCodes.some((t) => WIDEBODY_TYPES.has(t))) badges.push("widebody");
  if (typeCodes.filter((t) => MIL_TYPES.has(t)).length >= 10) badges.push("warbird");
  if (cl.countries >= 15) badges.push("globetrotter");
  if (row.streakDays >= 30) badges.push("streak30");
  if (firstTypeCount > 0) badges.push("first");
  return badges;
}

// A row is rankable only if the DEVICE said so (`legacy` is false -- the
// submission carried a claimedTypes array) AND the STORED ROW itself is v2.
//
// The second half is not redundant, and its absence was a live bug. A row
// written by the v1 Worker has no `claimed` object at all, and its `types` map
// means SEEN, not claimed. `!r.legacy` on such a row is `!undefined` === true,
// so it ranked -- which did two wrong things at once. It resurrected exactly the
// antenna advantage v2 exists to remove (every type the aerial ever heard,
// scored as though someone had tapped it). And it published a self-contradicting
// profile: `points` and `rarestType` computed from `types`, sitting beside
// all-zero counters read from the `claimed` that isn't there. That is how a real
// device rendered "Spotter Score 120, rarest catch Boeing 737 MAX 9, types
// claimed 0 of 12".
//
// Score and counters have to come from one source or they can disagree, and a
// page disagreeing with itself is worse than a page saying nothing. An excluded
// device reappears the moment its firmware submits once.
function isRankable(r: DeviceRow): boolean {
  return r.legacy !== true && r.claimed !== undefined && r.claimed !== null;
}

async function buildBoard(env: Env): Promise<Board> {
  const now = Date.now();
  const season = monthOf(now);

  // Enumerate device rows (bench scale: a small KV list).
  const all: DeviceRow[] = [];
  let cursor: string | undefined;
  do {
    const page = await env.ENRICH_KV.list({ prefix: "lb:dev:", cursor, limit: 1000 });
    const got = await Promise.all(page.keys.map((k) => env.ENRICH_KV.get<DeviceRow>(k.name, "json")));
    for (const r of got) if (r) all.push(r);
    cursor = page.list_complete ? undefined : page.cursor;
  } while (cursor);

  // Legacy rows are stored but never ranked, and they must not skew rarity
  // either: their `types` map is empty under v2, so counting them as devices
  // would inflate every claimed type's apparent rarity.
  const rows = all.filter(isRankable);

  // Rarity: fraction of devices that have CLAIMED each type. Deliberately not
  // "seen" -- a type everybody receives but nobody bothers to open is genuinely
  // rare as a catch, and the multiplier is what lets a quiet sky compete.
  const deviceCount = Math.max(1, rows.length);
  const typeFreq = new Map<string, number>();
  for (const r of rows) for (const t of Object.keys(r.types)) typeFreq.set(t, (typeFreq.get(t) ?? 0) + 1);
  const rarity = (t: string): number => rarityMultiplier((typeFreq.get(t) ?? 0) / deviceCount);

  // How many "First!" types each device owns (one pass over the claims).
  const firstOwners = new Map<string, number>();
  let fc: string | undefined;
  do {
    const page = await env.ENRICH_KV.list({ prefix: "lb:firsttype:", cursor: fc, limit: 1000 });
    const vals = await Promise.all(page.keys.map((k) => env.ENRICH_KV.get(k.name)));
    for (const v of vals) if (v) firstOwners.set(v, (firstOwners.get(v) ?? 0) + 1);
    fc = page.list_complete ? undefined : page.cursor;
  } while (fc);

  const allScored = rows.map((r) => {
    let typePts = 0;
    let seasonTypePts = 0;
    // Track the device's rarest logged type (lowest fleet-wide frequency) as we go.
    let rarestType = "";
    let rarestFreq = Infinity;
    for (const [t, m] of Object.entries(r.types)) {
      const p = PTS_TYPE * rarity(t);
      typePts += p;
      if (m === season) seasonTypePts += p;
      const f = typeFreq.get(t) ?? 0;
      if (f < rarestFreq) { rarestFreq = f; rarestType = t; }
    }
    const cl = r.claimed ?? { types: 0, airlines: 0, countries: 0, airports: 0 };
    const points =
      typePts +
      cl.airlines * PTS_AIRLINE +
      cl.countries * PTS_COUNTRY +
      cl.airports * PTS_AIRPORT;
    // Season score for counts-only categories = growth since the month start.
    const ms = r.seasonMonth === season ? r.monthStartCounts : cl;
    const seasonPoints =
      seasonTypePts +
      Math.max(0, cl.airlines - ms.airlines) * PTS_AIRLINE +
      Math.max(0, cl.countries - ms.countries) * PTS_COUNTRY +
      Math.max(0, cl.airports - ms.airports) * PTS_AIRPORT;
    return {
      id: r.id,
      name: r.name,
      verified: r.verified,
      counts: cl,        // the scoring tallies -- what the board columns mean
      seen: r.counts,    // the denominator, shown as context on the profile
      points: Math.round(points),
      seasonPoints: Math.round(seasonPoints),
      badges: computeBadges(r, firstOwners.get(r.id) ?? 0),
      rarestType,
      rarestPct: rarestType ? Math.round((rarestFreq / deviceCount) * 100) : 0,
      radiusKm: r.radiusKm,
      rank: 0,
      seasonRank: 0,
    };
  });

  // INVARIANT: a published row's score and its counters derive from the same
  // claim map, so "a score with zero claims" is not a state that can exist.
  // isRankable() removes the one shape known to produce it; this enforces the
  // property itself rather than trusting that enumeration to stay complete. A
  // row that still manages to violate it is DROPPED, not clamped and not
  // published -- an unexplained absence is recoverable, a number a customer can
  // see is contradicted by the number beside it is not. Logged so a recurrence
  // is findable instead of silent.
  // Stated precisely, because the loose version ("points with no claimed types")
  // rejects a legitimate row: the wire permits claimed countries/airlines without
  // claimed types, and the growth-clamp path relies on it. The two things that
  // genuinely cannot be true are a score with NOTHING claimed, and a "rarest
  // catch" with no claimed types to have been rarest among.
  const scored = allScored.filter((r) => {
    const claims = r.counts.types + r.counts.airlines + r.counts.countries + r.counts.airports;
    const contradictory = (r.points > 0 && claims === 0) || (r.rarestType !== "" && r.counts.types === 0);
    if (contradictory)
      console.log(JSON.stringify({ evt: "incoherent_row", id: r.id, points: r.points }));
    return !contradictory;
  });

  const byPoints = [...scored].sort((a, b) => b.points - a.points);
  byPoints.forEach((r, i) => (r.rank = i + 1));
  const bySeason = [...scored].sort((a, b) => b.seasonPoints - a.seasonPoints);
  bySeason.forEach((r, i) => (r.seasonRank = i + 1));

  // Lifetime leader: the biggest claimed tally in each category.
  const topBy = (sel: (c: Counts) => number): Leader => {
    let best: Leader = { name: "", count: 0 };
    for (const r of scored) {
      const v = sel(r.counts);
      if (v > best.count) best = { name: r.name, count: v };
    }
    return best;
  };
  // Season leader: the biggest GROWTH since the month start, which is a different
  // question and a different winner. Ranking the season tab by lifetime totals
  // would have made "this season" a slower-updating copy of "lifetime".
  const growth = new Map<string, Counts>();
  for (const r of rows) {
    const cl = r.claimed ?? { types: 0, airlines: 0, countries: 0, airports: 0 };
    const ms = r.seasonMonth === season ? r.monthStartCounts : cl;
    growth.set(r.id, {
      // TYPES ARE COUNTED FROM THEIR CLAIM MONTH, not from a count delta, because
      // that is how season POINTS already work (seasonTypePts sums types stamped
      // with this month). Using cl.types - ms.types here instead looked right and
      // was silently always 0 for a device on its first submission, since
      // monthStartCounts is seeded from that submission -- so the season tab's
      // whole "category leaders" block rendered empty and the page hid it.
      types: Object.values(r.types).filter((m) => m === season).length,
      // The other three genuinely are deltas: they arrive as tallies with no
      // per-item date, so growth-since-month-start is the only season signal
      // available for them.
      airlines: Math.max(0, cl.airlines - ms.airlines),
      countries: Math.max(0, cl.countries - ms.countries),
      airports: Math.max(0, cl.airports - ms.airports),
    });
  }
  const topBySeason = (sel: (c: Counts) => number): Leader => {
    let best: Leader = { name: "", count: 0 };
    for (const r of scored) {
      const g = growth.get(r.id);
      const v = g ? sel(g) : 0;
      if (v > best.count) best = { name: r.name, count: v };
    }
    return best;
  };

  const board: Board = {
    builtAt: now,
    season,
    rows: byPoints,
    leaders: {
      types: topBy((c) => c.types),
      airlines: topBy((c) => c.airlines),
      countries: topBy((c) => c.countries),
      airports: topBy((c) => c.airports),
    },
    seasonLeaders: {
      types: topBySeason((c) => c.types),
      airlines: topBySeason((c) => c.airlines),
      countries: topBySeason((c) => c.countries),
      airports: topBySeason((c) => c.airports),
    },
  };
  await env.ENRICH_KV.put("lb:board", JSON.stringify(board));
  return board;
}

async function getBoard(env: Env): Promise<Board> {
  const cached = await env.ENRICH_KV.get<Board>("lb:board", "json");
  if (cached && Date.now() - cached.builtAt < BOARD_STALE_MS && cached.season === monthOf(Date.now()))
    return cached;
  return buildBoard(env);
}

// ---- public read endpoints ---------------------------------------------------

// The board JSON. Shape is driven by pages/leaderboard.html, which is now the
// only consumer: the page is static and fetches this, so iterating on the page
// never touches this file and vice versa.
//
// BOTH SCOPES IN ONE RESPONSE. The old endpoint served one view per request via
// ?view=season, which made the page's tab switch a second network round trip for
// data the server had already computed -- buildBoard ranks lifetime AND season in
// the same pass. `points` inside each scope means THAT scope's points, so the
// page can render a row without knowing which tab it is on.
export async function handleLeaderboardJson(_request: Request, env: Env): Promise<Response> {
  const board = await getBoard(env);

  const rowOut = (r: ScoredRow, season: boolean) => ({
    // The profile key. It was NOT emitted before -- the old server-rendered board
    // built its own links from a value the JSON never carried, so a client-side
    // page had no way to construct /leaderboard/<id> and would have had to invent
    // one. Safe to publish: `id` is the device's LeaderboardId, the first 8 bytes
    // of SHA-256(MAC || salt) as 16 hex chars, computed on-device so the raw MAC
    // never leaves it and the id cannot be reversed to hardware. It is already
    // the public profile URL.
    id: r.id,
    rank: season ? r.seasonRank : r.rank,
    name: r.name,
    points: season ? r.seasonPoints : r.points,
    verified: r.verified,
    badges: r.badges,
    // `claimed` is what scores and what the page reads. `counts` is kept as an
    // alias for anything still reading the pre-rework name; `seen` is the
    // denominator that makes "47 of 156" possible, and was previously computed
    // server-side but never exposed.
    claimed: r.counts,
    counts: r.counts,
    seen: r.seen,
    rarestType: r.rarestType,
    rarestPct: r.rarestPct,
  });

  const lifetime = [...board.rows].sort((a, b) => a.rank - b.rank).slice(0, 100);
  const season = [...board.rows].sort((a, b) => a.seasonRank - b.seasonRank).slice(0, 100);

  return jsonResponse(
    {
      v: SCHEMA_V,
      // Machine-readable marker that this Worker scores CLAIMS. The deploy runbook
      // greps for it before running the irreversible KV reset. Deliberately not a
      // sentence from the page: the page's copy is edited freely, and a deploy
      // gate that breaks when someone improves a subtitle is a gate that gets
      // removed.
      scoring: "claims-v2",
      lifetime: { rows: lifetime.map((r) => rowOut(r, false)), leaders: board.leaders },
      season: { id: board.season, rows: season.map((r) => rowOut(r, true)), leaders: board.seasonLeaders },
    },
    200,
    // A browser page, not a device: overrides the shared helper's no-store, which
    // exists for devices polling every few seconds. NOTE this is not what governs
    // freshness -- BOARD_STALE_MS rebuilds the board at most every 5 min, so the
    // data can be ~5 min old however short this is. Lower that, not this, to make
    // the board feel live.
    { "Cache-Control": "public, max-age=60" },
  );
}

const BADGE_LABEL: Record<string, string> = {
  century: "💯 Century Club",
  widebody: "🛫 Widebody Collector",
  warbird: "🎖️ Warbird Spotter",
  globetrotter: "🌍 Globetrotter",
  streak30: "🔥 30-Day Streak",
  first: "🥇 First!",
};

function esc(s: string): string {
  return s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c] as string);
}

// The "top N percent" column is gone: the static board page renders rank by
// position and shows no percentile at all. The fix that lived here (rank 1 read
// "top 0 percent" at every fleet size, not just on a one-row board) is therefore
// moot rather than wrong, and the code is deleted instead of being kept warm for
// a column nothing renders. If a percentile comes back, it belongs in the page.

export function handleLeaderboardPage(): Response {
  const bytes = new TextEncoder().encode(leaderboardHtml);
  return new Response(bytes, {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Length": String(bytes.byteLength),
      // Longer than the JSON's 60s: the markup changes on deploys, the numbers
      // change on their own.
      "Cache-Control": "public, max-age=300",
    },
  });
}

export async function handleProfile(env: Env, id: string): Promise<Response> {
  if (!/^[0-9a-f]{8,32}$/.test(id)) return errorResponse(400, "bad_id");
  const board = await getBoard(env);
  const me = board.rows.find((r) => r.id === id);
  if (!me) return errorResponse(404, "not_found");
  const badges = me.badges.map((b) => `<li>${esc(BADGE_LABEL[b] ?? b)}</li>`).join("") || "<li class=dim>No badges yet</li>";
  const html = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>${esc(me.name)} — Blipscope Spotter</title>
<style>:root{color-scheme:light dark}body{font:15px/1.6 system-ui,sans-serif;max-width:560px;margin:0 auto;padding:2rem 1rem}h1{margin:0}.dim{color:#888}.stat{display:flex;justify-content:space-between;border-bottom:1px solid #8883;padding:.4rem 0}ul{padding-left:1.2rem}a{color:inherit}</style>
</head><body>
  <p class="dim"><a href="/leaderboard">← Leaderboard</a></p>
  <h1>${esc(me.name)} ${me.verified ? "✓" : ""}</h1>
  <p class="dim">Rank #${me.rank} lifetime · #${me.seasonRank} this season</p>
  <div class="stat"><span>Spotter Score</span><b>${me.points.toLocaleString("en-US")}</b></div>
  ${me.rarestType ? `<div class="stat"><span>Rarest catch</span><b>${esc(TYPE_NAMES[me.rarestType] ?? me.rarestType)} <span class="dim">(${me.rarestPct}% of spotters)</span></b></div>` : ""}
  <div class="stat"><span>Types claimed</span><b>${me.counts.types} <span class="dim">of ${me.seen.types} seen</span></b></div>
  <div class="stat"><span>Airlines</span><b>${me.counts.airlines} <span class="dim">of ${me.seen.airlines}</span></b></div>
  <div class="stat"><span>Countries</span><b>${me.counts.countries} <span class="dim">of ${me.seen.countries}</span></b></div>
  <div class="stat"><span>Airports</span><b>${me.counts.airports} <span class="dim">of ${me.seen.airports}</span></b></div>
  ${me.radiusKm ? `<div class="stat"><span>Play radius</span><b>${Math.round(me.radiusKm / 1.609)} mi${Math.round(me.radiusKm) < STANDARD_RADIUS_KM ? ` <span class="dim">(under the ${Math.round(STANDARD_RADIUS_KM / 1.609)} mi standard)</span>` : ""}</b></div>` : ""}
  <h2>Badges</h2><ul>${badges}</ul>
  <p class="dim" style="font-size:.85rem">Only aircraft you tapped on the device count. Type rarity is weighted by how many spotters have claimed it, so a quiet sky can still win.</p>
</body></html>`;
  const bytes = new TextEncoder().encode(html);
  return new Response(bytes, {
    status: 200,
    headers: { "Content-Type": "text/html; charset=utf-8", "Content-Length": String(bytes.byteLength), "Cache-Control": "public, max-age=300" },
  });
}
