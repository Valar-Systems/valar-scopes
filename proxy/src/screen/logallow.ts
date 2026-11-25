// THE SCREEN'S LOG ALLOWLIST (the logs-nosubject registration, GO 2026-11-25): what may reach the screen's Workers Logs.
//
// The screen counts THAT a call happened, never WHAT it was about. It runs the boards' handleEnrich unchanged, and two
// of that path's console lines name a subject: `enrich_gap` carries the aircraft's hex, and `route_stale` (a text line)
// its callsign and route. So the screen's console is guarded (logguard.ts): a line passes UNCHANGED only when it is
// the exact JSON serialisation of an object whose `evt` is listed below, every other key is listed for that evt, and
// every value fits its shape. Anything else is REPLACED by a `screen_log_dropped` line -- dropped, never redacted: a
// line minus its hex is a different line, and a reader cannot tell it was edited.
//
// No import but the map, so scripts/check-screen.ts can load this under Node. P-NS1 there holds every console call
// in the screen's bundle to a name below: a new line on the boards' enrich path fails that check until someone
// decides whether it is allowed or dropped.
import { SCREEN_MAP } from "./map";

export type Shape = "number" | "boolean" | RegExp | ReadonlySet<string>;

/** The upstream ids a line may name: a CLOSED set, since a pattern would also admit a 6-hex. Transcribed from
 *  src/upstreams/ (FEEDS' ids and the three breaker ids chain.ts keeps private); test/screen-logs.test.ts P-NS6 holds
 *  it equal to feedHealth()'s, read from the other side. */
export const UPSTREAM_IDS: ReadonlySet<string> = new Set([
  "adsb_lol", "adsb_lol_b", "adsb_fi", "adsb_fi_b", "airplanes_live", "adsb_lol_route", "adsbdb_route", "adsbdb_ac",
]);
const OPS = new Set(["point", "hex", "aircraft", "route"]); // chain.ts's logUpstream / logSkip call sites
const REASONS = new Set(["hold_4xx", "breaker_open", "disabled_by_config"]); // logSkip's reasons
const STATES = new Set(["open", "reopened", "closed"]); // types.ts logBreaker's states
const METHODS = new Set(SCREEN_MAP.flatMap((r) => (r.kind === "method" ? [r.method] : [])));
const CALLERS = new Set(["website", "app"]);
/** `err` is free text in the boards' line (String(err)); only these two exact shapes pass (fe's ruling). */
const ERR = /^(?:Error: HTTP \d{3}|degraded_stale)$/;
const CLASS_NAME = /^[A-Za-z]{1,40}$/;
const TYPE = /^[A-Z0-9]{1,4}$/; // an ICAO type designator: a type is not a subject (a hidden row keeps it)

const BASE: Readonly<Record<string, Readonly<Record<string, Shape>>>> = {
  tn_miss: { t: TYPE },
  upstream: { id: UPSTREAM_IDS, op: OPS, ok: "boolean", ms: "number", ageS: "number", err: ERR },
  upstream_skip: { id: UPSTREAM_IDS, op: OPS, reason: REASONS },
  hold: { id: UPSTREAM_IDS, ms: "number" },
  breaker: { id: UPSTREAM_IDS, state: STATES, consecutiveFailures: "number" },
  screen_error: { method: METHODS, caller: CALLERS, name: CLASS_NAME },
  screen_metrics_write_failed: { name: CLASS_NAME },
  screen_bg_error: { name: CLASS_NAME },
};

/** The lines the screen DROPS on purpose, by name: JSON lines by their evt, text lines by their first word. */
export const LOG_DROP = { json: ["enrich_gap", "req"], text: ["route_stale"] } as const;

/** A replacement line's `from`: an evt this file names, "text" for a line that is not JSON, else "other". */
export const DROP_FROM: ReadonlySet<string> = new Set([...Object.keys(BASE), "screen_log_dropped", ...LOG_DROP.json, "text", "other"]);

export const LOG_ALLOW: Readonly<Record<string, Readonly<Record<string, Shape>>>> = {
  ...BASE,
  screen_log_dropped: { from: DROP_FROM },
};
