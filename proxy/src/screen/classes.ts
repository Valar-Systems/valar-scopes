// The screen service's class rules (phase 1, the enrich part; registration D5 and A2.2). One module, server-side, so
// the website and the app class an aircraft identically. Transcribed from the app's rule (another repo's
// src/live.ts, quoted in A2.2): the WEAKER form of a contract, and test/screen-enrich.test.ts's P-E2 grades it row by
// row. The military table is the FIRMWARE's (src/SpecialAircraft.cpp), generated into milranges.gen.ts and held
// equal to that file by scripts/check-screen.ts's P-E3.
//
//   military = the hex is in the firmware's MIL_RANGES, OR dbFlags & 1
//   flagged  = dbFlags & (4 | 8)            (PIA, LADD)
//   PRIVATE  = flagged OR (NOT military AND the callsign is not airline-shaped)
//   dbFlags null (not received) means 0 (A2.1, v2): an airliner with no flags is public.
//
// BLOCKED is not here: it is a list in KV (cfg.ts, C1), never a committed file.
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

export function classify(a: { hex: unknown; cs: unknown; dbFlags: unknown }): AircraftClass {
  const db = typeof a.dbFlags === "number" && Number.isInteger(a.dbFlags) ? a.dbFlags : 0;
  const military = isMilitaryHex(normHex(a.hex)) || (db & 1) !== 0;
  const flagged = (db & (4 | 8)) !== 0;
  const cs = typeof a.cs === "string" ? a.cs.trim().toUpperCase() : "";
  /*P-E2*/
  return flagged || (!military && !AIRLINE_CS.test(cs)) ? "private" : "public";
}
