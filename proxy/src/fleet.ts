// INSTRUMENT A of the OTA control plan (docs/ota-control-plan.md, Phase 0).
//
// =============================================================================
// WHAT THIS OBSERVES, AND WHY IT IS NOT THE UPDATE CHECK
//
// Devices fetch version.txt and their firmware image DIRECTLY FROM GITHUB
// RELEASES -- see OTA_RELEASE_BASE in src/OtaUpdater.cpp. The Worker is never on
// the update path, so the update CHECK cannot be observed here at all. An
// earlier draft of the plan assumed a Worker update endpoint; there is no such
// endpoint, and building a counter on one would have measured nothing.
//
// So this does not watch the check. It watches the OUTCOME:
//
//     every feed request already carries X-Blip-Device and X-Blip-FW,
//     so an OTA SUCCESS IS THAT FIRMWARE VALUE CHANGING
//     with nobody having touched the device.
//
// Paired with Instrument B (the release asset's download_count, taken by hand
// either side of the window -- scripts/ota-download-count.sh) it separates three
// outcomes that a single signal collapses into one:
//
//     download_count   reported FW   conclusion
//     increments       changes       success, end to end
//     increments       unchanged     downloaded, then failed to verify or apply
//     unchanged        unchanged     never fetched: the timer or discovery is dead
//
// WHAT IS DELIBERATELY NOT COVERED. "The device checked in and there was nothing
// to install" is not observable while the device talks to GitHub directly, and
// no proxy for it is offered here. The plan dropped that distinction rather than
// substituting a weaker one and calling it covered.
//
// =============================================================================
// WRITTEN AFTER AUTH, ON PURPOSE
//
// This is called from the authenticated path, so an unenrolled or wrongly-keyed
// board produces NO ROW. That is not a gap, it is the answer to a separate
// launch question: a powered unit that never appears here is invisible to us in
// every sense that matters, and a row's absence says exactly that. It also means
// an anonymous caller cannot spend KV writes -- the same reason recordUsage()
// sits where it does.
//
// =============================================================================
// WHY KV AND NOT ANALYTICS ENGINE
//
// The question this answers is "what is each device running, and when did that
// change" -- a small current-state table with an append-only change log, not a
// high-volume time series. AE would need an account-scoped read token to query,
// which this project has already been burned by once (a stale KV-only token
// reported status=active while lacking the scope). A KV key can be read with the
// same credential the deploy already uses:
//
//     npx wrangler kv key list --prefix fw: --env production --remote
//     npx wrangler kv key get fw:<device> --env production --remote
//
// `--remote` or the answer is a confident lie: wrangler v4's kv commands read
// the LOCAL store by default, so without it the list returns `[]` and exits 0
// no matter what production holds. scripts/reconcile-fleet.py passes it; these
// two lines did not, and they are what someone reaches for when reconcile is
// not to hand.
//
// COST. A device polls every ~5 s. Writing every request would be ~17k writes
// per device per day for a value that changes about twice a year, so a write
// happens only when the record is NEW, when the FIRMWARE CHANGED, or when the
// last write is over an hour old. The hourly refresh is what makes lastSeen
// usable as a liveness signal; the change branch is the actual instrument.
//
// =============================================================================
// THE READ IS MEMOISED PER ISOLATE (docs/proxy-bill.md, step 4a)
//
// Writes were already rare; the READ was not. Every device request read
// fw:<device> once -- about 28% of production's KV reads -- to learn, almost
// always, "same firmware, refreshed within the hour, nothing to do". So each
// isolate remembers the STORED record's fw and lastSeen, as it last read or
// wrote them, and skips the read while that answer still holds.
//
// The memo keeps the STORED lastSeen, never the request's time. It therefore
// expires exactly when the stored record's refresh falls due, and the read at
// that moment does the write the old code would have done: liveness keeps its
// bound (REFRESH_MS plus the gap to the next request).
//
// WHAT THE READ ESTABLISHED, AND WHAT THE MEMO DOES WITH EACH -- the cost is
// written where it is paid (docs/proxy-bill.md, step 4a's table):
//   - "the stored fw equals the reported one": the memo knows only what THIS
//     isolate last saw. A device that returns within the hour to a firmware an
//     isolate still remembers (v19 -> v20 on another isolate -> v19 here) has
//     that return recorded up to REFRESH_MS LATE, never lost: the next read
//     anywhere sees the stored v20 and appends the change. reconcile-fleet.py
//     says so beside its Instrument A block.
//   - "lastSeen is fresh": another isolate's later refresh is unseen, so the
//     memo expires EARLY, never late -- an extra read, never a missed write.
//   - "the record exists": a row deleted by hand returns within REFRESH_MS.
// The update OFFER never reads this table or the memo: devices ask GitHub
// (src/OtaUpdater.cpp OTA_RELEASE_BASE). test/fleet-memo.test.ts pins that
// nothing else in proxy/src reads a fw: key.
//
// THE COUNTER: meta.fwKvRead is 1 when this request issued the read and 0 when
// the memo answered, set synchronously so record() writes it as the request
// row's double5. It is how the memo's hit rate is measured after a deploy.
import type { Env } from "./types";
import type { RequestMetric } from "./metrics";

/** Refresh lastSeen at most this often when nothing else has changed. */
export const REFRESH_MS = 60 * 60 * 1000;

/** The memo is cleared when it reaches this size: far above any fleet, and bounded so it cannot grow without limit. */
export const FLEET_MEMO_MAX = 4096;

/** Keep the change log bounded; 20 transitions is years of a real device. */
const MAX_CHANGES = 20;

export interface FleetRecord {
  fw: string;
  model: string;
  firstSeen: number;
  lastSeen: number;
  changes: Array<{ from: string; to: string; at: number }>;
}

export function fleetKey(deviceId: string): string {
  return `fw:${deviceId}`;
}

/** One isolate's recorder: its memo, and the function that consults it. Tests make their own, one per "isolate". */
export interface FleetRecorder {
  record(env: Env, ctx: ExecutionContext, meta: RequestMetric): void;
  /** How many devices the memo holds (tests, and the bound's control). */
  memoSize(): number;
}

export function makeFleetRecorder(clock: () => number = Date.now): FleetRecorder {
  // dev -> the STORED record's fw and lastSeen, as this isolate last read or wrote them.
  const memo = new Map<string, { fw: string; lastSeen: number }>();
  const remember = (dev: string, fw: string, lastSeen: number): void => {
    if (memo.size >= FLEET_MEMO_MAX && !memo.has(dev)) memo.clear();
    memo.set(dev, { fw, lastSeen });
  };

  /**
   * Record what firmware this device is reporting. Fire-and-forget: the fleet
   * table must never delay or fail a feed response, so every path here is inside
   * waitUntil and every error is swallowed.
   */
  function record(env: Env, ctx: ExecutionContext, meta: RequestMetric): void {
    const dev = (meta.dev ?? "").trim();
    const fw = (meta.fw ?? "").trim();
    // Both or nothing. A row with one half populated cannot answer the question
    // and would read as evidence that the instrument works when it does not.
    if (!dev || !fw) return;

    // The memo answers only what the read would have answered with "nothing to
    // do": the same firmware, refreshed within the hour.
    const m = memo.get(dev);
    if (m && m.fw === fw && clock() - m.lastSeen < REFRESH_MS) {
      meta.fwKvRead = 0;
      return;
    }
    meta.fwKvRead = 1;

    ctx.waitUntil(
      (async () => {
        try {
          const key = fleetKey(dev);
          const prev = await env.ENRICH_KV.get<FleetRecord>(key, "json");
          const now = clock();

          if (prev && prev.fw === fw && now - prev.lastSeen < REFRESH_MS) {
            remember(dev, prev.fw, prev.lastSeen);
            return;
          }

          const rec: FleetRecord = prev
            ? { ...prev }
            : { fw, model: meta.model ?? "", firstSeen: now, lastSeen: now, changes: [] };

          if (prev && prev.fw !== fw) {
            // THE OBSERVATION THE WHOLE PLAN TURNS ON. Appended rather than
            // overwritten: the transition and its timestamp are the evidence, and
            // a current-value-only table would show a device on the new firmware
            // without ever showing it arriving there.
            rec.changes = [...(prev.changes ?? []), { from: prev.fw, to: fw, at: now }]
              .slice(-MAX_CHANGES);
            rec.fw = fw;
          }
          if (meta.model) rec.model = meta.model;
          rec.lastSeen = now;

          await env.ENRICH_KV.put(key, JSON.stringify(rec));
          remember(dev, rec.fw, rec.lastSeen);
        } catch {
          // A fleet-table failure must not surface as a feed failure. It shows up
          // as a missing row, which the plan already treats as "not observed"
          // rather than as "did not happen" -- see the verification step. Nothing
          // is remembered, so the next request reads again.
        }
      })(),
    );
  }

  return { record, memoSize: () => memo.size };
}

/** The isolate's recorder: one per isolate, because module state is per isolate. */
const isolateRecorder = makeFleetRecorder();

/** Record what firmware this device is reporting (index.ts). See makeFleetRecorder. */
export function recordFleetFirmware(env: Env, ctx: ExecutionContext, meta: RequestMetric): void {
  isolateRecorder.record(env, ctx, meta);
}
