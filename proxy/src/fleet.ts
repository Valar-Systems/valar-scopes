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
//     npx wrangler kv key list --prefix fw: --env production
//     npx wrangler kv key get fw:<device> --env production
//
// COST. A device polls every ~5 s. Writing every request would be ~17k writes
// per device per day for a value that changes about twice a year, so a write
// happens only when the record is NEW, when the FIRMWARE CHANGED, or when the
// last write is over an hour old. The hourly refresh is what makes lastSeen
// usable as a liveness signal; the change branch is the actual instrument.
import type { Env } from "./types";
import type { RequestMetric } from "./metrics";

/** Refresh lastSeen at most this often when nothing else has changed. */
const REFRESH_MS = 60 * 60 * 1000;

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

/**
 * Record what firmware this device is reporting. Fire-and-forget: the fleet
 * table must never delay or fail a feed response, so every path here is inside
 * waitUntil and every error is swallowed.
 */
export function recordFleetFirmware(
  env: Env,
  ctx: ExecutionContext,
  meta: RequestMetric,
): void {
  const dev = (meta.dev ?? "").trim();
  const fw = (meta.fw ?? "").trim();
  // Both or nothing. A row with one half populated cannot answer the question
  // and would read as evidence that the instrument works when it does not.
  if (!dev || !fw) return;

  ctx.waitUntil(
    (async () => {
      try {
        const key = fleetKey(dev);
        const prev = await env.ENRICH_KV.get<FleetRecord>(key, "json");
        const now = Date.now();

        if (prev && prev.fw === fw && now - prev.lastSeen < REFRESH_MS) return;

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
      } catch {
        // A fleet-table failure must not surface as a feed failure. It shows up
        // as a missing row, which the plan already treats as "not observed"
        // rather than as "did not happen" -- see the verification step.
      }
    })(),
  );
}
