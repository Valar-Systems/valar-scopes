import { describe, expect, it } from "vitest";
import { checkEntryValue, mergeDeviceEntry, readBackMatches } from "../scripts/fleet-device-entry-lib";

// The tool that writes a device's cfg:fleet entry refuses any value the firmware would not apply unchanged
// (ruled 2026-11-05 ~19:50Z; docs/bend-site-feed.md). One case, on purpose: it is the tool's whole contract. The id is
// an allowlisted example (scripts/device-id-allowlist.txt), never a real device.
const ID = "b2c3d4e5f6a7b8c9";

describe("fleet-device-entry: the value check, the merge and the read-back", () => {
  it("accepts only whole numbers from 1000 to 4294967295 for a poll field, and the merge touches nothing else", () => {
    for (const ok of ["15000", "1000", "4294967295", "60000"]) {
      expect(checkEntryValue("pollNightMs", ok), ok).toEqual({ ok: true, field: "pollNightMs", value: Number(ok) });
    }
    for (const bad of ["999", "0", "4294967296", "15000.5", "15000.0", "-1", "+15000", "1e4", " 15000", "", "fast"]) {
      expect(checkEntryValue("pollNightMs", bad).ok, JSON.stringify(bad)).toBe(false);
    }
    expect(checkEntryValue("staleFactor", "3").ok).toBe(false);
    expect(checkEntryValue("enrich", "15000").ok).toBe(false);

    const before = { rev: 7, minFw: "5", defaults: { pollIdleMs: 15000 }, devices: { aaaabbbbccccdddd: { pollNightMs: 30000 } } };
    const after = mergeDeviceEntry(before, ID, "pollNightMs", 15000);
    expect(after).toEqual({ ...before, devices: { aaaabbbbccccdddd: { pollNightMs: 30000 }, [ID]: { pollNightMs: 15000 } } });
    expect(readBackMatches(before, after, ID, "pollNightMs", 15000)).toBe(true);
    // CONTROL: a read-back that moved anything else, or carries a different value, is not a match.
    expect(readBackMatches(before, { ...after, rev: 8 }, ID, "pollNightMs", 15000)).toBe(false);
    expect(readBackMatches(before, after, ID, "pollNightMs", 15001)).toBe(false);
  });
});
