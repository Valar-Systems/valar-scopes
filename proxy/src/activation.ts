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
 * locations can both succeed; same remedy.
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
}

export type ActivationState = "not_listed" | "listed" | "activated";

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

export async function activationState(env: Env, id: string): Promise<ActivationState> {
  const row = await readRow(env, id);
  return row === null ? "not_listed" : row.state;
}

/**
 * THE GATE, called by the enrol route AFTER the solve verified and AFTER revocation.
 *
 * On a listed id it writes `activated` BEFORE returning, so the caller derives the
 * key only for an activation that has already been recorded. Returning first and
 * writing later would hand out a key the list does not know was issued.
 */
export async function claimActivation(env: Env, id: string, nowIso: string): Promise<ActivationState> {
  const row = await readRow(env, id);
  if (row === null) return "not_listed";
  if (row.state === "activated") return "activated";
  const next: ActivationRow = { ...row, state: "activated", activatedAt: nowIso };
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
    const [only] = ids;
    return json({ op, state: await activationState(env, only as string) });
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
