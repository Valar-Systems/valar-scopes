import { env } from "cloudflare:test";
import { beforeEach, describe, expect, it } from "vitest";
import { REVOKED_KEY, parseRevoked, readRevoked, setRevoked } from "../src/revoke";
import type { Env } from "../src/types";

const E = env as unknown as Env;
const ID = "0123456789abcdef";
const ID2 = "a1b2c3d4e5f60718";

beforeEach(async () => {
  await E.ENRICH_KV.delete(REVOKED_KEY);
});

describe("parseRevoked", () => {
  it("reads the tolerant hand-edited format", () => {
    const s = parseRevoked(`# RMA 2026-08-04\n${ID}\n\n  ${ID2}  , 0123456789ABCDEF\n`);
    expect([...s].sort()).toEqual([ID2, ID].sort());
  });

  // The fail-open/fail-closed pair the denylist was explicitly tested for. A
  // malformed entry must deny NOBODY -- never everybody, and never a wildcard.
  it("drops anything that is not a device id rather than matching on it", () => {
    for (const junk of ["", "*", "  ", "all", "''", '""', "%", "..", "0x", "zzzz", "-"]) {
      expect(parseRevoked(junk).size).toBe(0);
    }
  });

  it("keeps a truncated id from becoming a prefix match", () => {
    expect(parseRevoked("2aeea6").size).toBe(0);
  });
});

describe("setRevoked", () => {
  it("adds and removes, and reports whether anything changed", async () => {
    let r = await setRevoked(E, ID, true);
    expect(r).toMatchObject({ ok: true, changed: true, revoked: [ID] });
    expect(await readRevoked(E)).toEqual(new Set([ID]));

    r = await setRevoked(E, ID, true);
    expect(r).toMatchObject({ ok: true, changed: false });

    r = await setRevoked(E, ID, false);
    expect(r).toMatchObject({ ok: true, changed: true, revoked: [] });
    expect((await readRevoked(E)).size).toBe(0);
  });

  it("refuses to write anything that is not a device id", async () => {
    for (const bad of ["", "*", "all", "2aeea6", "../x", "ZZZZZZZZ", "'; DROP TABLE"]) {
      const r = await setRevoked(E, bad, true);
      expect(r.ok).toBe(false);
      expect(await E.ENRICH_KV.get(REVOKED_KEY)).toBeNull();
    }
  });

  it("round-trips through the SAME format the CLI procedure writes", async () => {
    await setRevoked(E, ID, true);
    await setRevoked(E, ID2, true);
    const raw = (await E.ENRICH_KV.get(REVOKED_KEY)) ?? "";
    // Comments and blank lines survive a re-parse; the ids are one per line, so
    // a human editing the file with wrangler sees what they expect.
    expect(raw).toContain("#");
    expect(parseRevoked(raw)).toEqual(new Set([ID, ID2]));
    expect(raw.split("\n").filter((l) => l && !l.startsWith("#"))).toEqual([ID, ID2].sort());
  });

  // The property the whole feature turns on: no single operation can take the
  // fleet off the air.
  it("cannot deny the whole fleet, whatever is already in the entry", async () => {
    await E.ENRICH_KV.put(REVOKED_KEY, "*\nall\n\n#comment\n%\n");
    expect((await readRevoked(E)).size).toBe(0);
    const r = await setRevoked(E, ID, true);
    expect(r).toMatchObject({ ok: true });
    // The junk is gone AND only the one real id is listed.
    expect(await readRevoked(E)).toEqual(new Set([ID]));
  });

  it("drops pre-existing junk on the next write rather than propagating it", async () => {
    await E.ENRICH_KV.put(REVOKED_KEY, `${ID}\nnot-an-id\n${ID2}\n`);
    await setRevoked(E, ID, false);
    const raw = (await E.ENRICH_KV.get(REVOKED_KEY)) ?? "";
    expect(raw).not.toContain("not-an-id");
    expect(await readRevoked(E)).toEqual(new Set([ID2]));
  });
});
