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
