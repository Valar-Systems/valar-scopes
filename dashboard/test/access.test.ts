import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { resetAccessCache, verifyAccess } from "../src/access";
import type { Env } from "../src/types";

// A real RS256 keypair, generated per run, so these tests exercise the actual
// signature path rather than a stub. Anything that only "looks" verified would
// pass a mock and fail in production, which is the failure mode that matters on
// an admin surface.
let priv: CryptoKey;
let jwk: JsonWebKey & { kid: string };

const TEAM = "test.cloudflareaccess.com";
const AUD = "test-aud";

const b64url = (b: ArrayBuffer | Uint8Array): string => {
  const bytes = b instanceof Uint8Array ? b : new Uint8Array(b);
  let s = "";
  for (const byte of bytes) s += String.fromCharCode(byte);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
};
const enc = (o: unknown) => b64url(new TextEncoder().encode(JSON.stringify(o)));

async function sign(
  claims: Record<string, unknown>,
  header: Record<string, unknown> = { alg: "RS256", kid: "test-kid" },
): Promise<string> {
  const body = `${enc(header)}.${enc(claims)}`;
  const sig = await crypto.subtle.sign(
    "RSASSA-PKCS1-v1_5",
    priv,
    new TextEncoder().encode(body),
  );
  return `${body}.${b64url(sig)}`;
}

const good = () => ({
  aud: [AUD],
  iss: `https://${TEAM}`,
  exp: Math.floor(Date.now() / 1000) + 600,
  email: "operator@example.com",
  sub: "user-1",
});

const envWith = (over: Partial<Env> = {}): Env =>
  ({ ACCESS_TEAM_DOMAIN: TEAM, ACCESS_AUD: AUD, ...over }) as Env;

const req = (token: string) =>
  new Request("https://fleet.example/", { headers: { "Cf-Access-Jwt-Assertion": token } });

beforeEach(async () => {
  const pair = (await crypto.subtle.generateKey(
    { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" },
    true,
    ["sign", "verify"],
  )) as CryptoKeyPair;
  priv = pair.privateKey;
  const pub = (await crypto.subtle.exportKey("jwk", pair.publicKey)) as JsonWebKey;
  jwk = { ...pub, kid: "test-kid", alg: "RS256", use: "sig" } as JsonWebKey & { kid: string };
  resetAccessCache();
  vi.stubGlobal(
    "fetch",
    vi.fn(async (input: RequestInfo | URL) => {
      const u = String(input instanceof Request ? input.url : input);
      if (u === `https://${TEAM}/cdn-cgi/access/certs`) {
        return new Response(JSON.stringify({ keys: [jwk] }), {
          headers: { "Content-Type": "application/json" },
        });
      }
      return new Response("no", { status: 404 });
    }),
  );
});

afterEach(() => vi.unstubAllGlobals());

describe("verifyAccess", () => {
  it("accepts a token Access actually signed", async () => {
    const who = await verifyAccess(req(await sign(good())), envWith());
    expect(who?.email).toBe("operator@example.com");
  });

  it("accepts the cookie Access sets, not just the header", async () => {
    const token = await sign(good());
    const r = new Request("https://fleet.example/", {
      headers: { Cookie: `foo=bar; CF_Authorization=${token}; baz=1` },
    });
    expect(await verifyAccess(r, envWith())).not.toBeNull();
  });

  it("refuses when there is no token at all", async () => {
    expect(await verifyAccess(new Request("https://fleet.example/"), envWith())).toBeNull();
  });

  // The reason the Worker checks the JWT rather than trusting the hostname.
  it("refuses a token signed by a different key", async () => {
    const other = (await crypto.subtle.generateKey(
      { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" },
      true,
      ["sign", "verify"],
    )) as CryptoKeyPair;
    const body = `${enc({ alg: "RS256", kid: "test-kid" })}.${enc(good())}`;
    const sig = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", other.privateKey, new TextEncoder().encode(body));
    expect(await verifyAccess(req(`${body}.${b64url(sig)}`), envWith())).toBeNull();
  });

  it("refuses a tampered payload", async () => {
    const token = await sign(good());
    const [h, , s] = token.split(".") as [string, string, string];
    const forged = `${h}.${enc({ ...good(), email: "attacker@example.com" })}.${s}`;
    expect(await verifyAccess(req(forged), envWith())).toBeNull();
  });

  // The classic JWT holes. Both are refused by pinning alg before any key work.
  it("refuses alg=none", async () => {
    const token = `${enc({ alg: "none", kid: "test-kid" })}.${enc(good())}.`;
    expect(await verifyAccess(req(token), envWith())).toBeNull();
  });

  it("refuses an algorithm swap to HS256", async () => {
    const token = await sign(good(), { alg: "HS256", kid: "test-kid" });
    expect(await verifyAccess(req(token), envWith())).toBeNull();
  });

  it("refuses a token minted for a DIFFERENT Access application", async () => {
    expect(await verifyAccess(req(await sign({ ...good(), aud: ["other-app"] })), envWith())).toBeNull();
  });

  it("refuses a token from a different Access team", async () => {
    expect(
      await verifyAccess(req(await sign({ ...good(), iss: "https://evil.cloudflareaccess.com" })), envWith()),
    ).toBeNull();
  });

  it("refuses an expired token", async () => {
    expect(
      await verifyAccess(req(await sign({ ...good(), exp: Math.floor(Date.now() / 1000) - 5 })), envWith()),
    ).toBeNull();
  });

  it("refuses a token with no expiry", async () => {
    const { exp, ...noExp } = good();
    expect(await verifyAccess(req(await sign(noExp)), envWith())).toBeNull();
  });

  it("refuses an unknown signing key id", async () => {
    expect(
      await verifyAccess(req(await sign(good(), { alg: "RS256", kid: "someone-elses-kid" })), envWith()),
    ).toBeNull();
  });

  // THE deployment footgun: a Worker shipped before its Access config is set
  // must serve nobody, not everybody.
  it("refuses everything when Access is not configured", async () => {
    const token = await sign(good());
    expect(await verifyAccess(req(token), envWith({ ACCESS_AUD: "" }))).toBeNull();
    expect(await verifyAccess(req(token), envWith({ ACCESS_TEAM_DOMAIN: "" }))).toBeNull();
    expect(await verifyAccess(req(token), {} as Env)).toBeNull();
  });

  it("refuses when the key service is unreachable, rather than letting it through", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => new Response("down", { status: 503 })));
    expect(await verifyAccess(req(await sign(good())), envWith())).toBeNull();
  });

  it("honours the optional email allowlist", async () => {
    const token = await sign(good());
    expect(await verifyAccess(req(token), envWith({ ACCESS_ALLOWED_EMAILS: "someone@else.com" }))).toBeNull();
    expect(
      await verifyAccess(req(token), envWith({ ACCESS_ALLOWED_EMAILS: " OPERATOR@example.com , x@y.z" })),
    ).not.toBeNull();
  });

  it("refuses garbage that is not a JWT at all", async () => {
    for (const junk of ["", "abc", "a.b", "a.b.c.d", "....", "%%%"]) {
      expect(await verifyAccess(req(junk), envWith())).toBeNull();
    }
  });
});
