import { describe, expect, it } from "vitest";
import { militaryCallsignOperator, militaryOperator } from "../src/military";
import icaoCountry from "../../src/IcaoCountry.cpp?raw";
import militarySource from "../src/military.ts?raw";

describe("militaryOperator", () => {
  it("labels the US DoD block, boundaries inclusive", () => {
    expect(militaryOperator("adf7c8")).toBe("US military"); // lower bound
    expect(militaryOperator("ae1234")).toBe("US military");
    expect(militaryOperator("afffff")).toBe("US military"); // upper bound
    expect(militaryOperator("adf7c7")).toBe(""); // one below: civil N-number space
  });

  it("labels nationally attributed blocks and generic ones", () => {
    expect(militaryOperator("43c001")).toBe("UK military");
    expect(militaryOperator("3ea001")).toBe("German military");
    expect(militaryOperator("c20001")).toBe("Canadian military");
    expect(militaryOperator("44f001")).toBe("Military"); // unattributed allocation
  });

  it("returns empty for civil hexes", () => {
    expect(militaryOperator("4b1817")).toBe(""); // Swiss civil
    expect(militaryOperator("a00001")).toBe(""); // US civil
    expect(militaryOperator("ffffff")).toBe(""); // past the last range
  });

  it("accepts readsb's ~ TIS-B prefix and rejects malformed input", () => {
    expect(militaryOperator("~ae1234")).toBe("US military");
    expect(militaryOperator("zzz")).toBe("");
    expect(militaryOperator("")).toBe("");
    expect(militaryOperator("ae12")).toBe("");
  });
});

describe("militaryCallsignOperator (P3)", () => {
  it("resolves well-known military designators", () => {
    expect(militaryCallsignOperator("RCH4571")).toBe("Air Mobility Command");
    expect(militaryCallsignOperator("PAT025")).toBe("US Army Priority Air Transport");
    expect(militaryCallsignOperator("CNV6501")).toBe("US Navy");
    expect(militaryCallsignOperator("RRR2205")).toBe("Royal Air Force");
    expect(militaryCallsignOperator("GAF892")).toBe("German Air Force");
    expect(militaryCallsignOperator("JOLLY51")).toBe("US Air Force rescue");
  });

  it("requires the letters-then-digit shape", () => {
    expect(militaryCallsignOperator("RCH")).toBe(""); // bare designator, no flight number
    expect(militaryCallsignOperator("N123AB")).toBe(""); // registration as callsign
    expect(militaryCallsignOperator("RCHXY1")).toBe(""); // prefix is the full alpha run
    expect(militaryCallsignOperator("")).toBe("");
  });

  it("returns empty for civil airline designators", () => {
    expect(militaryCallsignOperator("SWR123")).toBe("");
    expect(militaryCallsignOperator("UAL1")).toBe("");
    expect(militaryCallsignOperator("DAL2205")).toBe("");
  });
});

// ICAO Annex 10 Vol III, Table 9-1, as the firmware transcribes it (src/IcaoCountry.cpp): the State a hex belongs to.
// Read from that file, not restated here, so this test and the on-device country lookup share one table.
function icaoState(hex: number): string | undefined {
  for (const m of icaoCountry.matchAll(/\{\s*0x([0-9A-Fa-f]{6}),\s*0x([0-9A-Fa-f]{6}),\s*"([^"]+)"\s*\}/g)) {
    if (hex >= parseInt(m[1]!, 16) && hex <= parseInt(m[2]!, 16)) return m[3];
  }
  return undefined;
}

describe("0x444000-0x446FFF is Austria's block (ICAO Annex 10 Vol III, Table 9-1), not Belgium's", () => {
  it("is labelled Austrian military at both ends", () => {
    expect(militaryOperator("444000")).toBe("Austrian military");
    expect(militaryOperator("446fff")).toBe("Austrian military");
  });

  it("lies inside Austria's ICAO allocation, read from the firmware's table", () => {
    expect(icaoState(0x444000)).toBe("Austria");
    expect(icaoState(0x446fff)).toBe("Austria");
    // The control: the table parsed, and Belgium's block starts just past Austria's.
    expect(icaoState(0x447fff)).toBe("Austria");
    expect(icaoState(0x448000)).toBe("Belgium");
  });
});

// THE WHOLE TABLE (#468's follow-up, predictions first). Every NATIONAL label in MIL_RANGES must name the ICAO State
// that holds its block at BOTH ends, read from src/IcaoCountry.cpp (icaoState above). The rows are read from
// src/military.ts itself, and each label is also checked through militaryOperator() at both ends.
// Predicted on main 26510c0, before this ran: 32 rows, 20 national and 12 generic, and all 20 match.
// Sabotages, run by hand and recorded on the PR: relabel one row, and the test goes red naming that row; give a row a
// label with no entry in STATE_OF, and it goes red saying so.
const STATE_OF: Record<string, string> = {
  "Egyptian military": "Egypt",
  "Algerian military": "Algeria",
  "Italian military": "Italy",
  "Spanish military": "Spain",
  "French military": "France",
  "German military": "Germany",
  "UK military": "United Kingdom",
  "Austrian military": "Austria",
  "Greek military": "Greece",
  "Dutch military": "Netherlands",
  "Swiss military": "Switzerland",
  "Israeli military": "Israel",
  "Australian military": "Australia",
  "Indian military": "India",
  "US military": "United States",
  "Canadian military": "Canada",
  "Brazilian military": "Brazil",
};

interface Row { lo: string; hi: string; op: string; generic: boolean }
const ROWS: Row[] = [...militarySource.matchAll(
  /\{\s*lo:\s*0x([0-9a-fA-F]{6}),\s*hi:\s*0x([0-9a-fA-F]{6}),\s*op:\s*(?:(GENERIC)|"([^"]+)")\s*\}/g,
)].map((m) => ({ lo: m[1]!.toLowerCase(), hi: m[2]!.toLowerCase(), op: m[4] ?? "Military", generic: m[3] === "GENERIC" }));

describe("every national label in MIL_RANGES names the ICAO State at both ends", () => {
  it("reads the whole table (the control)", () => {
    expect(ROWS.length).toBe(32);
    expect(ROWS.filter((r) => !r.generic).length).toBe(20);
    expect(ROWS.filter((r) => r.generic).length).toBe(12);
  });

  it.each(ROWS.filter((r) => !r.generic).map((r) => [r.lo, r.hi, r.op] as const))("%s-%s: %s", (lo, hi, op) => {
    const state = STATE_OF[op];
    expect(state, `"${op}" names no State in STATE_OF; add it with the name src/IcaoCountry.cpp uses`).toBeDefined();
    expect(icaoState(parseInt(lo, 16)), `${lo} is not in ${state}'s ICAO block`).toBe(state);
    expect(icaoState(parseInt(hi, 16)), `${hi} is not in ${state}'s ICAO block`).toBe(state);
    expect(militaryOperator(lo)).toBe(op);
    expect(militaryOperator(hi)).toBe(op);
  });

  it("labels every generic row Military at both ends", () => {
    for (const r of ROWS.filter((x) => x.generic)) {
      expect(militaryOperator(r.lo)).toBe("Military");
      expect(militaryOperator(r.hi)).toBe("Military");
    }
  });
});
