import { describe, expect, it, beforeEach } from "vitest";
import { isRevoked, parseRevoked, resetRevocationCache, REVOKED_KEY } from "../src/revocation";
import type { Env } from "../src/types";

const REAL = "0123456789abcdef";
const OTHER = "1111222233334444";

function envWith(raw: string | null, opts: { throws?: boolean } = {}): Env {
  return {
    ENRICH_KV: {
      get: async (key: string) => {
        if (opts.throws) throw new Error("KV unavailable");
        return key === REVOKED_KEY ? raw : null;
      },
    },
  } as unknown as Env;
}

beforeEach(() => resetRevocationCache());

describe("parseRevoked", () => {
  it("accepts newline, comma and space separated ids, and lowercases", () => {
    const s = parseRevoked(`${REAL}\n${OTHER.toUpperCase()}, aaaabbbbccccdddd`);
    expect(s.has(REAL)).toBe(true);
    expect(s.has(OTHER)).toBe(true);
    expect(s.has("aaaabbbbccccdddd")).toBe(true);
  });

  it("ignores comments and blank lines", () => {
    const s = parseRevoked(`# stolen unit, RMA 2026-08\n\n${REAL}\n\n`);
    expect(s.size).toBe(1);
    expect(s.has(REAL)).toBe(true);
  });

  // THE ONE THAT MATTERS: a malformed entry must be dropped, never treated as a
  // wildcard, and must not take the well-formed ids down with it.
  it("drops malformed entries without discarding valid ones", () => {
    const s = parseRevoked(`nothex!!\n${REAL}\nZZZZ\n123\n*\n""`);
    expect(s.has(REAL)).toBe(true);
    expect(s.size).toBe(1);
    for (const bad of ["nothex!!", "zzzz", "123", "*", '""', ""]) expect(s.has(bad)).toBe(false);
  });

  it("treats empty/missing/whitespace-only content as an empty list", () => {
    for (const raw of [null, "", "   ", "\n\n", "# only a comment"]) {
      expect(parseRevoked(raw).size).toBe(0);
    }
  });
});

describe("isRevoked", () => {
  it("denies exactly the listed device", async () => {
    const env = envWith(REAL);
    expect(await isRevoked(env, REAL)).toBe(true);
    resetRevocationCache();
    expect(await isRevoked(env, OTHER)).toBe(false);
  });

  it("is case-insensitive on the presented id", async () => {
    expect(await isRevoked(envWith(REAL), REAL.toUpperCase())).toBe(true);
  });

  // --- the "cannot deny the whole fleet" contract -------------------------
  it("denies NOBODY when the entry is empty or missing", async () => {
    for (const raw of [null, "", "   ", "# nothing revoked yet"]) {
      resetRevocationCache();
      expect(await isRevoked(envWith(raw), REAL)).toBe(false);
      resetRevocationCache();
      expect(await isRevoked(envWith(raw), OTHER)).toBe(false);
    }
  });

  it("denies NOBODY when the entry is malformed junk", async () => {
    for (const raw of ["*", "all", "yes", "true", '{"revoked":"all"}', "0", "-"]) {
      resetRevocationCache();
      expect(await isRevoked(envWith(raw), REAL)).toBe(false);
      resetRevocationCache();
      expect(await isRevoked(envWith(raw), OTHER)).toBe(false);
    }
  });

  it("FAILS OPEN when KV throws -- a storage blip must not black out the fleet", async () => {
    const env = envWith(null, { throws: true });
    expect(await isRevoked(env, REAL)).toBe(false);
    expect(await isRevoked(env, OTHER)).toBe(false);
  });

  it("never matches an empty or malformed presented id", async () => {
    // A device that sends no id must not collide with a stray entry, and a
    // wildcard-looking id must not be honoured.
    const env = envWith(`${REAL}\n${OTHER}`);
    for (const id of ["", "   ", "*", "nothex", "ZZ"]) {
      expect(await isRevoked(env, id)).toBe(false);
    }
  });

  // ---- A '#' COMMENTS OUT ITS LINE, NOT ONE TOKEN --------------------------
  //
  // THE 2026-08-31 PRODUCTION INCIDENT. parseRevoked split the whole blob on
  // whitespace and commas and skipped only tokens that START WITH '#'. So a '#'
  // commented out exactly itself, and every word after it on the line was still
  // parsed -- making any id-shaped token inside prose a live revocation, in a
  // file whose header says "annotate freely".
  //
  // It fired while somebody was being careful: an annotation explaining WHY a
  // board was revoked named the synthetic bench identity, and revoked it. The
  // symptom was "production refuses a correctly-derived key", which is
  // indistinguishable from a botched secret rotation -- and a secret rotation
  // was in progress at the time.
  //
  // Both shapes below are from the real file. The second is the one that bit:
  // an id in the middle of a sentence, not on a line of its own.
  it("does NOT revoke an id that appears inside a comment", () => {
    const raw = [
      "# Revoked device ids, one per line.",
      `# ${OTHER} is the synthetic bench identity, do not revoke it`,
      REAL,
    ].join("\n");
    const ids = parseRevoked(raw);
    expect(ids.has(REAL)).toBe(true);          // the real entry still lands
    expect(ids.has(OTHER)).toBe(false);        // the mentioned one does not
    expect(ids.size).toBe(1);
  });

  it("does NOT revoke an id buried mid-sentence in a comment", () => {
    // The exact shape that fired: prose, a comma after the id, more words after.
    const raw = [
      "# 2026-08-31 -- the workstation held this board's key where the runbook",
      `# specifies the synthetic ${OTHER}, and four secrets reached a transcript.`,
      REAL,
    ].join("\n");
    const ids = parseRevoked(raw);
    expect(ids.has(OTHER)).toBe(false);
    expect([...ids]).toEqual([REAL]);
  });

  it("CONTROL: the same ids OUTSIDE a comment are still revoked", () => {
    // Without this the two assertions above pass against a parser that revokes
    // nothing at all, which is the other way to be catastrophically wrong here.
    const ids = parseRevoked(`${REAL}\n${OTHER}`);
    expect(ids.has(REAL)).toBe(true);
    expect(ids.has(OTHER)).toBe(true);
    expect(ids.size).toBe(2);
  });

  it("a trailing comment on an entry line leaves the entry revoked", () => {
    const ids = parseRevoked(`${REAL}   # RMA, board resold`);
    expect([...ids]).toEqual([REAL]);
  });

  it("survives CRLF, which is what a Windows operator will paste", () => {
    const ids = parseRevoked(`# note about ${OTHER}\r\n${REAL}\r\n`);
    expect(ids.has(OTHER)).toBe(false);
    expect(ids.has(REAL)).toBe(true);
  });

  it("caches: a second call in the window does not re-read KV", async () => {
    let reads = 0;
    const env = {
      ENRICH_KV: { get: async () => { reads++; return REAL; } },
    } as unknown as Env;
    expect(await isRevoked(env, REAL)).toBe(true);
    expect(await isRevoked(env, REAL)).toBe(true);
    expect(await isRevoked(env, OTHER)).toBe(false);
    expect(reads).toBe(1);
  });
});
