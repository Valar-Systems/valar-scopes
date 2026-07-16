import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call, hexBody } from "./helpers";

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});

beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

const LOL = "https://api.adsb.lol";
const JPEG = new Uint8Array([0xff, 0xd8, 0xff, 0xe0, 1, 2, 3, 4, 0xff, 0xd9]); // tiny fake baseline JPEG

describe("GET /v1/photo/<key>", () => {
  it("serves the immutable blob with a JPEG content-type and a year-long immutable cache", async () => {
    await env.ENRICH_KV.put("photo:A320-deadbeef", JPEG);

    const res = await call(apiRequest("/v1/photo/photo:A320-deadbeef"));
    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe("image/jpeg");
    expect(res.headers.get("Cache-Control")).toBe("public, max-age=31536000, immutable");
    expect(new Uint8Array(await res.arrayBuffer())).toEqual(JPEG);
  });

  it("requires the device key", async () => {
    // Bare request: no X-Blip-Key header at all.
    const res = await call(new Request("https://proxy.test/v1/photo/photo:A320-deadbeef"));
    expect(res.status).toBe(401);
  });

  it("404s an absent-but-valid key (no `p` was ever emitted for it)", async () => {
    const res = await call(apiRequest("/v1/photo/photo:B738-00000000"));
    expect(res.status).toBe(404);
  });

  it("400s a malformed key without touching KV", async () => {
    expect((await call(apiRequest("/v1/photo/not-a-key"))).status).toBe(400);
    expect((await call(apiRequest("/v1/photo/photo:A320-zzzz"))).status).toBe(400);
  });
});

describe("GET /credits", () => {
  it("renders the manifest as public HTML with no key required", async () => {
    await env.ENRICH_KV.put(
      "photo:manifest",
      JSON.stringify([
        {
          target: "A343",
          kind: "type",
          source: "https://commons.wikimedia.org/wiki/File:HB-JMB.jpg",
          author: "Jane Spotter",
          credit: "Airbus A340 by Jane Spotter",
          license: "CC-BY",
          layer: "auto",
          autoPicked: false,
        },
      ]),
    );

    const res = await call(new Request("https://proxy.test/credits"));
    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe("text/html; charset=utf-8");
    const html = await res.text();
    expect(html).toContain("Airbus A340 by Jane Spotter");
    expect(html).toContain("creativecommons.org/licenses/by/4.0/");
  });

  it("serves an empty-but-valid page when nothing has been ingested", async () => {
    const res = await call(new Request("https://proxy.test/credits"));
    expect(res.status).toBe(200);
    expect(await res.text()).toContain("Blipscope photo credits");
  });
});

describe("/v1/enrich photo join", () => {
  it("adds p + pk:type when only a type pointer exists", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/4b1817" })
      .reply(200, hexBody([{ hex: "4b1817", r: "HB-JMB", t: "A343" }]));
    await env.ENRICH_KV.put("pptr:t:A343", "photo:A343-aabbccdd");

    const res = await call(apiRequest("/v1/enrich/4b1817"));
    const body = (await res.json()) as { p?: string; pk?: string };
    expect(body.p).toBe("/v1/photo/photo:A343-aabbccdd");
    expect(body.pk).toBe("type");
  });

  it("prefers a per-hex override over the type pointer (hex beats type, uncaptioned)", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/abc123" })
      .reply(200, hexBody([{ hex: "abc123", r: "N1", t: "A343" }]));
    await env.ENRICH_KV.put("pptr:t:A343", "photo:A343-aabbccdd");
    await env.ENRICH_KV.put("pptr:h:abc123", "photo:abc123-11223344");

    const res = await call(apiRequest("/v1/enrich/abc123"));
    const body = (await res.json()) as { p?: string; pk?: string };
    expect(body.p).toBe("/v1/photo/photo:abc123-11223344");
    expect(body.pk).toBe("hex");
  });

  it("omits p/pk entirely when the library has no image for the aircraft", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/def456" })
      .reply(200, hexBody([{ hex: "def456", r: "N2", t: "B738" }]));

    const res = await call(apiRequest("/v1/enrich/def456"));
    const body = (await res.json()) as Record<string, unknown>;
    expect(body).not.toHaveProperty("p");
    expect(body).not.toHaveProperty("pk");
  });
});
