import { describe, expect, it } from "vitest";
import type { RateLimit } from "../src/types";
import { apiRequest, call } from "./helpers";

const deny: RateLimit = { limit: async () => ({ success: false }) };
const allow: RateLimit = { limit: async () => ({ success: true }) };

describe("auth", () => {
  it("rejects a missing or wrong key with 401", async () => {
    const noKey = new Request("https://proxy.test/v1/config");
    expect((await call(noKey)).status).toBe(401);

    const wrongKey = apiRequest("/v1/config", { "X-Blip-Key": "wrong" });
    expect((await call(wrongKey)).status).toBe(401);
  });

  it("accepts any key from the comma-separated rotation list", async () => {
    const res = await call(apiRequest("/v1/config"), { BLIP_KEYS: "old-key, test-key" });
    expect(res.status).toBe(200);
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
