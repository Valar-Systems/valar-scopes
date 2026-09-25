import { deriveDeviceKey } from "./deviceauth";
import { LEADERBOARD_SALT } from "./devicesalt.generated";
import { refusesFakeId } from "./fakeids";
import { isRevoked } from "./revocation";
import type { Env } from "./types";

// Worker-minted provisioning keys. Spec: docs/provisioning-mint.md.
//
// WHY THE WORKER MINTS. Since 2026-08-31 no person holds DEVICE_KEY_SECRET -- the
// #266 split generated it inside a session and deleted the only file the same day.
// The bench therefore cannot derive a factory board's key, and rotating to a value
// somebody holds would break every key in the fleet AND recreate the copy that went
// missing. The Worker already holds the secret, so it mints, for an authenticated
// bench, and the secret stays Worker-only by design.

export const PROVISION_DAILY_CAP = 120;
const OPERATOR = "bench";
const MAC_RE = /^([0-9a-f]{2}:){5}[0-9a-f]{2}$/;
const dayKey = (day: string): string => `prov:day:${day}`;

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  });
}

const hex = (buf: ArrayBuffer): string => [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, "0")).join("");

// CONSTANT TIME, AND LENGTH-BLIND. Both sides are SHA-256'd first, so the compare
// always walks 64 characters whatever was presented: an early exit on a length
// mismatch would tell a caller how long the token is.
async function tokenMatches(presented: string, expected: string | undefined): Promise<boolean> {
  if (!expected) return false; // unset secret = every request refused (fails closed)
  const enc = new TextEncoder();
  const [a, b] = await Promise.all([
    crypto.subtle.digest("SHA-256", enc.encode(presented)),
    crypto.subtle.digest("SHA-256", enc.encode(expected)),
  ]);
  const x = new Uint8Array(a), y = new Uint8Array(b);
  let diff = 0;
  for (let i = 0; i < x.length; i++) diff |= (x[i] as number) ^ (y[i] as number);
  return diff === 0;
}

/** "90:70:69:31:E2:08" / "90-70-..." / "907069..." -> "90:70:69:31:e2:08", or null. */
export function normalizeMac(raw: unknown): string | null {
  const h = String(raw ?? "").toLowerCase().replace(/[^0-9a-f]/g, "");
  if (h.length !== 12) return null;
  const mac = h.match(/../g)!.join(":");
  if (!MAC_RE.test(mac)) return null;
  const first = parseInt(h.slice(0, 2), 16);
  // A factory MAC is unicast; all-zero and broadcast are never a board.
  if (first & 1 || h === "000000000000" || h === "ffffffffffff") return null;
  return mac;
}

/** Byte-for-byte what DeviceIdentity::LeaderboardId() and provision-device.py compute. */
export async function deviceIdFromMac(mac: string): Promise<string> {
  const raw = new Uint8Array(mac.split(":").map((o) => parseInt(o, 16)));
  const salt = new TextEncoder().encode(LEADERBOARD_SALT);
  const buf = new Uint8Array(raw.length + salt.length);
  buf.set(raw, 0);
  buf.set(salt, raw.length);
  return hex(await crypto.subtle.digest("SHA-256", buf)).slice(0, 16);
}

// KEYED, because a plain hash of a MAC is reversible by enumerating the vendor's
// address block. 32 hex, not 16: a 16-hex value has the device-id shape the repo's
// guard (scripts/check_device_ids.py) refuses, and a log field must never be
// mistakable for an id.
async function macHash(secret: string, mac: string): Promise<string> {
  const enc = new TextEncoder();
  const k = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  return hex(await crypto.subtle.sign("HMAC", k, enc.encode(`mac:${mac}`))).slice(0, 32);
}

function record(env: Env, evt: "provision_mint" | "provision_refused", f: Record<string, unknown>): void {
  // The token is never passed in here, so it cannot reach a log line.
  console.log(JSON.stringify({ evt, operator: OPERATOR, ...f }));
  if (evt !== "provision_mint") return;
  try {
    env.METRICS?.writeDataPoint({
      blobs: ["provision", String(f.mac_hash ?? ""), OPERATOR, String(f.device ?? "")],
      doubles: [Number(f.mints_today ?? 0)],
      indexes: ["provision"],
    });
  } catch {
    // never let telemetry break the mint
  }
}

/**
 * POST /blipscope/provision -- mint a factory board's key from its MAC.
 *
 * Header: X-Provision-Token: <PROVISION_TOKEN>
 * Body:   { mac: "aa:bb:cc:dd:ee:ff" }
 * 200:    { deviceId, key, mintsToday }
 * 403:    { error: "forbidden" }        missing/wrong token, or PROVISION_TOKEN unset
 * 503:    { error: "not_configured" }   DEVICE_KEY_SECRET unset
 * 400:    { error: "bad_mac" }
 * 403:    { error: "fake_device_id" } | { error: "revoked" }
 * 429:    { error: "daily_cap", cap, resetsAt }
 */
export async function handleProvision(
  request: Request,
  env: Env,
  // A seam for the test only: no real MAC derives to an allowlisted fake id, so the
  // refusal cannot be reached from input. Production always passes the real check.
  isFake: (env: Env, id: string) => boolean = refusesFakeId,
): Promise<Response> {
  // 1. The token, before anything else is read or revealed -- including the cap.
  const presented = request.headers.get("X-Provision-Token") ?? "";
  if (!(await tokenMatches(presented, env.PROVISION_TOKEN))) {
    record(env, "provision_refused", { reason: presented ? "bad_token" : "no_token" });
    return json({ error: "forbidden" }, 403);
  }
  const secret = env.DEVICE_KEY_SECRET;
  if (!secret) return json({ error: "not_configured" }, 503);

  let body: { mac?: unknown };
  try {
    body = (await request.json()) as typeof body;
  } catch {
    return json({ error: "bad_mac" }, 400);
  }
  const mac = normalizeMac(body.mac);
  if (!mac) {
    record(env, "provision_refused", { reason: "bad_mac" });
    return json({ error: "bad_mac" }, 400);
  }
  const [deviceId, mac_hash] = await Promise.all([deviceIdFromMac(mac), macHash(secret, mac)]);

  if (isFake(env, deviceId)) {
    record(env, "provision_refused", { reason: "fake_device_id", mac_hash, device: deviceId });
    return json({ error: "fake_device_id" }, 403);
  }
  if (await isRevoked(env, deviceId)) {
    record(env, "provision_refused", { reason: "revoked", mac_hash, device: deviceId });
    return json({ error: "revoked" }, 403);
  }

  // BEST EFFORT, AND SAID SO. KV has no atomic increment: mints racing on the same
  // counter (at most the hub's 8 in parallel) can overshoot by that many. The cap
  // bounds what a leaked token can mint in a day; it is not an exact meter.
  const day = new Date().toISOString().slice(0, 10);
  const before = Number((await env.ENRICH_KV.get(dayKey(day))) ?? "0");
  if (before >= PROVISION_DAILY_CAP) {
    record(env, "provision_refused", { reason: "daily_cap", mac_hash, device: deviceId, mints_today: before });
    const resetsAt = new Date(Date.parse(`${day}T00:00:00Z`) + 86400000).toISOString();
    return json({ error: "daily_cap", cap: PROVISION_DAILY_CAP, resetsAt }, 429);
  }
  const key = await deriveDeviceKey(secret, deviceId);
  const mintsToday = before + 1;
  await env.ENRICH_KV.put(dayKey(day), String(mintsToday), { expirationTtl: 60 * 60 * 24 * 40 });
  record(env, "provision_mint", { mac_hash, device: deviceId, mints_today: mintsToday });
  return json({ deviceId, key, mintsToday });
}
