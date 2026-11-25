// "Released" comes from the release, never from a person (P-F3, the screen registration, accepted 2026-11-25).
// The published firmware version is read at RUNTIME from the release's own version.txt -- the file every board's
// OTA check reads -- so a planned method unlocks within an hour of a promote with nobody acting, and a GitHub outage
// only ever refuses. The caller passes the fetch (the Worker fetches map.ts's RELEASE_URL; scripts/check-screen.ts
// passes a stub), so this file has NO imports and a copy of it loads on its own (the selftest's P3s sabotage).
//
// Phase 0 memoises per isolate. The per-colo Cache API layer needs the script's custom domain (the sizing's A3.6)
// and comes with phase 1; it does not change these answers.

const MEMO_MS = 60 * 60_000;      // a good read is kept for 1 h
const NULL_MEMO_MS = 5 * 60_000;  // an unreadable one for 5 minutes, so a GitHub blip is retried soon (still null meanwhile)

let memo: { at: number; value: number | null } | null = null;

/** The version file's text as a whole number, or null. Anything but optional spaces around 1-4 digits is null. */
export function parseVersion(text: string): number | null {
  /*P-F3*/
  const m = /^\s*(\d{1,4})\s*$/.exec(text);
  if (!m) return null;
  const n = Number(m[1]);
  return Number.isInteger(n) && n > 0 ? n : null;
}

/** The published version, or null when it cannot be read. Fails CLOSED: a non-200, a throw or a bad body is null. */
export async function releasedVersion(fetchVersion: () => Promise<Response>, now: number): Promise<number | null> {
  if (memo && now - memo.at < (memo.value === null ? NULL_MEMO_MS : MEMO_MS)) return memo.value;
  let value: number | null = null;
  try {
    const r = await fetchVersion();
    if (r.status === 200) value = parseVersion(await r.text());
  } catch {
    value = null;
  }
  memo = { at: now, value };
  return value;
}

/** The last read, for /health: its value and when it was taken (null before the first read). */
export const releaseMemo = (): { at: number; value: number | null } | null => memo;

/** Tests only: forget the memo, so each case reads afresh. */
export function __resetReleaseMemoForTests(): void {
  memo = null;
}
