import type { Env } from "./types";

// Revocation writes, sharing the device Worker's storage contract exactly.
//
// The key, the format and the tolerant parse are all defined by
// proxy/src/revocation.ts -- this file must not invent a second dialect. It is
// duplicated rather than imported because the two Workers deploy separately and
// a shared package would couple their release cycles for ~20 lines; the tests
// pin the shapes on both sides.
//
// WHAT THIS FIXES about the documented CLI procedure: the wrangler path stores
// whatever the shell hands it, and an inline value containing a newline gets
// truncated at the first line -- silently, and in the dangerous direction (the
// write "succeeds", the id never lands, the device you meant to cut off keeps
// working). That happened on the first live test of the feature. A
// read-modify-write against a parsed set cannot reproduce it.

export const REVOKED_KEY = "cfg:revoked";

// Same shape the device Worker enforces on a presented id.
export const DEVICE_ID_RE = /^[0-9a-f]{8,32}$/;

export function parseRevoked(raw: string | null): Set<string> {
  const out = new Set<string>();
  if (!raw) return out;
  for (const tokenRaw of raw.split(/[\s,]+/)) {
    const token = tokenRaw.trim().toLowerCase();
    if (!token || token.startsWith("#")) continue;
    if (DEVICE_ID_RE.test(token)) out.add(token);
  }
  return out;
}

export async function readRevoked(env: Env): Promise<Set<string>> {
  return parseRevoked(await env.ENRICH_KV.get(REVOKED_KEY));
}

// Serialise with a header that tells the next person what the file is, because
// the CLI procedure will still be used and the two must not fight.
function serialise(ids: Set<string>): string {
  const sorted = [...ids].sort();
  const header = [
    "# Blipscope revoked device ids -- one per line.",
    "# Written by the dashboard; safe to edit by hand with:",
    "#   npx wrangler kv key put --binding=ENRICH_KV --env production --remote \\",
    "#     cfg:revoked --path revoked.txt",
    "# (always --path, never an inline value: a newline truncates it silently)",
  ].join("\n");
  return sorted.length ? `${header}\n${sorted.join("\n")}\n` : `${header}\n`;
}

export type RevokeOutcome =
  | { ok: true; revoked: string[]; changed: boolean }
  | { ok: false; error: string };

// Add or remove one id. Read-modify-write against the parsed set, so a
// malformed byte already in the entry is dropped rather than propagated, and an
// id we do not recognise as well-formed is REFUSED rather than written --
// nothing that fails DEVICE_ID_RE can ever enter the list through this path.
export async function setRevoked(env: Env, idRaw: string, revoked: boolean): Promise<RevokeOutcome> {
  const id = idRaw.trim().toLowerCase();
  if (!DEVICE_ID_RE.test(id)) {
    return { ok: false, error: `"${idRaw}" is not a device id (expected 8-32 hex characters)` };
  }
  const current = await readRevoked(env);
  const had = current.has(id);
  if (revoked) current.add(id);
  else current.delete(id);
  const changed = had !== revoked;
  if (changed) await env.ENRICH_KV.put(REVOKED_KEY, serialise(current));
  return { ok: true, revoked: [...current].sort(), changed };
}
