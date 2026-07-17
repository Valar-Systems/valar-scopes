import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { apiRequest, call } from "./helpers";

// Central-Oregon fixture tile (the endpoint's origin story: RDM and BDN were
// missing from the baked table). One neighbouring tile proves the tile walk.
async function seedTiles(): Promise<void> {
  await env.ENRICH_KV.put(
    "apt:44:-122",
    JSON.stringify([
      [44.25, -121.15, "RDM", "M"],
      [44.09, -121.2, "BDN", "S"],
    ]),
  );
  await env.ENRICH_KV.put("apt:45:-123", JSON.stringify([[45.59, -122.6, "PDX", "L"]]));
}

describe("/v1/airports", () => {
  it("returns airports within radius, priority-sorted L > M > S", async () => {
    await seedTiles();
    // Centred between Redmond and Portland; 250 km reaches all three.
    const res = await call(apiRequest("/v1/airports?lat=44.9&lon=-121.9&r=250"));
    expect(res.status).toBe(200);
    const body = (await res.json()) as { v: number; a: [number, number, string, string][] };
    expect(body.v).toBe(1);
    expect(body.a.map((a) => a[2])).toEqual(["PDX", "RDM", "BDN"]);
  });

  it("filters by true distance, not tile membership", async () => {
    await seedTiles();
    // 30 km around Redmond: RDM and BDN are in range, PDX (~180 km) is not --
    // and BDN (10 km) sorts before RDM? No: RDM is M, BDN is S; kind wins.
    const res = await call(apiRequest("/v1/airports?lat=44.2&lon=-121.2&r=30"));
    const body = (await res.json()) as { a: [number, number, string, string][] };
    expect(body.a.map((a) => a[2])).toEqual(["RDM", "BDN"]);
  });

  it("serves empty (not an error) where there are no tiles", async () => {
    const res = await call(apiRequest("/v1/airports?lat=0.5&lon=0.5&r=50"));
    expect(res.status).toBe(200);
    expect(((await res.json()) as { a: unknown[] }).a).toEqual([]);
  });

  it("rejects malformed positions and requires auth", async () => {
    expect((await call(apiRequest("/v1/airports?lat=999&lon=0"))).status).toBe(400);
    expect((await call(apiRequest("/v1/airports?lat=abc&lon=0"))).status).toBe(400);
    const unauthed = new Request("https://proxy.test/v1/airports?lat=44&lon=-121");
    expect((await call(unauthed)).status).toBe(401);
  });
});
