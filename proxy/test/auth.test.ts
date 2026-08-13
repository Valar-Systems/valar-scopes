import { describe, expect, it } from "vitest";
import type { RateLimit } from "../src/types";
import { AUTH_HEADERS, TEST_KEY, apiRequest, call } from "./helpers";

const deny: RateLimit = { limit: async () => ({ success: false }) };
const allow: RateLimit = { limit: async () => ({ success: true }) };

describe("auth", () => {
  it("rejects a missing or wrong key with 401", async () => {
    const noKey = new Request("https://proxy.test/v1/config");
    expect((await call(noKey)).status).toBe(401);

    const wrongKey = apiRequest("/v1/config", { "X-Blip-Key": "wrong" });
    expect((await call(wrongKey)).status).toBe(401);
  });

  // The shared BLIP_KEYS list was removed 2026-08-13. These pin the ways a
  // caller might still expect it to work, because "no code path exists" is not
  // something the other tests can show -- they all authenticate correctly and
  // would pass just the same if a fallback were quietly reintroduced.
  it("refuses a key with NO device id, even a well-formed one", async () => {
    // Deliberately NOT AUTH_HEADERS: the absence of X-Blip-Device is the whole
    // subject of this test, so the pair must be taken apart by hand here.
    const noDevice = new Request("https://proxy.test/v1/config", {
      headers: { "X-Blip-Key": TEST_KEY },
    });
    expect((await call(noDevice)).status).toBe(401);
  });

  it("ignores a BLIP_KEYS binding entirely if one is ever set again", async () => {
    // Belt and braces: the field is gone from Env, so this is cast in
    // deliberately. If someone re-adds the binding and a fallback with it, this
    // fails -- which is the only way that regression announces itself.
    const sharedOnly = new Request("https://proxy.test/v1/config", {
      headers: { "X-Blip-Key": "old-shared-key" },
    });
    const res = await call(sharedOnly, { BLIP_KEYS: "old-shared-key, another" } as never);
    expect(res.status).toBe(401);
  });

  it("refuses a valid key presented with the WRONG device id", async () => {
    const mismatched = apiRequest("/v1/config", { "X-Blip-Device": "ffffffffffffffff" });
    expect((await call(mismatched)).status).toBe(401);
  });

  // Negative control for all three: the correct pair must still be accepted, or
  // "everything 401s" would satisfy every assertion above.
  it("accepts the correct device id + derived key pair", async () => {
    expect((await call(apiRequest("/v1/config"))).status).toBe(200);
  });

  it("leaves /healthz public", async () => {
    const res = await call(new Request("https://proxy.test/healthz"));
    expect(res.status).toBe(200);
    const body = (await res.json()) as { ok: boolean; upstreams: { id: string; enabled: boolean }[] };
    expect(body.ok).toBe(true);
    expect(body.upstreams.find((u) => u.id === "adsb_lol")?.enabled).toBe(true);
    expect(body.upstreams.find((u) => u.id === "adsb_fi")?.enabled).toBe(false);
  });
});

describe("rate limiting", () => {
  it("throttles by IP before auth, with Retry-After", async () => {
    const res = await call(apiRequest("/v1/config"), { RL_IP: deny });
    expect(res.status).toBe(429);
    expect(res.headers.get("Retry-After")).toBe("10");
  });

  it("throttles by key after auth", async () => {
    const res = await call(apiRequest("/v1/config"), { RL_IP: allow, RL_KEY: deny });
    expect(res.status).toBe(429);
  });

  it("passes when the buckets allow", async () => {
    const res = await call(apiRequest("/v1/config"), { RL_IP: allow, RL_KEY: allow });
    expect(res.status).toBe(200);
  });
});

describe("routing", () => {
  it("404s unknown paths and 405s non-GET", async () => {
    expect((await call(apiRequest("/v1/nope"))).status).toBe(404);
    expect((await call(apiRequest("/other"))).status).toBe(404);
    const post = new Request("https://proxy.test/v1/blips", {
      method: "POST",
      headers: { "X-Blip-Key": "test-key" },
    });
    expect((await call(post)).status).toBe(405);
  });
});
