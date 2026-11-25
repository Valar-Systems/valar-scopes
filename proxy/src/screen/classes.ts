// The screen service's class rules (phase 1, the enrich part; registration D5 and A2.2). One module, server-side, so
// the website and the app class an aircraft identically. Transcribed from the app's rule (another repo's
// src/live.ts, quoted in A2.2): the WEAKER form of a contract, and test/screen-enrich.test.ts's P-E2 grades it row by
// row. The military table is the FIRMWARE's (src/SpecialAircraft.cpp), generated into milranges.gen.ts and held
// equal to that file by scripts/check-screen.ts's P-E3.
//
//   military = the hex is in the firmware's MIL_RANGES, OR dbFlags & 1
//   flagged  = dbFlags & (4 | 8)            (PIA, LADD)
//   PRIVATE  = flagged OR (NOT military AND the callsign is not airline-shaped)
//   dbFlags null (not received) means 0 in a v2 snapshot (A2.1): an airliner with no flags is public. In a snapshot
//   whose v is NOT 2, a null dbFlags counts as FLAGGED (G3, the site's ruling of 2026-11-04: v1 rows cannot be checked
//   for LADD or PIA), and adds no military bit.
//
// BLOCKED is not here: it is a list in KV (cfg.ts, C1), never a committed file. `bare()` is how a hex is compared with
// it: a leading "~" (a non-ICAO relay of the same digits) is stripped, as the site does (G2).
import { MIL_RANGES } from "./milranges.gen";

export type AircraftClass = "public" | "private";

const AIRLINE_CS = /^[A-Z]{3}[0-9][0-9A-Z]{0,3}$/;

/** A hex as the screen compares it: trimmed, lowercased; anything not a string is "". */
export const normHex = (h: unknown): string => (typeof h === "string" ? h.trim().toLowerCase() : "");
/** Exactly 6 hex digits: the only shape an aircraft in the snapshot can have here (no "~" non-ICAO ids). */
export const isHex6 = (h: string): boolean => /^[0-9a-f]{6}$/.test(h);

export function isMilitaryHex(hex: string): boolean {
  if (!isHex6(hex)) return false;
  const n = parseInt(hex, 16);
  return MIL_RANGES.some(([lo, hi]) => n >= lo && n <= hi);
}

/** A hex for the BLOCK check (G2): a leading "~" stripped. */
export const bare = (hex: string): string => (hex.startsWith("~") ? hex.slice(1) : hex);

/** `v2` false (a snapshot whose v is not 2): a null dbFlags is flagged (G3). */
export function classify(a: { hex: unknown; cs: unknown; dbFlags: unknown }, v2 = true): AircraftClass {
  const known = typeof a.dbFlags === "number" && Number.isInteger(a.dbFlags);
  /*P-V2*/
  if (!known && !v2) return "private";
  const db = known ? (a.dbFlags as number) : 0;
  const military = isMilitaryHex(normHex(a.hex)) || (db & 1) !== 0;
  const flagged = (db & (4 | 8)) !== 0;
  const cs = typeof a.cs === "string" ? a.cs.trim().toUpperCase() : "";
  /*P-E2*/
  return flagged || (!military && !AIRLINE_CS.test(cs)) ? "private" : "public";
}
