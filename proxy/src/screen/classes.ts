// The screen service's class rule: THE OWNER'S 2026-11-25 RULE (registration A10), the same on the website, the app and
// this service, and the rule he set on Nov 7 for following a flight. One module, server-side.
//
//   HIDDEN ("private")  the aircraft is flagged PIA or LADD: dbFlags & (4 | 8), the bits as the site feed's own
//                       SiteAircraft type names them ("readsb bitfield: military 1, interesting 2, PIA 4, LADD 8").
//                       It is served type only, with the daily disguise (privateids.ts) and no dbFlags.
//   REMOVED             the aircraft is on the block list: not here, but a list in KV (cfg.ts, C1).
//   PUBLIC              everyone else, in full: registration, operator, route, photo. Military included. The earlier
//                       "private unless military or an airline-style callsign" is RETIRED (A10).
//
// G3: in a snapshot whose v is NOT 2, a null dbFlags counts as FLAGGED. A v1 row cannot carry LADD or PIA, so the only
// rule that cannot expose such an aircraft is to hide it (the site's ruling of 2026-11-04). Under v 2, null means "not
// received" on a row stored before the pass-through and reads 0, as the site does; such rows last until the feed's next
// write (about 10 s). That residual is stated, not hidden.
//
// `bare()` is how a hex is compared with the block list: a leading "~" (a non-ICAO relay of the same digits) is
// stripped, as the site does (G2).

export type AircraftClass = "public" | "private";

/** PIA (4) and LADD (8): the only flags that hide an aircraft (the owner's rule). */
const HIDING_FLAGS = 4 | 8;

/** A hex as the screen compares it: trimmed, lowercased; anything not a string is "". */
export const normHex = (h: unknown): string => (typeof h === "string" ? h.trim().toLowerCase() : "");
/** Exactly 6 hex digits: the only shape an aircraft in the snapshot can have here (no "~" non-ICAO ids). */
export const isHex6 = (h: string): boolean => /^[0-9a-f]{6}$/.test(h);

/** A hex for the BLOCK check (G2): a leading "~" stripped. */
export const bare = (hex: string): string => (hex.startsWith("~") ? hex.slice(1) : hex);

/** `v2` false (a snapshot whose v is not 2): a null dbFlags is flagged (G3). A row the site feed marked `dbFlagsBad`
 *  (the upstream sent a dbFlags that is not an integer in 0..15) is HIDDEN, like G3 (the owner, 2026-11-25:
 *  "malformed flags: hide"). The marker, never null, is what says "malformed": null also means readsb left it out (0). */
export function classify(a: { dbFlags: unknown; dbFlagsBad?: unknown }, v2 = true): AircraftClass {
  /*P-MF3*/
  if (a.dbFlagsBad === true) return "private";
  const known = typeof a.dbFlags === "number" && Number.isInteger(a.dbFlags);
  /*P-V2*/
  if (!known && !v2) return "private";
  const db = known ? (a.dbFlags as number) : 0;
  /*P-E2*/
  return (db & HIDING_FLAGS) !== 0 ? "private" : "public";
}
