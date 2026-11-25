// PRIVATE ROWS ARE REDACTED AT THE SOURCE (G1, ruled into #816 at 15:21-15:23Z, 2026-11-25). A private aircraft is
// served with cs "" and, in its hex slot, an opaque DAILY ID. The construction is the SITE'S OWN (valarscopes-site-06,
// src/privacy.ts:115-180 and src/live.ts:156-189 at ff958d54, quoted to this repo), with one change: the day is UTC.
//
//   ID       "~" + 6 lowercase hex digits inside PRIVATE_BLOCK, 0x210000-0x22ffff: readsb's shape for a non-ICAO
//            address, so a consumer that knows nothing of IDs still never looks one up as an airframe.
//   KEY      a fresh random 32-byte HMAC-SHA256 key per UTC day, held in THIS object's memory only: never stored,
//            logged or configured. There is no secret to set or to leak. Once a day's key is gone nobody, us
//            included, can link that day's IDs to their hexes.
//   START    the first 4 bytes of HMAC(key, hex), big-endian, mod the block's size. The message is the hex alone, as
//            classed (trimmed, lowercased; a leading "~" kept): the day lives in the key.
//   PROBING  the first slot from START that is neither taken that day nor the hex of a PUBLIC aircraft in the same
//            snapshot, so an ID never collides with another ID or with a served public hex. Memoised per hex per day.
//   FULL     the block exhausted -> throw, which the call answers as `error`: never a raw row.
//
// STATED, not hidden: a Durable Object evicted mid-day loses its key, so the next read hands out NEW IDs (an aircraft's
// ID changes; nothing leaks). Parity with the site needs the SHAPE, not the day boundary (the site's day is LA 03:00),
// and no continuity is owed across the site's switch day.
export const PRIVATE_BLOCK = { lo: 0x210000, size: 0x20000 } as const;

let sizeOverride: number | undefined;
let keyObserver: ((raw: Uint8Array) => void) | undefined;
/** TEST SEAM ONLY (P-P2, P-P3): shrink the block, so avoidance and exhaustion can be reached. */
export function __setPrivateBlockSize(n: number | undefined): void {
  sizeOverride = n;
}
/** TEST SEAM ONLY (P-P4): see each new day's key bytes, so a test can prove they appear in no output. */
export function __observeDayKey(f: ((raw: Uint8Array) => void) | undefined): void {
  keyObserver = f;
}

const hex6 = (n: number): string => n.toString(16).padStart(6, "0");
/** The UTC day of a millisecond time, "YYYY-MM-DD". */
export const utcDay = (t: number): string => new Date(t).toISOString().slice(0, 10);

export class PrivateIds {
  private day = "";
  private key: Promise<CryptoKey> | null = null;
  private readonly ids = new Map<string, Promise<string>>();
  private readonly taken = new Set<number>();

  /** The daily ID for a private aircraft's hex, avoiding `publicHexes` (the same snapshot's public hexes). */
  idFor(hex: string, day: string, publicHexes: ReadonlySet<string>): Promise<string> {
    if (day !== this.day) {
      // A new day: a new key, and the day's memo and taken set start empty. All synchronous, so two calls arriving
      // together on a new day share one key.
      this.day = day;
      this.ids.clear();
      this.taken.clear();
      const raw = crypto.getRandomValues(new Uint8Array(32));
      keyObserver?.(raw);
      this.key = crypto.subtle.importKey("raw", raw, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
    }
    let id = this.ids.get(hex);
    if (!id) {
      id = this.probe(hex, this.key!, publicHexes);
      this.ids.set(hex, id); // memoised before it settles: one ID per hex per day, however many ask at once
    }
    return id;
  }

  private async probe(hex: string, key: Promise<CryptoKey>, publicHexes: ReadonlySet<string>): Promise<string> {
    const sig = new DataView(await crypto.subtle.sign("HMAC", await key, new TextEncoder().encode(hex)));
    const size = sizeOverride ?? PRIVATE_BLOCK.size;
    const start = sig.getUint32(0) % size;
    /*P-P2*/
    for (let i = 0; i < size; i++) {
      const n = PRIVATE_BLOCK.lo + ((start + i) % size);
      // A public row may itself be a "~" non-ICAO relay: an ID must not equal either spelling (the site checks only
      // the bare one).
      if (this.taken.has(n) || publicHexes.has(hex6(n)) || publicHexes.has(`~${hex6(n)}`)) continue;
      this.taken.add(n);
      return `~${hex6(n)}`;
    }
    /*P-P3*/
    throw new Error("the private-ID block is exhausted");
  }
}
