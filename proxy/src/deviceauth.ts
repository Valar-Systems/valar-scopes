import type { Env } from "./types";

// Per-device API keys (ROADMAP: "the real fix ... proxy v1 deferred"). This is
// the ADDITIVE, non-breaking foundation: the shared BLIP_KEYS path is untouched
// and always accepted, so the live fleet keeps authenticating exactly as before.
// The device-key path only activates when DEVICE_KEY_SECRET is configured AND a
// request presents an X-Blip-Device id, so it can be rolled out gradually.
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
// the key doesn't match -- callers then fall back to the shared-key path.
export async function verifyDeviceKey(env: Env, deviceId: string, presentedKey: string): Promise<boolean> {
  const secret = env.DEVICE_KEY_SECRET;
  if (!secret || !deviceId || !presentedKey) return false;
  if (!/^[0-9a-f]{8,32}$/.test(deviceId)) return false;
  if (!/^[0-9a-f]{64}$/.test(presentedKey)) return false; // HMAC-SHA256 hex is 64 chars
  const expected = await deriveDeviceKey(secret, deviceId);
  return timingSafeEqual(expected, presentedKey);
}
