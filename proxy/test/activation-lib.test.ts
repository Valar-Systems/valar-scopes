import { describe, expect, it } from "vitest";
import {
  chunk, coverage, errorCode, parseImport, parseRecords, planSeed, seedWith, sumAdd, summarize, summarizeSeed,
  summarizeSweep, sweepCounts, failureText,
  type SeedStore,
} from "../scripts/activation-lib";

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

/* ===========================================================================
 * K1-K6 (docs/device-activation.md, "Seeding before the deploy"). Fake ids from
 * scripts/device-id-allowlist.txt; made-up 02: MACs.
 * ======================================================================== */

const A = "1111222233334444", B = "aaaabbbbccccdddd", C = "0123456789abcdef";

/** A store that drops `drop` on the first put (once) or on every put (always); `states` records what each id holds. */
function memStore(dropFirst: string | null, dropAlways: string | null): SeedStore & { rows: Set<string>; puts: string[][]; states: Map<string, string> } {
  const rows = new Set<string>();
  const states = new Map<string, string>();
  const puts: string[][] = [];
  let first = true;
  return {
    rows, puts, states,
    async listedIds() { return new Set(rows); },
    async putRows(ids, state) {
      puts.push([...ids]);
      for (const id of ids) {
        if (id === dropAlways) continue;
        if (first && id === dropFirst) continue;
        rows.add(id);
        states.set(id, state);
      }
      first = false;
    },
  };
}

describe("K: the seed", () => {
  it("K1: CSV mode reads the device_id column, names a bad row by its line number, ignores other columns", () => {
    const csv = `ts,device_id,mac,source\n2026-08-01,${A},02:00:00:00:00:01,provisioned\n2026-08-02,not-an-id,02:00:00:00:00:02,\n\n2026-08-03,${B.toUpperCase()},02:00:00:00:00:03,\n`;
    const p = parseRecords(csv);
    expect(p.mode).toBe("csv");
    expect(p.ids).toEqual([A, B]);
    expect(p.macs).toEqual([]);
    expect(p.badLines).toEqual([3]);
    expect(JSON.stringify(p)).not.toContain("not-an-id");
  });

  it("K2: ids mode for 16 hex per line; MAC/log mode otherwise", () => {
    const ids = parseRecords(`# stock boards\n${A}\n${B}\n${A}\n`);
    expect(ids).toEqual({ mode: "ids", ids: [A, B], macs: [], badLines: [] });
    const macs = parseRecords("02:00:00:00:00:01\n02:00:00:00:00:02\n");
    expect(macs.mode).toBe("macs");
    expect(macs.macs).toEqual(["02:00:00:00:00:01", "02:00:00:00:00:02"]);
  });

  it("K3: the plan skips every id that already has a row", () => {
    expect(planSeed([A, B, C], new Set([B]))).toEqual([A, C]);
    expect(planSeed([A], new Set([A]))).toEqual([]);
  });

  it("K4: a key dropped silently on the first put is repaired, and the result says so", async () => {
    const store = memStore(B, null);
    const r = await seedWith(store, [A, B, C]);
    expect(r).toEqual({ read: 3, alreadyPresent: 0, written: 3, repaired: 1, missing: 0, complete: true });
    expect(store.puts).toEqual([[A, B, C], [B]]);
    expect(summarizeSeed("staging", r)).toBe("seed (staging): read 3, already present 0, written 3, repaired 1, missing 0 -> COMPLETE");
  });

  it("K5: a key that never lands leaves the seed INCOMPLETE, never a success", async () => {
    const r = await seedWith(memStore(null, C), [A, B, C]);
    expect(r.missing).toBe(1);
    expect(r.complete).toBe(false);
    expect(summarizeSeed("production", r)).toContain("INCOMPLETE");
    expect(summarizeSeed("production", r)).not.toMatch(/[0-9a-f]{16}/);
  });
});

describe("K7: grandfathering writes activated rows (ruled ~19:50Z)", () => {
  it("seedWith(..., \"activated\") writes activated rows and leaves an existing listed row untouched", async () => {
    const store = memStore(null, null);
    store.rows.add(A);
    store.states.set(A, "listed");
    const r = await seedWith(store, [A, B, C], "activated");
    expect(r).toEqual({ read: 3, alreadyPresent: 1, written: 2, repaired: 0, missing: 0, complete: true });
    expect(store.states.get(A)).toBe("listed");
    expect(store.states.get(B)).toBe("activated");
    expect(store.states.get(C)).toBe("activated");
  });
});

describe("K6: the coverage arithmetic", () => {
  it("counts the union, the listed, the missing split by ledger, and the test-identity boolean", () => {
    const seen = new Set([A, B, C]);
    expect(coverage(seen, new Set([A]), new Set([B]), C)).toEqual({
      union: 3, listed: 1, missing: 2, missingWithLedgerRow: 1, missingWithoutLedgerRow: 1, testIdentityMissing: true,
    });
    expect(coverage(seen, new Set([A, B, C]), new Set(), null)).toEqual({
      union: 3, listed: 3, missing: 0, missingWithLedgerRow: 0, missingWithoutLedgerRow: 0, testIdentityMissing: null,
    });
  });
});

describe("M1: the sweep's arithmetic (accepted ~20:15Z)", () => {
  it("counts stamped from the gen read back, not from the state, and prints no id", () => {
    const c = sweepCounts(["activated", "activated", "activated", "listed"], ["0123456789ab", undefined, "not-a-gen", "abcdef012345"]);
    expect(c).toEqual({ swept: 4, activated: 3, stamped: 2, unstamped: 2 });
    const line = summarizeSweep("production", c);
    expect(line).toBe("sweep (production): swept 4, activated 3, stamped 2, unstamped 2");
    expect(line).not.toMatch(/[0-9a-f]{16}/);
  });
});

describe("F1: a failed wrangler call names its cause and never an id (G-1, ruled ~20:35Z)", () => {
  it("keeps the error, masks every id, drops key lines and the known config warning", () => {
    const stderr = [
      "\x1b[33m▲ \x1b[43;33m[\x1b[43;30mWARNING\x1b[43;33m]\x1b[0m \x1b[1mProcessing wrangler.toml configuration:\x1b[0m",
      "    - \"env.production\" environment configuration",
      "      - \"unsafe\" fields are experimental and may change or break at any time.",
    ].join("\n");
    const stdout = [
      "[",
      `  { "name": "enr:dev:${A}" },`,
      `  { "name": "enr:dev:${B}" },`,
      "]",
      `✘ [ERROR] A request to the Cloudflare API failed for ${C}: Authentication error [code: 10000]`,
    ].join("\n");
    const text = failureText(stdout, stderr);
    expect(text).toContain("Authentication error [code: 10000]");
    expect(text).toContain("<id>");
    expect(text).not.toMatch(/[0-9a-f]{16}/i);
    expect(text).not.toContain("unsafe");
    expect(text).not.toContain("Processing wrangler.toml");
    expect(failureText("", "")).toBe("(wrangler printed nothing)");
  });
});
