/**
 * routes-guards.ts -- the weekly route refresh's deletion guards and freeze rule (docs/route-weekly-refresh.md, 2.5;
 * docs/route-weekly-code.md, W1). Pure functions: no KV, no network, no clock but the one passed in. The writer
 * (ingest-routes.ts --key-level) calls them; routes-guards.selftest.mts grades them, red-first.
 *
 * WHY EACH BOUND IS WHERE IT IS (measured 2026-11-13 on standing-data, counts only):
 *   - an ordinary week drops 3-4 keys, and no surviving shard loses more than 0.3% of its keys;
 *   - the 79 days 08-25 to 11-12 dropped 827: 715 from surviving shards (one shard lost 627 keys, 94%), 112 from 5
 *     shards removed outright. Those events would refuse here, and that is intended: a person looks, and an attended
 *     run passes a named override. A scheduled run can never pass one (G5 below, and the writer refuses the flags).
 *
 * A refusal of DELETES does not refuse the run's writes, but the run does not seal (the reviewer's ruling: a run that
 * cannot delete must not reset the age as if it had). G1 alone refuses everything, writes included, because a
 * truncated source is the one input that must never be mistaken for a smaller world.
 */

export interface GuardResult {
  ok: boolean;
  reason: string;
  /** G1 only: the source itself is untrustworthy, so nothing is written either. */
  refuseAll?: boolean;
  /** G3/G4: the shard file names concerned (airline files: public, never a callsign), so an attended run can name
   * them in an override. */
  names?: string[];
}

/** The ingest's own sane band on the row count, re-used, plus a floor relative to the previous snapshot. */
export const MIN_ROWS = 400_000;
export const MAX_ROWS = 900_000;
export const MIN_RATIO = 0.99;
/** G3: a surviving shard is refused only when it loses MORE than this share AND more than this many keys. */
export const SHARD_SHARE = 0.25;
export const SHARD_KEYS = 20;
/** G4: removed shards (absent from the new source entirely), per run. */
export const MAX_REMOVED_SHARDS = 3;
export const MAX_REMOVED_KEYS = 200;
/** G5: deletes per scheduled run. */
export const MAX_DELETES_SCHEDULED = 250;
/** G6: the mirror's key shape. A bucketed runtime row (rt:<cs>:<n>) has a colon and can never match. */
export const MIRROR_KEY = /^rt:[A-Z0-9]{2,8}$/;

/** G1: is the new source whole? */
export function g1Completeness(prevRows: number, newRows: number): GuardResult {
  if (!Number.isFinite(newRows) || newRows < MIN_ROWS || newRows > MAX_ROWS) {
    return { ok: false, refuseAll: true, reason: `G1: ${newRows} usable rows is outside ${MIN_ROWS}-${MAX_ROWS}` };
  }
  if (Number.isFinite(prevRows) && prevRows > 0 && newRows < prevRows * MIN_RATIO) {
    return { ok: false, refuseAll: true, reason: `G1: ${newRows} usable rows is under ${MIN_RATIO * 100}% of the previous ${prevRows}` };
  }
  return { ok: true, reason: "G1: complete" };
}

/** G2: every anchor callsign present in the new source -- and, as the list's own control, in the previous one. */
export function g2Anchors(anchors: string[], prevKeys: Set<string>, newKeys: Set<string>): GuardResult {
  if (anchors.length === 0) return { ok: false, reason: "G2: no anchors given (an empty list proves nothing)" };
  const notPrev = anchors.filter((a) => !prevKeys.has(a)).length;
  if (notPrev > 0) return { ok: false, reason: `G2: ${notPrev} anchor(s) absent from the PREVIOUS snapshot: the anchor list is wrong` };
  const missing = anchors.filter((a) => !newKeys.has(a)).length;
  if (missing > 0) return { ok: false, reason: `G2: ${missing} of ${anchors.length} anchor(s) missing from the new source` };
  return { ok: true, reason: `G2: ${anchors.length} of ${anchors.length} anchors present` };
}

/** G3: no surviving shard loses more than SHARD_SHARE of its keys AND more than SHARD_KEYS keys, unless named. */
export function g3SurvivingShare(prevByShard: Map<string, Set<string>>, newByShard: Map<string, Set<string>>, allowShards: Set<string>): GuardResult {
  const offenders: string[] = [];
  for (const [shard, before] of prevByShard) {
    const after = newByShard.get(shard);
    if (!after) continue; // removed shards are G4's case, not this one
    let lost = 0;
    for (const k of before) if (!after.has(k)) lost++;
    if (lost > SHARD_KEYS && lost / before.size > SHARD_SHARE && !allowShards.has(shard)) offenders.push(shard);
  }
  if (offenders.length > 0) return { ok: false, names: offenders, reason: `G3: ${offenders.length} surviving shard(s) would lose more than ${SHARD_SHARE * 100}% and more than ${SHARD_KEYS} keys` };
  return { ok: true, reason: "G3: no surviving shard over its bound" };
}

/** G4: shards removed from the source entirely -- at most MAX_REMOVED_SHARDS holding at most MAX_REMOVED_KEYS. */
export function g4Removed(prevByShard: Map<string, Set<string>>, newByShard: Map<string, Set<string>>, allowRemoved: number): GuardResult {
  const removed = [...prevByShard.keys()].filter((s) => !newByShard.has(s));
  const keys = removed.reduce((n, s) => n + (prevByShard.get(s)?.size ?? 0), 0);
  // B2 (the review): every comparison against NaN is false, so a bad bound would pass ANY count. Refused here too.
  if (!Number.isInteger(allowRemoved) || allowRemoved < 0) {
    return { ok: false, names: removed, reason: `G4: the allowance ${String(allowRemoved)} is not a non-negative integer: refused` };
  }
  const maxShards = Math.max(MAX_REMOVED_SHARDS, allowRemoved);
  if (removed.length > maxShards || (keys > MAX_REMOVED_KEYS && allowRemoved < removed.length)) {
    return { ok: false, names: removed, reason: `G4: ${removed.length} shard(s) removed, holding ${keys} keys (bounds ${MAX_REMOVED_SHARDS} and ${MAX_REMOVED_KEYS})` };
  }
  return { ok: true, reason: `G4: ${removed.length} shard(s) removed, holding ${keys} keys` };
}

/** G5: the run cap. An override (maxDeletes) is honoured only when the run is NOT scheduled. */
export function g5Cap(deletes: number, o: { scheduled: boolean; maxDeletes?: number }): GuardResult {
  if (o.scheduled && o.maxDeletes !== undefined) return { ok: false, reason: "G5: an override was passed to a scheduled run: refused" };
  // B2 (the review): a NaN cap made `deletes > cap` false for every count. Refused, never coerced.
  if (o.maxDeletes !== undefined && (!Number.isInteger(o.maxDeletes) || o.maxDeletes < 0)) {
    return { ok: false, reason: `G5: the cap ${String(o.maxDeletes)} is not a non-negative integer: refused` };
  }
  const cap = o.scheduled || o.maxDeletes === undefined ? MAX_DELETES_SCHEDULED : o.maxDeletes;
  if (deletes > cap) return { ok: false, reason: `G5: ${deletes} deletes is over the cap of ${cap}` };
  return { ok: true, reason: `G5: ${deletes} deletes, cap ${cap}` };
}

/** G6: every delete key has the mirror's shape AND was written by the mirror (it is in the proven previous snapshot). */
export function g6ShapeAndProvenance(deleteKeys: string[], prevKeys: Set<string>): GuardResult {
  const badShape = deleteKeys.filter((k) => !MIRROR_KEY.test(k)).length;
  const notOurs = deleteKeys.filter((k) => MIRROR_KEY.test(k) && !prevKeys.has(k)).length;
  if (badShape + notOurs > 0) return { ok: false, reason: `G6: ${badShape} key(s) out of shape, ${notOurs} not in the previous snapshot` };
  return { ok: true, reason: `G6: ${deleteKeys.length} delete key(s), all in shape and all the mirror's` };
}

const ISO_UTC = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/;
/** The freeze: unset or past -> run; future -> skip; anything else -> fail (never a quiet run, never a quiet skip). */
export function freeze(nowIso: string, freezeUntil: string | undefined): { action: "run" | "skip" | "fail"; reason: string } {
  if (freezeUntil === undefined || freezeUntil === "") return { action: "run", reason: "no freeze set" };
  if (!ISO_UTC.test(freezeUntil) || Number.isNaN(Date.parse(freezeUntil))) {
    return { action: "fail", reason: "ROUTES_FREEZE_UNTIL is set but is not an ISO UTC time (YYYY-MM-DDTHH:MM:SSZ)" };
  }
  return Date.parse(nowIso) < Date.parse(freezeUntil)
    ? { action: "skip", reason: `frozen until ${freezeUntil}` }
    : { action: "run", reason: `the freeze ended at ${freezeUntil}` };
}
