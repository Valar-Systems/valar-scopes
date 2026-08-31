import type { Env } from "./types";
import { deriveDeviceKey } from "./deviceauth";
import { isRevoked } from "./revocation";

/* ===========================================================================
 * DEVICE ENROLLMENT — how a board we did not flash gets its per-device key.
 *
 * See docs/device-enrollment.md for the decision record. The load-bearing parts,
 * restated here because this file is where they are enforced:
 *
 * 1. THERE IS NO OPEN ENROLLMENT ENDPOINT, UNDER ANY FRAMING. The key is a pure
 *    function of the id — HMAC(DEVICE_KEY_SECRET, deviceId) — so an endpoint
 *    that mints without proof of a human returns a valid key for ANY id,
 *    including ids nobody owns. That is a key ORACLE, not a mint. Rate limiting
 *    does not touch it (an abuser needs one key, once) and revocation dies with
 *    it, because ids would be attacker-chosen: we revoke, they enroll a new one.
 *
 * 2. THE GATE IS THE SOLVE, NOT THE TRANSPORT. A verified Turnstile token is the
 *    ONLY thing standing between a request and a key. It is deliberately NOT a
 *    boundary made of where the request came from: the enrol page is a normal
 *    HTTPS page and a customer whose LAN cannot reach Cloudflare is expected to
 *    solve on a phone instead. Moving the browser does not weaken this, and
 *    pretending the popup was a security boundary would be a lie that made the
 *    fallback look like a hole.
 *
 * 3. WHAT IS BEING PROTECTED IS A RELATIONSHIP, NOT A QUOTA. Sponsored upstream
 *    access came with leeway rather than a hard ceiling, so a farm of keys would
 *    not exhaust an allowance — it would make us the source of the traffic our
 *    upstream sees. There is therefore no "acceptable" number of leaked keys to
 *    tune a threshold against, and two things follow: server-side siteverify
 *    ALWAYS (a client-only check is not a weaker version of this design, it is a
 *    different design that does not work), and enrollment volume is LOGGED so
 *    abuse is observed rather than inferred. With no ceiling to hit, nothing
 *    else will ever tell us.
 * ======================================================================== */

/** Cloudflare's token verification endpoint. The only authority on a solve. */
const SITEVERIFY = "https://challenges.cloudflare.com/turnstile/v0/siteverify";

/** Device ids are the salted MAC hash the firmware publishes (DeviceIdentity.h). */
const DEVICE_ID_RE = /^[0-9a-f]{8,32}$/;

/** KV keys. `enr:dev:<id>` is the ledger row; `enr:day:<yyyy-mm-dd>` the counter. */
const ledgerKey = (id: string): string => `enr:dev:${id}`;
const dayKey = (day: string): string => `enr:day:${day}`;

export interface EnrollLedgerRow {
  id: string;
  firstAt: string;
  lastAt: string;
  /** How many times this id has enrolled. >1 is a reflash, not an attack. */
  enrollments: number;
  /** Siteverify's view of where the solve happened. Recorded, never enforced. */
  hostnames: string[];
  /** Coarse origin of the most recent solve, for abuse triage. */
  lastCountry?: string;
  lastAsn?: string;
}

interface SiteverifyResponse {
  success: boolean;
  hostname?: string;
  "error-codes"?: string[];
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      // The enrol PAGE is same-origin with this endpoint, so no CORS grant is
      // needed and none is given. A cross-origin caller cannot read the reply.
      "cache-control": "no-store",
    },
  });
}

/**
 * Verify the Turnstile token with Cloudflare. Server-side, always.
 *
 * Returns null on any failure, deliberately without distinguishing "no token"
 * from "bad token" to the caller — the client has no legitimate use for the
 * difference and the error codes are for our logs.
 */
async function verifySolve(
  env: Env,
  token: string,
  remoteIp: string | null,
): Promise<SiteverifyResponse | null> {
  const secret = env.TURNSTILE_SECRET_KEY;
  if (!secret || !token) return null;

  const form = new FormData();
  form.append("secret", secret);
  form.append("response", token);
  if (remoteIp) form.append("remoteip", remoteIp);

  try {
    const res = await fetch(SITEVERIFY, { method: "POST", body: form });
    if (!res.ok) return null;
    const data = (await res.json()) as SiteverifyResponse;
    return data.success ? data : null;
  } catch {
    // A siteverify outage must FAIL CLOSED. Minting on an unreachable verifier
    // would turn every Cloudflare blip into the open endpoint this design
    // exists to avoid, and an unverified key is indistinguishable from one an
    // attacker asked for.
    return null;
  }
}

/** Best-effort day counter, so enrollment volume is visible without a query. */
async function bumpDayCounter(env: Env, day: string): Promise<number> {
  const prev = Number((await env.ENRICH_KV.get(dayKey(day))) ?? "0");
  const next = prev + 1;
  // 40-day TTL: long enough to see a month-shaped pattern, short enough that the
  // namespace does not grow without bound.
  await env.ENRICH_KV.put(dayKey(day), String(next), { expirationTtl: 60 * 60 * 24 * 40 });
  return next;
}

/**
 * POST /blipscope/enroll — mint (or re-mint) a device key for a solved challenge.
 *
 * Body: { id: "<deviceId>", token: "<cf-turnstile-response>" }
 * 200:  { status: "enrolled" | "already_enrolled", id, key, enrollments }
 * 400:  { error: "bad_request" }         malformed id, or no token at all
 * 403:  { error: "unverified" }          the solve did not verify
 * 403:  { error: "revoked" }             this id is revoked; no key is issued
 * 503:  { error: "not_configured" }      secrets missing on this deployment
 */
export async function handleEnroll(request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") return json({ error: "method_not_allowed" }, 405);

  const secret = env.DEVICE_KEY_SECRET;
  if (!secret || !env.TURNSTILE_SECRET_KEY) return json({ error: "not_configured" }, 503);

  let body: { id?: unknown; token?: unknown };
  try {
    body = (await request.json()) as typeof body;
  } catch {
    return json({ error: "bad_request" }, 400);
  }

  const id = String(body.id ?? "").trim().toLowerCase();
  const token = String(body.token ?? "").trim();
  if (!DEVICE_ID_RE.test(id)) return json({ error: "bad_request" }, 400);
  if (!token) return json({ error: "bad_request" }, 400);

  const remoteIp = request.headers.get("CF-Connecting-IP");
  const solve = await verifySolve(env, token, remoteIp);
  if (!solve) return json({ error: "unverified" }, 403);

  // REVOCATION IS CHECKED BEFORE MINTING, AND A REFLASH CANNOT SHED IT.
  //
  // The id is derived on-device from the efuse MAC, so it survives a full erase
  // and every edition switch — which is exactly why "reflash to escape
  // revocation" is the first thing someone would try. Re-enrolling a revoked id
  // returns no key at all. (Even if it did, the key would be the same one the
  // auth path already refuses; this is defence in depth, not the only barrier.)
  if (await isRevoked(env, id)) return json({ error: "revoked" }, 403);

  const key = await deriveDeviceKey(secret, id);
  const now = new Date();
  const nowIso = now.toISOString();
  const day = nowIso.slice(0, 10);

  const prev = await env.ENRICH_KV.get<EnrollLedgerRow>(ledgerKey(id), "json");

  // IDEMPOTENT BY CONSTRUCTION, AND SAID SO OUT LOUD.
  //
  // THE ORDER ABOVE IS LOAD-BEARING, and was questioned on 2026-08-31 while a
  // rotated board sat on this page reading "Already verified". `key` is derived
  // from the CURRENT secret BEFORE this ledger read, and is returned on BOTH
  // branches -- so an already_enrolled response carries a FRESHLY DERIVED key,
  // which is exactly what a device needs after a DEVICE_KEY_SECRET rotation.
  //
  // This is not a short-circuit and must not become one. If the ledger read ever
  // moves above the derivation, or `key` is dropped from the repeat branch as
  // "redundant", a rotated device would be told it is fine while still 401ing --
  // a screen reporting a conclusion drawn from the wrong signal. There is a test.
  //
  // The key is a pure function of the id, so a second enrollment cannot produce
  // a different one — re-enrolling is a re-derivation, not a re-issue. A
  // customer who reflashed (Blipscope -> Missileer -> Blipscope, or a full
  // erase) must not hit a wall, so this is a plain 200 with a message rather
  // than a 409. The ledger LOGS the repeat instead of blocking it.
  const row: EnrollLedgerRow = {
    id,
    firstAt: prev?.firstAt ?? nowIso,
    lastAt: nowIso,
    enrollments: (prev?.enrollments ?? 0) + 1,
    hostnames: [...new Set([...(prev?.hostnames ?? []), solve.hostname ?? ""].filter(Boolean))].slice(0, 10),
  };
  const country = request.headers.get("CF-IPCountry");
  const asn = (request.cf as { asn?: number } | undefined)?.asn;
  if (country) row.lastCountry = country;
  if (asn) row.lastAsn = String(asn);

  await env.ENRICH_KV.put(ledgerKey(id), JSON.stringify(row));
  const dayTotal = await bumpDayCounter(env, day);

  // VOLUME IS THE DETECTION, so it is logged on every mint rather than derived
  // later from a query nobody runs. With leeway instead of a ceiling upstream,
  // this line is the only thing that will ever say "something is wrong".
  console.log(
    `enroll ${row.enrollments === 1 ? "new" : "repeat"} id=${id} n=${row.enrollments} `
    + `day_total=${dayTotal} host=${solve.hostname ?? "?"} cc=${country ?? "?"} asn=${asn ?? "?"}`,
  );

  return json({
    status: row.enrollments === 1 ? "enrolled" : "already_enrolled",
    id,
    key,
    enrollments: row.enrollments,
  });
}

/** Today's enrollment count, for the operator. Not a public figure. */
export async function enrollmentsToday(env: Env): Promise<number> {
  const day = new Date().toISOString().slice(0, 10);
  return Number((await env.ENRICH_KV.get(dayKey(day))) ?? "0");
}
