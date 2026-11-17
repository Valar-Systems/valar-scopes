// devlabel.ts -- the device LABEL our own request line carries instead of the device id (docs/workers-logs-invocation-off.md,
// section 4; the owner's ruling, 2026-11-17): the first 8 hex characters of SHA-256 over the normalised id, no secret.
//
// A PSEUDONYM, NOT ANONYMITY. Device ids come from a small space, so anyone who holds the ids can recompute every label.
// That fits our own logs, which is what it is for.
//
// ONE RULE for what a device id is. normDeviceId() is shared by the Worker (setDeviceAttribution) and the helper
// (scripts/device-label.ts), so the label the helper prints is the label the Worker logs, by construction. No imports:
// it runs in workerd and in Node alike (crypto.subtle, TextEncoder).

const DEVICE_ID = /^[0-9a-f]{8,32}$/;

/** The normalised device id (trimmed, lowercased, 8-32 hex), or null when the input is not exactly one. */
export function normDeviceId(raw: string): string | null {
  const id = raw.trim().toLowerCase();
  return DEVICE_ID.test(id) ? id : null;
}

/** The label of a NORMALISED device id: the first 8 hex characters of SHA-256 over its UTF-8 bytes. */
export async function deviceLabel(id: string): Promise<string> {
  if (normDeviceId(id) !== id) throw new Error("deviceLabel: not a normalised device id");
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(id)));
  return Array.from(d.subarray(0, 4), (b) => b.toString(16).padStart(2, "0")).join("");
}
