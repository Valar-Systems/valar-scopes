import { describe, expect, it } from "vitest";
import { chunk, errorCode, parseImport, sumAdd, summarize } from "../scripts/activation-lib";

/* ===========================================================================
 * A12 (docs/device-activation.md): the tool prints no id, and reads a factory's
 * list or log. The MACs here are made up: 02: is the locally administered
 * prefix, which no factory board carries.
 * ======================================================================== */

const HEX16 = /[0-9a-f]{16}/;

describe("A12: summarize prints counts or a state, never an id", () => {
  it("add, reset and status lines carry no id even when the answer does", () => {
    const leaky = { listed: 2, already_listed: 1, already_activated: 0, id: "1111222233334444", ids: ["aaaabbbbccccdddd"] };
    const lines = [
      summarize("add", leaky),
      summarize("reset", { reopened: 1, already_open: 0, not_listed: 3, id: "1111222233334444" }),
      summarize("status", { state: "activated", id: "1111222233334444" }),
    ];
    for (const l of lines) {
      expect(l).not.toBeNull();
      expect(l!).not.toMatch(HEX16);
    }
    // CONTROL: they still say what happened.
    expect(lines[0]).toBe("add: 2 newly listed, 1 already listed, 0 already activated (left as they were)");
    expect(lines[1]).toBe("reset: 1 re-opened, 0 already open, 3 not on the list (reset never lists)");
    expect(lines[2]).toBe("status: activated");
  });

  it("an answer of the wrong shape is null, not a guess", () => {
    expect(summarize("add", { listed: "2" })).toBeNull();
    expect(summarize("status", { state: "1111222233334444" })).toBeNull();
  });

  it("sums add answers across batches", () => {
    expect(sumAdd([{ listed: 250, already_listed: 0, already_activated: 0 }, { listed: 9, already_listed: 1, already_activated: 2 }]))
      .toEqual({ listed: 259, already_listed: 1, already_activated: 2 });
  });

  it("an error code is echoed only if it is one of ours", () => {
    expect(errorCode({ error: "forbidden" })).toBe("forbidden");
    expect(errorCode({ error: "1111222233334444 is not listed" })).toBe("unknown");
    expect(errorCode(null)).toBe("unknown");
  });
});

describe("A12: parseImport reads a plain list and an esptool log", () => {
  it("a plain list: one MAC per line, any separator, deduplicated; comments and blanks skipped", () => {
    const p = parseImport("# batch 1\n02:00:00:00:00:01\n\n02-00-00-00-00-02\r\n020000000003\n02:00:00:00:00:01\n");
    expect(p.mode).toBe("list");
    expect(p.macs).toEqual(["02:00:00:00:00:01", "02:00:00:00:00:02", "02:00:00:00:00:03"]);
    expect(p.badLines).toEqual([]);
  });

  it("a bad line is named by its NUMBER, and its content goes nowhere", () => {
    const p = parseImport("02:00:00:00:00:01\nunit 7 1111222233334444\n02:00:00:00:00:02\n");
    expect(p.badLines).toEqual([2]);
    expect(JSON.stringify(p)).not.toContain("1111222233334444");
  });

  it("an esptool log: only the MAC: lines count, and the rest is ordinary log text", () => {
    const log = [
      "esptool v5.3.0",
      "Connected to ESP32-S3 on COM5:",
      "Chip type:          ESP32-S3 (QFN56) (revision v0.2)",
      "MAC:                02:00:00:00:00:0a",
      "Hash of data verified.",
      "MAC: 02:00:00:00:00:0B",
    ].join("\n");
    const p = parseImport(log);
    expect(p.mode).toBe("log");
    expect(p.macs).toEqual(["02:00:00:00:00:0a", "02:00:00:00:00:0b"]);
    expect(p.badLines).toEqual([]);
  });

  it("chunks to the route's batch size", () => {
    expect(chunk([1, 2, 3, 4, 5], 2)).toEqual([[1, 2], [3, 4], [5]]);
  });
});
