import type { Env } from "./types";

// Per-device API keys -- since 2026-08-13 the ONLY way in. This landed first as
// an additive path alongside the shared BLIP_KEYS list so it could be rolled out
// gradually; that list was removed once every board had enrolled and the
// analytics showed no successful device request still arriving on it.
//
// A device key is HMAC-SHA256(DEVICE_KEY_SECRET, deviceId), hex. The server
// holds ONE secret and recomputes the expected key per request -- no key
// database to store or scan. Keys are minted at manufacture with
// `npm run derive-device-key <deviceId>` (the operator has the secret locally);
// the device stores its key like it stores the shared key today. Because minting
// requires the secret (which an attacker can't extract from open-source firmware,
// unlike the shared key), a device-authed request is trustworthy enough to back
// the leaderboard's "verified" tier.

// Hex-encode an ArrayBuffer.
function toHex(buf: ArrayBuffer): string {
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Constant-time-ish string compare (avoids early-exit timing leaks). Both inputs
// are fixed-length hex here, so length equality is expected.
function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

// The expected per-device key for a device id: HMAC-SHA256(secret, deviceId) hex.
export async function deriveDeviceKey(secret: string, deviceId: string): Promise<string> {
  const enc = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    enc.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"],
  );
  const sig = await crypto.subtle.sign("HMAC", key, enc.encode(deviceId));
  return toHex(sig);
}

// Validate a presented (deviceId, key) pair against DEVICE_KEY_SECRET. Returns
// false (never throws) when the secret isn't configured, the id is malformed, or
// the key doesn't match. There is no longer anything to fall back TO, so a false
// here is a 401 -- which also means an unset DEVICE_KEY_SECRET now refuses the
// entire fleet rather than quietly degrading to shared keys. That is the
// intended failure direction (closed, and loudly), but it makes the secret's
// presence a deploy-blocking precondition: see scripts/deploy.sh.
export async function verifyDeviceKey(env: Env, deviceId: string, presentedKey: string): Promise<boolean> {
  const secret = env.DEVICE_KEY_SECRET;
  if (!secret || !deviceId || !presentedKey) return false;
  if (!/^[0-9a-f]{8,32}$/.test(deviceId)) return false;
  if (!/^[0-9a-f]{64}$/.test(presentedKey)) return false; // HMAC-SHA256 hex is 64 chars
  const expected = await deriveDeviceKey(secret, deviceId);
  return timingSafeEqual(expected, presentedKey);
}
