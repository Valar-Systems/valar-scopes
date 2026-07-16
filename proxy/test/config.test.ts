import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { apiRequest, call } from "./helpers";

interface ConfigBody {
  v: number;
  rev: number;
  minFw: string;
  enrich: string;
  pollActiveMs: number;
  pollIdleMs: number;
  pollNightMs: number;
  idleAfterMs: number;
  staleFactor: number;
  upstreamState: string;
}

async function getConfig(model?: string): Promise<ConfigBody> {
  const res = await call(apiRequest("/v1/config", model ? { "X-Blip-Model": model } : {}));
  expect(res.status).toBe(200);
  return (await res.json()) as ConfigBody;
}

describe("/v1/config", () => {
  it("serves baked defaults for an unknown model", async () => {
    const c = await getConfig();
    expect(c.v).toBe(1);
    expect(c.rev).toBe(1);
    expect(c.minFw).toBe("0.0.0");
    expect(c.enrich).toBe("full");
    expect(c.pollActiveMs).toBe(5000);
    expect(c.pollIdleMs).toBe(15000);
    expect(c.pollNightMs).toBe(60000);
    expect(c.idleAfterMs).toBe(600000);
    expect(c.staleFactor).toBe(3);
    expect(c.upstreamState).toBe("ok");
  });

  it("resolves the S3 tier server-side (faster cadence, full enrichment)", async () => {
    const c = await getConfig("s3-146");
    expect(c.pollActiveMs).toBe(2000);
    expect(c.pollIdleMs).toBe(10000);
    expect(c.pollNightMs).toBe(45000);
    expect(c.idleAfterMs).toBe(900000);
    expect(c.enrich).toBe("full");
  });

  it("resolves the C3 tier server-side (default cadence, watchlist enrichment)", async () => {
    const c = await getConfig("c3-128");
    expect(c.pollActiveMs).toBe(5000);
    expect(c.enrich).toBe("watchlist");
  });

  it("applies KV fleet overrides: defaults < model", async () => {
    await env.ENRICH_KV.put(
      "cfg:fleet",
      JSON.stringify({
        rev: 9,
        minFw: "3.5.0",
        defaults: { pollActiveMs: 7000 },
        models: { "c3-128": { pollNightMs: 90000 } },
      }),
    );

    const c3 = await getConfig("c3-128");
    expect(c3.rev).toBe(9);
    expect(c3.minFw).toBe("3.5.0");
    expect(c3.pollActiveMs).toBe(7000); // KV defaults override baked values
    expect(c3.pollNightMs).toBe(90000); // KV per-model overrides KV defaults
    expect(c3.enrich).toBe("watchlist"); // untouched baked model default survives

    const other = await getConfig();
    expect(other.pollActiveMs).toBe(7000);
    expect(other.pollNightMs).toBe(60000);
  });

  it("survives malformed KV config by serving baked defaults", async () => {
    await env.ENRICH_KV.put("cfg:fleet", "this is not json");
    const c = await getConfig();
    expect(c.pollActiveMs).toBe(5000);
    expect(c.rev).toBe(1);
  });
});
