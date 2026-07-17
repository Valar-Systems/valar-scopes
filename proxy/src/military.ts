// Military ICAO 24-bit address allocations, for the enrich "military floor":
// many military airframes have no record in the upstream aircraft DB, so their
// detail card resolved a position and nothing else. When the hex sits in a
// military allocation and no operator resolved, we fill `op` with a truthful
// generic ("US military") rather than leaving the card blank. Never types,
// never registrations -- only the fact the allocation itself proves.
//
// This is the long-standing VirtualRadarServer "military ranges" table,
// reproduced widely across ADS-B tooling, and kept IDENTICAL to the firmware's
// offline classifier (src/SpecialAircraft.cpp) so the proxy and the on-screen
// MIL tag can never disagree. Ranges are sorted ascending and non-overlapping;
// labels are set only where the allocation is clearly attributable.

interface MilRange {
  lo: number;
  hi: number;
  op: string;
}

const GENERIC = "Military";

const MIL_RANGES: MilRange[] = [
  { lo: 0x010070, hi: 0x01008f, op: "Egyptian military" },
  { lo: 0x0a4000, hi: 0x0a4fff, op: "Algerian military" },
  { lo: 0x33ff00, hi: 0x33ffff, op: "Italian military" },
  { lo: 0x350000, hi: 0x37ffff, op: "Spanish military" },
  { lo: 0x3aa000, hi: 0x3affff, op: "French military" },
  { lo: 0x3b7000, hi: 0x3bffff, op: "French military" },
  { lo: 0x3ea000, hi: 0x3ebfff, op: "German military" },
  { lo: 0x3f4000, hi: 0x3fbfff, op: "German military" },
  { lo: 0x400000, hi: 0x40003f, op: "UK military" },
  { lo: 0x43c000, hi: 0x43cfff, op: "UK military" },
  { lo: 0x444000, hi: 0x446fff, op: "Belgian military" },
  { lo: 0x44f000, hi: 0x44ffff, op: GENERIC },
  { lo: 0x457000, hi: 0x457fff, op: GENERIC },
  { lo: 0x45f400, hi: 0x45f4ff, op: GENERIC },
  { lo: 0x468000, hi: 0x4683ff, op: "Greek military" },
  { lo: 0x473c00, hi: 0x473c0f, op: GENERIC },
  { lo: 0x478100, hi: 0x4781ff, op: GENERIC },
  { lo: 0x480000, hi: 0x480fff, op: "Dutch military" },
  { lo: 0x48d800, hi: 0x48d87f, op: GENERIC },
  { lo: 0x497c00, hi: 0x497cff, op: GENERIC },
  { lo: 0x498420, hi: 0x49842f, op: GENERIC },
  { lo: 0x4b7000, hi: 0x4b7fff, op: "Swiss military" },
  { lo: 0x4b8200, hi: 0x4b82ff, op: GENERIC },
  { lo: 0x70c070, hi: 0x70c07f, op: GENERIC },
  { lo: 0x710258, hi: 0x71028f, op: GENERIC },
  { lo: 0x710380, hi: 0x71039f, op: GENERIC },
  { lo: 0x738a00, hi: 0x738aff, op: "Israeli military" },
  { lo: 0x7cf800, hi: 0x7cfaff, op: "Australian military" },
  { lo: 0x800200, hi: 0x8002ff, op: "Indian military" },
  { lo: 0xadf7c8, hi: 0xafffff, op: "US military" },
  { lo: 0xc20000, hi: 0xc3ffff, op: "Canadian military" },
  { lo: 0xe40000, hi: 0xe41fff, op: "Brazilian military" },
];

// P3: military callsign designators, filling `op` from the broadcast callsign
// when neither the DB record nor anything else produced one. A designator is
// MORE specific than the allocation floor (RCH proves Air Mobility Command;
// the hex block only proves "US military"), so the enrich path consults this
// before militaryOperator(). Only unambiguous, ICAO-assigned or
// long-established military designators belong here -- never squadron
// tactical names a civil operator could plausibly reuse.
const MIL_CALLSIGN_OPS: Record<string, string> = {
  // United States
  RCH: "Air Mobility Command", // REACH
  CNV: "US Navy", // CONVOY -- Navy logistics
  PAT: "US Army Priority Air Transport",
  SAM: "US Air Force Special Air Mission", // 89th AW VIP
  SPAR: "US Air Force Special Air Mission",
  KING: "US Air Force rescue", // HC-130
  JOLLY: "US Air Force rescue", // HH-60
  // Europe / NATO
  RRR: "Royal Air Force", // spoken "ASCOT"
  RFR: "Royal Air Force", // spoken "RAFAIR"
  GAF: "German Air Force",
  GAM: "German Army",
  CTM: "French Air Force", // COTAM
  FAF: "French Air Force",
  FNY: "French Navy",
  IAM: "Italian Air Force",
  BAF: "Belgian Air Force",
  NAF: "Royal Netherlands Air Force",
  PLF: "Polish Air Force",
  SVF: "Swedish Air Force",
  NOW: "Royal Norwegian Air Force",
  DAF: "Danish Air Force",
  AME: "Spanish Air Force",
  AFP: "Portuguese Air Force",
  NATO: "NATO",
  // Rest of world
  CFC: "Royal Canadian Air Force", // CANFORCE
  ASY: "Royal Australian Air Force", // AUSSIE
};

// Operator from a broadcast callsign's designator prefix; "" when it isn't a
// known military one. Expects the router's normalized callsign (uppercase
// alphanumeric) and requires the letters-then-digit shape ("RCH4571"), so a
// registration flown as callsign ("N123AB") or a bare word can never match.
export function militaryCallsignOperator(cs: string): string {
  const m = /^([A-Z]{3,5})[0-9]/.exec(cs);
  return m ? (MIL_CALLSIGN_OPS[m[1]] ?? "") : "";
}

// Operator floor for a hex in a military allocation; "" when it isn't in one.
// Accepts readsb's "~" TIS-B prefix (stripped, like the firmware's parser).
export function militaryOperator(hexRaw: string): string {
  const hex = hexRaw.startsWith("~") ? hexRaw.slice(1) : hexRaw;
  if (!/^[0-9a-fA-F]{6}$/.test(hex)) return "";
  const a = parseInt(hex, 16);
  for (const r of MIL_RANGES) {
    if (a < r.lo) return ""; // sorted ascending: no later range can contain it
    if (a <= r.hi) return r.op;
  }
  return "";
}
