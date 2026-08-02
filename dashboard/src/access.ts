import type { Env } from "./types";

// Cloudflare Access verification.
//
// WHY THE WORKER VERIFIES THE JWT ITSELF, when Access is already in front.
// Access protects a HOSTNAME. It does not protect the Worker. If the Worker is
// ever reachable by any other route -- a workers.dev subdomain left enabled, a
// second custom domain, a preview URL, an Access policy edited to "Bypass" by
// mistake -- then an unverified request arrives at code that can revoke devices
// and read the fleet. Checking the assertion here means the only way in is a
// token Access actually signed, whatever the routing looks like. This is the
// standard Access footgun and it is worth the ~60 lines.
//
// Everything below fails CLOSED. That is the opposite of revocation.ts's
// deliberate fail-open, and for the opposite reason: there, an infrastructure
// blip must not take the fleet off the air; here, an infrastructure blip must
// not hand out an admin surface. When in doubt, serve nobody.

interface Jwk {
  kid: string;
  kty: string;
  alg?: string;
  use?: string;
  n: string;
  e: string;
}

interface CachedKeys {
  keys: Jwk[];
  at: number;
}

// Access rotates signing keys; an hour is well inside the window and keeps this
// off the hot path of every page view.
const JWKS_TTL_MS = 60 * 60 * 1000;
let jwksCache: CachedKeys | null = null;

export function resetAccessCache(): void {
  jwksCache = null;
}

function b64urlToBytes(s: string): Uint8Array {
  const pad = s.length % 4 === 0 ? "" : "=".repeat(4 - (s.length % 4));
  const bin = atob(s.replace(/-/g, "+").replace(/_/g, "/") + pad);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

function b64urlToString(s: string): string {
  return new TextDecoder().decode(b64urlToBytes(s));
}

export interface AccessIdentity {
  email: string;
  sub: string;
}

async function fetchJwks(teamDomain: string): Promise<Jwk[]> {
  const now = Date.now();
  if (jwksCache && now - jwksCache.at < JWKS_TTL_MS) return jwksCache.keys;
  const res = await fetch(`https://${teamDomain}/cdn-cgi/access/certs`, {
    cf: { cacheTtl: 3600, cacheEverything: true },
  });
  if (!res.ok) throw new Error(`jwks fetch failed: ${res.status}`);
  const body = (await res.json()) as { keys?: Jwk[] };
  const keys = body.keys ?? [];
  if (keys.length === 0) throw new Error("jwks empty");
  jwksCache = { keys, at: now };
  return keys;
}

// Verify the Access assertion. Returns the identity, or null for every failure
// mode -- an absent token, a bad signature, the wrong audience, an expired
// token, or a misconfigured Worker.
export async function verifyAccess(request: Request, env: Env): Promise<AccessIdentity | null> {
  // A missing config is a REFUSAL, not a bypass. This is the line that decides
  // whether a half-deployed dashboard is open to the internet.
  const teamDomain = (env.ACCESS_TEAM_DOMAIN ?? "").trim();
  const aud = (env.ACCESS_AUD ?? "").trim();
  if (!teamDomain || !aud) return null;

  const token =
    request.headers.get("Cf-Access-Jwt-Assertion") ??
    (request.headers.get("Cookie") ?? "").match(/(?:^|;\s*)CF_Authorization=([^;]+)/)?.[1] ??
    "";
  if (!token) return null;

  const parts = token.split(".");
  if (parts.length !== 3) return null;
  const [headerB64, payloadB64, sigB64] = parts as [string, string, string];

  try {
    const header = JSON.parse(b64urlToString(headerB64)) as { kid?: string; alg?: string };
    // Pin the algorithm. Accepting whatever the token names is the classic JWT
    // hole -- "alg":"none" or an HMAC forged with the public key as the secret.
    if (header.alg !== "RS256" || !header.kid) return null;

    const keys = await fetchJwks(teamDomain);
    const jwk = keys.find((k) => k.kid === header.kid);
    if (!jwk) return null;

    const key = await crypto.subtle.importKey(
      "jwk",
      { kty: jwk.kty, n: jwk.n, e: jwk.e, alg: "RS256", ext: true },
      { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
      false,
      ["verify"],
    );
    const ok = await crypto.subtle.verify(
      "RSASSA-PKCS1-v1_5",
      key,
      b64urlToBytes(sigB64),
      new TextEncoder().encode(`${headerB64}.${payloadB64}`),
    );
    if (!ok) return null;

    const claims = JSON.parse(b64urlToString(payloadB64)) as {
      aud?: string | string[];
      iss?: string;
      exp?: number;
      nbf?: number;
      email?: string;
      sub?: string;
    };

    // Audience: this token must have been minted for THIS application. Without
    // it, a valid token for any other app on the same Access team gets in.
    const auds = Array.isArray(claims.aud) ? claims.aud : claims.aud ? [claims.aud] : [];
    if (!auds.includes(aud)) return null;
    if (claims.iss !== `https://${teamDomain}`) return null;

    const nowSec = Math.floor(Date.now() / 1000);
    if (typeof claims.exp !== "number" || claims.exp <= nowSec) return null;
    if (typeof claims.nbf === "number" && claims.nbf > nowSec + 60) return null;

    // Optional second gate. The Access policy is the real one; this is a
    // belt-and-braces list for the case where the policy is widened by accident.
    const allow = (env.ACCESS_ALLOWED_EMAILS ?? "")
      .split(",")
      .map((e) => e.trim().toLowerCase())
      .filter(Boolean);
    const email = (claims.email ?? "").toLowerCase();
    if (allow.length > 0 && !allow.includes(email)) return null;

    return { email, sub: claims.sub ?? "" };
  } catch {
    return null;
  }
}
