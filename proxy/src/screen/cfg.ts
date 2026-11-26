// The screen service's two owner-written switches, in the boards' ENRICH_KV, each read with a 60 s cache by the ONE
// ScreenDetailsDO (registration D7, C1). Nothing in the service writes either key: a flip is the owner's KV write.
// Both FAIL CLOSED.
//
//   cfg:screen-places   exactly "any" -> any; absent, unreadable or anything else -> "circle" (D7)
//   cfg:screen-blocked  absent -> an empty list; a JSON array of 6-hex strings (either case) -> that list;
//                       anything else, or a read that throws -> UNKNOWN, which refuses every call (C1)
//
// No value of either key is ever logged or counted (C1, P-E2f): the list is the set of aircraft whose owners asked to
// be hidden.

export type Places = "circle" | "any";
export type Blocked = { ok: true; set: ReadonlySet<string> } | { ok: false };

export const CFG_TTL_MS = 60_000;
export const PLACES_KEY = "cfg:screen-places";
export const BLOCKED_KEY = "cfg:screen-blocked";

export function parsePlaces(v: string | null): Places {
  /*P-G2*/
  return v === "any" ? "any" : "circle";
}

export function parseBlocked(v: string | null): Blocked {
  if (v === null) return { ok: true, set: new Set() };
  let j: unknown;
  try {
    j = JSON.parse(v);
  } catch {
    return { ok: false };
  }
  /*P-E2c*/
  if (!Array.isArray(j) || !j.every((x) => typeof x === "string" && /^[0-9a-fA-F]{6}$/.test(x))) return { ok: false };
  return { ok: true, set: new Set((j as string[]).map((x) => x.toLowerCase())) };
}

/**
 * One key, read at most once per CFG_TTL_MS (on the caller's clock), with concurrent refreshes sharing one read.
 * A read that throws yields `failed` and is cached like any other answer: fail closed, and no read storm in an outage.
 */
export class CachedKey<T> {
  private value: T | undefined;
  private at = -Infinity;
  private inflight: Promise<T> | null = null;

  constructor(private readonly key: string, private readonly parse: (v: string | null) => T, private readonly failed: T) {}

  async get(kv: KVNamespace, now: number): Promise<T> {
    /*P-G3*/
    if (this.value !== undefined && now - this.at < CFG_TTL_MS) return this.value;
    if (this.inflight) return this.inflight;
    this.inflight = (async () => {
      let v: T;
      try {
        v = this.parse(await kv.get(this.key));
      } catch {
        v = this.failed;
      }
      this.value = v;
      this.at = now;
      return v;
    })();
    try {
      return await this.inflight;
    } finally {
      this.inflight = null;
    }
  }
}
