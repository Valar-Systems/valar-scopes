import { env } from "cloudflare:test";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { resetAccessCache } from "../src/access";
import worker from "../src/index";
import type { Env } from "../src/types";

// /device/<id> BEHIND Access: a real RS256 key signs a real assertion (the same
// approach as access.test.ts), analytics answers with no rows, and KV decides
// whether the id is enrolled. "Unknown id -> 404, not an empty page" is only
// meaningful for an operator who got past the front door.
const TEAM = "test.cloudflareaccess.com", AUD = "test-aud";
const KNOWN = "1111222233334444", UNKNOWN = "aaaabbbbccccdddd"; // allowlisted fakes
const E = env as unknown as Env;

let token = "";
const b64url = (b: ArrayBuffer | Uint8Array) => {
  const bytes = b instanceof Uint8Array ? b : new Uint8Array(b);
  let s = ""; for (const x of bytes) s += String.fromCharCode(x);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
};
const enc = (o: unknown) => b64url(new TextEncoder().encode(JSON.stringify(o)));

beforeEach(async () => {
  const pair = (await crypto.subtle.generateKey(
    { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" },
    true, ["sign", "verify"],
  )) as CryptoKeyPair;
  const jwk = { ...(await crypto.subtle.exportKey("jwk", pair.publicKey)), kid: "k1", alg: "RS256", use: "sig" };
  const body = `${enc({ alg: "RS256", kid: "k1" })}.${enc({ aud: [AUD], iss: `https://${TEAM}`, exp: Math.floor(Date.now() / 1000) + 600, email: "operator@example.com" })}`;
  token = `${body}.${b64url(await crypto.subtle.sign("RSASSA-PKCS1-v1_5", pair.privateKey, new TextEncoder().encode(body)))}`;
  resetAccessCache();
  vi.stubGlobal("fetch", vi.fn(async (input: RequestInfo | URL) => {
    const u = String(input instanceof Request ? input.url : input);
    if (u === `https://${TEAM}/cdn-cgi/access/certs`) return new Response(JSON.stringify({ keys: [jwk] }));
    if (u.includes("/analytics_engine/sql")) return new Response(JSON.stringify({ data: [], rows: 0 }));
    return new Response("unexpected fetch in test", { status: 500 });
  }));
  await E.ENRICH_KV.put(`enr:dev:${KNOWN}`, JSON.stringify({ id: KNOWN, firstAt: "2026-09-01T00:00:00Z", lastAt: "2026-09-01T00:00:00Z", enrollments: 1 }));
});
afterEach(() => vi.unstubAllGlobals());

const get = (path: string) =>
  worker.fetch(new Request(`https://fleet.example${path}`, { headers: { "Cf-Access-Jwt-Assertion": token } }), E);

describe("/device/<id> behind Access", () => {
  it("CONTROL: an enrolled id renders its page, 200, with its id linked", async () => {
    const res = await get(`/device/${KNOWN}`);
    expect(res.status).toBe(200);
    const html = await res.text();
    expect(html).toContain(`href="/device/${KNOWN}"`);
    expect(html).toContain("Boot history");
  });

  it("an id that is not enrolled and reported nothing is a 404, not an empty page", async () => {
    const res = await get(`/device/${UNKNOWN}`);
    expect(res.status).toBe(404);
    expect(await res.text()).toContain("No such device");
  });

  it("a malformed id is a 404 before any query is made", async () => {
    for (const bad of ["not-an-id", "ABC", "%3Cscript%3E", `${KNOWN}/extra`]) {
      expect((await get(`/device/${bad}`)).status, bad).toBe(404);
    }
  });
});
