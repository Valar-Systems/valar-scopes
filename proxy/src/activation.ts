import { deviceIdFromMac, normalizeMac, tokenMatches } from "./provision";
import type { Env } from "./types";

/* ===========================================================================
 * THE ACTIVATION ALLOWLIST — only boards Daniel sold can activate.
 *
 * Spec, facts and predictions: docs/device-activation.md (ruled 2026-11-05, a
 * follow-up to #545). One KV row per allowed id, `act:<id>`:
 *
 *   listed     on the list; its one activation is still unused
 *   activated  the activation was used. A second one needs Daniel's reset.
 *
 * The id is on the list IF AND ONLY IF its row exists. The list is built from
 * the factory's MACs and Daniel's own records, and never appears in the repo.
 * The enrolment ledger (`enr:dev:`) stays the volume log and is never read here.
 *
 * KNOWN LIMIT, ACCEPTED: the firmware is public and a batch's MACs are
 * near-consecutive, so a determined person can derive a real unit's id and
 * activate it first. The remedy is the reset (and revocation). KV is
 * eventually consistent, so two activations of one listed id racing across
 * locations can both succeed; same remedy. After a DEVICE_KEY_SECRET rotation
 * every listed id is open once more (keyGeneration), so a guesser can race the
 * owner for that one activation too; same remedy.
 * ======================================================================== */

/** Real device ids are SHA-256(MAC || salt)[:8] as hex: exactly 16 characters. */
export const ACTIVATION_ID_RE = /^[0-9a-f]{16}$/;

/** Entries per admin call. Each costs a KV get and at most one put, inside the per-invocation operation limit. */
export const MAX_BATCH = 250;

export const actKey = (id: string): string => `act:${id}`;

export interface ActivationRow {
  v: 1;
  state: "listed" | "activated";
  listedAt: string;
  activatedAt?: string;
  /** How many times Daniel has re-opened this id. */
  resets: number;
  /** The key generation the activation was made under (keyGeneration), so a rotation re-opens it once. */
  gen?: string;
}

export type ActivationState = "not_listed" | "listed" | "activated";

/**
 * THE KEY GENERATION: a keyed fingerprint of the current DEVICE_KEY_SECRET (ruled 2026-11-05 ~18:15Z).
 *
 * "Activated" means "activated under THIS secret". A rotation changes every device key, so every listed
 * id must be able to fetch its new one -- once -- without 600 manual resets. HMAC, not a plain hash: the
 * stored value says which generation, and nothing about the secret.
 */
export async function keyGeneration(secret: string): Promise<string> {
  const enc = new TextEncoder();
  const k = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const mac = new Uint8Array(await crypto.subtle.sign("HMAC", k, enc.encode("activation-gen")));
  return [...mac].map((b) => b.toString(16).padStart(2, "0")).join("").slice(0, 12);
}

/**
 * The state a row has UNDER `gen`, used by the gate and by status alike so the two cannot disagree.
 * An activation made under an earlier generation is open again. A row activated with no generation was
 * never written by this code; it refuses (fail closed) rather than being read as re-opened.
 */
function effectiveState(row: ActivationRow, gen: string): "listed" | "activated" {
  if (row.state === "listed") return "listed";
  return typeof row.gen === "string" && row.gen !== gen ? "listed" : "activated";
}

const isObject = (x: unknown): x is Record<string, unknown> =>
  typeof x === "object" && x !== null && !Array.isArray(x);

/**
 * The row, or null. A row that is not one we wrote reads as NOT LISTED: the gate
 * refuses rather than guessing, and the refusal names the support address.
 */
async function readRow(env: Env, id: string): Promise<ActivationRow | null> {
  let raw: unknown;
  try {
    raw = await env.ENRICH_KV.get(actKey(id), "json");
  } catch {
    return null;
  }
  if (!isObject(raw) || (raw.state !== "listed" && raw.state !== "activated")) return null;
  return raw as unknown as ActivationRow;
}

/**
 * THE LAZY GENERATION WRITE (accepted 2026-11-05 ~20:15Z). A row seeded `activated` before the gate deployed carries
 * no `gen` -- no tool can compute one, the secret lives only here. The first time the Worker reads such a row it writes
 * the CURRENT generation onto it, so from then on it is refused under that generation and re-opened once by a
 * rotation, exactly like a row the gate activated itself. The post-deploy sweep reads every grandfathered row once
 * through `status`, so they are all stamped before any rotation can happen.
 */
async function readStamped(env: Env, id: string, gen: string): Promise<ActivationRow | null> {
  const row = await readRow(env, id);
  if (row === null || row.state !== "activated" || typeof row.gen === "string") return row;
  const stamped: ActivationRow = { ...row, gen };
  await env.ENRICH_KV.put(actKey(id), JSON.stringify(stamped));
  return stamped;
}

export async function activationState(env: Env, id: string, gen: string): Promise<ActivationState> {
  const row = await readStamped(env, id, gen);
  return row === null ? "not_listed" : effectiveState(row, gen);
}

/**
 * THE GATE, called by the enrol route AFTER the solve verified and AFTER revocation.
 *
 * On a listed id it writes `activated` BEFORE returning, so the caller derives the
 * key only for an activation that has already been recorded. Returning first and
 * writing later would hand out a key the list does not know was issued.
 */
export async function claimActivation(env: Env, id: string, nowIso: string, gen: string): Promise<ActivationState> {
  const row = await readStamped(env, id, gen);
  if (row === null) return "not_listed";
  if (effectiveState(row, gen) === "activated") return "activated";
  const next: ActivationRow = { ...row, state: "activated", activatedAt: nowIso, gen };
  await env.ENRICH_KV.put(actKey(id), JSON.stringify(next));
  return "listed";
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  });
}

/** Counts only: no id, and no entry, ever reaches a log line from this route. */
function record(f: Record<string, string | number>): void {
  console.log(JSON.stringify({ evt: "activation_admin", ...f }));
}

/**
 * POST /blipscope/activation — Daniel's list edits (scripts/activation.ts).
 *
 * Header: X-Provision-Token: <PROVISION_TOKEN>   the bench mint's credential (src/provision.ts)
 * Body:   { op: "add" | "reset" | "status", ids?: string[], macs?: string[] }
 * 200:    add    { op, listed, already_listed, already_activated }
 *         reset  { op, reopened, already_open, not_listed }
 *         status { op, state }                    exactly one entry
 * 400:    { error: "bad_request" | "bad_op" | "empty" | "bad_entry" | "status_takes_one" }
 *         { error: "too_many", max }
 * 403:    { error: "forbidden" }                   missing/wrong token, or PROVISION_TOKEN unset
 * 503:    { error: "not_configured" }              status with no DEVICE_KEY_SECRET (no generation to read under)
 *
 * A RESPONSE NEVER CARRIES AN ID, and a refusal never repeats the entry it refused:
 * the tool's output is a transcript, and an id in it is an id in a transcript.
 *
 * ALL OR NOTHING. Every entry is validated before the first write, so one bad
 * line in a 600-MAC import changes nothing rather than half the list.
 */
export async function handleActivationAdmin(request: Request, env: Env): Promise<Response> {
  const presented = request.headers.get("X-Provision-Token") ?? "";
  if (!(await tokenMatches(presented, env.PROVISION_TOKEN))) {
    record({ result: "refused", reason: presented ? "bad_token" : "no_token" });
    return json({ error: "forbidden" }, 403);
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return json({ error: "bad_request" }, 400);
  }
  if (!isObject(body)) return json({ error: "bad_request" }, 400);
  const op = body.op;
  if (op !== "add" && op !== "reset" && op !== "status") return json({ error: "bad_op" }, 400);

  const asList = (x: unknown): unknown[] | null => (x === undefined ? [] : Array.isArray(x) ? x : null);
  const rawIds = asList(body.ids);
  const rawMacs = asList(body.macs);
  if (rawIds === null || rawMacs === null) return json({ error: "bad_request" }, 400);
  const total = rawIds.length + rawMacs.length;
  if (total === 0) return json({ error: "empty" }, 400);
  if (total > MAX_BATCH) return json({ error: "too_many", max: MAX_BATCH }, 400);

  // Validate EVERYTHING before anything is written.
  const ids = new Set<string>();
  let bad = 0;
  for (const x of rawIds) {
    const id = typeof x === "string" ? x.trim().toLowerCase() : "";
    if (ACTIVATION_ID_RE.test(id)) ids.add(id);
    else bad++;
  }
  const macs: string[] = [];
  for (const x of rawMacs) {
    const mac = normalizeMac(x);
    if (mac) macs.push(mac);
    else bad++;
  }
  if (bad > 0) {
    record({ result: "refused", reason: "bad_entry", op, bad });
    return json({ error: "bad_entry", count: bad }, 400);
  }
  for (const id of await Promise.all(macs.map(deviceIdFromMac))) ids.add(id);

  if (op === "status") {
    if (ids.size !== 1) return json({ error: "status_takes_one" }, 400);
    // The state is the EFFECTIVE one under the current key generation, as the gate sees it.
    if (!env.DEVICE_KEY_SECRET) return json({ error: "not_configured" }, 503);
    const [only] = ids;
    return json({ op, state: await activationState(env, only as string, await keyGeneration(env.DEVICE_KEY_SECRET)) });
  }

  const nowIso = new Date().toISOString();
  if (op === "add") {
    let listed = 0, alreadyListed = 0, alreadyActivated = 0;
    for (const id of ids) {
      const row = await readRow(env, id);
      if (row === null) {
        const fresh: ActivationRow = { v: 1, state: "listed", listedAt: nowIso, resets: 0 };
        await env.ENRICH_KV.put(actKey(id), JSON.stringify(fresh));
        listed++;
      } else if (row.state === "listed") {
        alreadyListed++;
      } else {
        // ADD NEVER RE-OPENS. Re-opening an activated id is a decision about one
        // customer's board, and it is reset's job alone.
        alreadyActivated++;
      }
    }
    record({ result: "ok", op, listed, already_listed: alreadyListed, already_activated: alreadyActivated });
    return json({ op, listed, already_listed: alreadyListed, already_activated: alreadyActivated });
  }

  let reopened = 0, alreadyOpen = 0, notListed = 0;
  for (const id of ids) {
    const row = await readRow(env, id);
    if (row === null) {
      // RESET NEVER LISTS. Putting an id on the list is add's job; a reset that
      // listed would turn a typo into an activatable id.
      notListed++;
    } else if (row.state === "activated") {
      const next: ActivationRow = { v: 1, state: "listed", listedAt: row.listedAt, resets: (row.resets ?? 0) + 1 };
      await env.ENRICH_KV.put(actKey(id), JSON.stringify(next));
      reopened++;
    } else {
      alreadyOpen++;
    }
  }
  record({ result: "ok", op, reopened, already_open: alreadyOpen, not_listed: notListed });
  return json({ op, reopened, already_open: alreadyOpen, not_listed: notListed });
}
