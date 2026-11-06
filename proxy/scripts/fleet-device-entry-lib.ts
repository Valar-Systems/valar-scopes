/**
 * fleet-device-entry-lib.ts -- the pure half of scripts/fleet-device-entry.ts (docs/bend-site-feed.md, "The
 * feeder's night cadence"; the value check ruled 2026-11-05 ~19:50Z). No node: imports, so the test runs in the
 * Workers pool.
 *
 * WHAT THE FIRMWARE ACCEPTS (src/CloudFeed.cpp, ParseConfig), which is the only range this tool will write:
 *   - each poll field is applied only if `is<unsigned long>()`: a whole number in 0..4294967295;
 *   - pollActiveMs is floored at 1000, and pollIdleMs / pollNightMs at the interval below them, so no poll interval
 *     the device runs is ever under 1000 ms.
 * So a value is written only if it is a whole number from 1000 to 4294967295. Anything else -- a fraction, a sign,
 * an exponent, a string with spaces, a number out of range -- is refused before anything is read or written, rather
 * than handed to a device that would silently ignore or clamp it.
 */

export const ENTRY_FIELDS = ["pollActiveMs", "pollIdleMs", "pollNightMs"] as const;
export type EntryField = (typeof ENTRY_FIELDS)[number];
export const MIN_MS = 1000;
export const MAX_MS = 4294967295;

export type ValueCheck = { ok: true; field: EntryField; value: number } | { ok: false; reason: string };

/** The value as typed on the command line. Refuses anything that is not a whole number the firmware applies unchanged. */
export function checkEntryValue(field: string, raw: string): ValueCheck {
  if (!(ENTRY_FIELDS as readonly string[]).includes(field)) {
    return { ok: false, reason: `the field must be one of ${ENTRY_FIELDS.join(", ")}` };
  }
  if (!/^[0-9]+$/.test(raw)) return { ok: false, reason: "the value must be a whole number of milliseconds" };
  const value = Number(raw);
  if (!Number.isSafeInteger(value) || value < MIN_MS || value > MAX_MS) {
    return { ok: false, reason: `the value must be from ${MIN_MS} to ${MAX_MS} (the range the firmware applies)` };
  }
  return { ok: true, field: field as EntryField, value };
}

const isObject = (x: unknown): x is Record<string, unknown> =>
  typeof x === "object" && x !== null && !Array.isArray(x);

/** cfg:fleet with devices[id][field] = value; every other field and every other device untouched. */
export function mergeDeviceEntry(fleet: unknown, id: string, field: EntryField, value: number): Record<string, unknown> {
  const base = isObject(fleet) ? fleet : {};
  const devices = isObject(base.devices) ? base.devices : {};
  const entry = isObject(devices[id]) ? devices[id] : {};
  return { ...base, devices: { ...devices, [id]: { ...entry, [field]: value } } };
}

/**
 * The read-back, as one boolean: what KV now holds equals exactly the merge of what it held, so the entry landed and
 * nothing else in cfg:fleet moved. Compared as canonical JSON (keys sorted), so key order cannot fake a difference.
 */
export function readBackMatches(before: unknown, after: unknown, id: string, field: EntryField, value: number): boolean {
  return canonical(after) === canonical(mergeDeviceEntry(before, id, field, value));
}

function canonical(x: unknown): string {
  if (Array.isArray(x)) return `[${x.map(canonical).join(",")}]`;
  if (isObject(x)) return `{${Object.keys(x).sort().map((k) => `${JSON.stringify(k)}:${canonical(x[k])}`).join(",")}}`;
  return JSON.stringify(x);
}
