import { describe, expect, it } from "vitest";
import { militaryCallsignOperator, militaryOperator } from "../src/military";

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
