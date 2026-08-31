import type { Env } from "./types";

// Per-device revocation, as small as it can be.
//
// WHY IT EXISTS. deviceauth.ts is deliberately stateless: one secret, keys
// recomputed per request, no key database. Elegant -- but it left exactly one
// revocation lever, rotating DEVICE_KEY_SECRET, which invalidates the ENTIRE
// fleet at once. One leaked key, one stolen unit or one resold RMA board should
// not brick everybody else's device.
//
// SHAPE. ONE aggregate KV entry listing revoked device ids, cached in the
// isolate for ~60 s. Not a per-device KV lookup: at 50 devices polling every 5 s
// that would be ~600 reads/minute on the hottest path for no benefit. One read
// per isolate per minute is free by comparison, and a minute of propagation delay
// is irrelevant to this threat model.
//
// There is no admin surface and no new endpoint. Revoking is a wrangler command
// the operator runs (see relay/../README + the procedure at the bottom of this
// file); nothing about revocation is reachable over HTTP.
export const REVOKED_KEY = "cfg:revoked";

const CACHE_TTL_MS = 60_000;

// Same shape deviceauth.ts enforces on a presented id. Anything else in the list
// is IGNORED rather than trusted -- see the fail-open note below.
const DEVICE_ID_RE = /^[0-9a-f]{8,32}$/;

let cache: { ids: Set<string>; at: number } | null = null;

// Exported for tests and for the rare case an operator wants the next request to
// see a just-published change without waiting out the TTL.
export function resetRevocationCache(): void {
  cache = null;
}

// Parse the stored blob into a set of ids.
//
// TOLERANT ON PURPOSE. The entry is hand-edited by a human under pressure (a key
// just leaked), so it accepts newlines, commas or spaces, ignores blank lines and
// `#` comments, and lowercases. Anything that is not a well-formed device id is
// DROPPED, not matched: a typo'd or truncated entry must never become a wildcard.
export function parseRevoked(raw: string | null): Set<string> {
  const out = new Set<string>();
  if (!raw) return out;
  // LINE BY LINE, AND A '#' COMMENTS OUT THE REST OF ITS LINE.
  //
  // This used to split the WHOLE blob on whitespace and commas and skip only
  // tokens that START WITH '#'. So a '#' commented out exactly one token --
  // itself -- and every word after it on the line was still parsed. Any
  // id-shaped token anywhere in the prose was therefore a live revocation, in
  // a file whose own header says "annotate freely".
  //
  // It fired in production on 2026-08-31. An annotation naming the synthetic
  // bench identity revoked that identity, and it presented as "production
  // refuses a correctly-derived key" -- which sent the investigation through
  // wrangler versions, deployments and route config before anyone re-read the
  // comment they had just written.
  //
  // The failure is silent in the dangerous direction and self-camouflaging: the
  // symptom of an accidental revocation is indistinguishable from a broken
  // secret rotation, which is exactly what was being done at the time.
  for (const line of raw.split(/\r?\n/)) {
    const hash = line.indexOf("#");
    const body = hash >= 0 ? line.slice(0, hash) : line;
    for (const tokenRaw of body.split(/[\s,]+/)) {
      const token = tokenRaw.trim().toLowerCase();
      if (!token) continue;
      if (DEVICE_ID_RE.test(token)) out.add(token);
    }
  }
  return out;
}

// True when this device id is on the list.
//
// FAILS OPEN, DELIBERATELY. If KV is unreachable or the read throws, we return
// false (not revoked) rather than denying. The asymmetry is the whole point: a
// KV blip that fails CLOSED would take the entire fleet off the air to enforce a
// list that is empty ~100% of the time. The realistic cost of failing open is
// that one already-compromised device keeps working for another minute; the cost
// of failing closed is every customer's device going dark at once.
//
// An empty or missing entry yields an empty set, so nothing matches. An empty
// deviceId short-circuits before any lookup, so "" can never match a "" in a
// malformed list (parseRevoked would have dropped it anyway -- belt and braces).
export async function isRevoked(env: Env, deviceId: string): Promise<boolean> {
  const id = deviceId.trim().toLowerCase();
  if (!id || !DEVICE_ID_RE.test(id)) return false;

  const now = Date.now();
  if (!cache || now - cache.at > CACHE_TTL_MS) {
    try {
      const raw = await env.ENRICH_KV.get(REVOKED_KEY);
      cache = { ids: parseRevoked(raw), at: now };
    } catch {
      // Keep serving the previous snapshot if we have one; otherwise treat the
      // list as empty. Either way: never deny because of an infrastructure error.
      if (!cache) cache = { ids: new Set(), at: now };
      else cache.at = now; // don't hammer KV on every request while it is down
    }
  }
  return cache.ids.has(id);
}

// ---------------------------------------------------------------------------
// OPERATOR PROCEDURE (no admin surface exists; this is the whole interface)
//
// ALWAYS EDIT A FILE AND PUT IT WITH --path. Do not pass the list as an inline
// argument: a value containing a newline gets truncated at the first line by the
// shell, and the failure is SILENT AND WRONG IN THE DANGEROUS DIRECTION -- the
// write "succeeds", the id never lands, and the device you meant to cut off keeps
// working. Observed on the first live test of this feature: only the comment line
// was stored. (It denied nobody rather than everybody, which is the fail-open
// design working, but the operator would have believed the revocation took.)
//
//   1. Fetch the current list:
//        npx wrangler kv key get --binding=ENRICH_KV --env production --remote \
//          "cfg:revoked" > revoked.txt
//
//   2. Edit revoked.txt -- one device id per line. Blank lines are ignored and a
//      `#` comments out the REST OF ITS LINE, so annotate freely -- which became
//      true on 2026-08-31 and was not before. Until then a '#' hid only itself
//      and an id written inside a comment was a live revocation:
//        # RMA 2026-08-04, board resold
//        0123456789abcdef
//
//   3. Publish:
//        npx wrangler kv key put --binding=ENRICH_KV --env production --remote \
//          "cfg:revoked" --path revoked.txt
//
//   4. Verify it took (the id must appear in the output):
//        npx wrangler kv key get --binding=ENRICH_KV --env production --remote "cfg:revoked"
//
// Un-revoke = remove the line and repeat. Takes effect within ~60 s fleet-wide.
// Device ids come from provisioned.csv, which is the device registry.
//
// WHAT A REVOKED DEVICE DOES (verified on hardware 2026-08-01): every request
// 401s, and the firmware treats that exactly like any other failed poll --
//     [WARN] Blipscope Cloud returned HTTP 401; keeping current picture
// It holds the last picture, ages into the stale indicator past
// staleFactor x interval, and empties out. No wedge, no reboot, no alloc/hard
// failures, frame time unchanged. Restoring the id brings it back within the
// same ~60 s with no user action.
// ---------------------------------------------------------------------------
