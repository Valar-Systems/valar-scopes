// The screen's logs carry no subject (the logs-nosubject registration, GO 2026-11-25; predictions frozen in its
// registration.md before the first run). The pure half: the guard's shape table (P-NS4) and its closed id set (P-NS6).
// The half that needs the details object -- a real resolution through the boards' handleEnrich, and the shim -- is in
// test/screen-enrich.test.ts (P-NS3, P-NS3a, P-NS3b, P-NS5), which owns that harness.
//
// Every hex here is SYNTHETIC; "TST141" is a made-up callsign.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { UPSTREAM_IDS } from "../src/screen/logallow";
import { screenLogLine } from "../src/screen/logguard";
import { feedHealth } from "../src/upstreams/chain";

const HEX = "a9b9c1";
const j = (o: unknown) => JSON.stringify(o);
const dropped = (from: string) => [j({ evt: "screen_log_dropped", from })];

// Each ALLOWED line, as its producer writes it (JSON.stringify, the producer's key order): it must come back as the
// SAME string -- byte-identical, not merely equal once parsed.
const PASS: ReadonlyArray<[string, string]> = [
  ["tn_miss", j({ evt: "tn_miss", t: "ZZ9Z" })],
  ["upstream ok", j({ evt: "upstream", id: "adsb_lol", op: "hex", ok: true, ms: 12 })],
  ["upstream degraded_stale + ageS", j({ evt: "upstream", id: "adsb_lol", op: "point", ok: true, ms: 5, err: "degraded_stale", ageS: 30 })],
  ["upstream Error: HTTP 503", j({ evt: "upstream", id: "adsbdb_ac", op: "aircraft", ok: false, ms: 40, err: "Error: HTTP 503" })],
  ["upstream_skip", j({ evt: "upstream_skip", id: "adsbdb_route", op: "route", reason: "disabled_by_config" })],
  ["hold", j({ evt: "hold", id: "adsb_fi", ms: 600000 })], // 6 digits: the hex belt reads strings, never numbers
  ["breaker", j({ evt: "breaker", id: "adsb_lol_route", state: "reopened", consecutiveFailures: 3 })],
  ["screen_error", j({ evt: "screen_error", method: "enrich", caller: "app", name: "TypeError" })],
  ["screen_metrics_write_failed", j({ evt: "screen_metrics_write_failed", name: "Error" })],
  ["screen_bg_error", j({ evt: "screen_bg_error", name: "TypeError" })],
  ["screen_bg_error with the producers' fallback name", j({ evt: "screen_bg_error", name: "unknown" })],
  ["screen_log_dropped", j({ evt: "screen_log_dropped", from: "enrich_gap" })],
];

// Each line the guard must REPLACE, with the `from` its replacement carries.
const DROP: ReadonlyArray<[string, unknown[], string]> = [
  ["enrich_gap (the hex: the ask)", [j({ evt: "enrich_gap", gap: "photo", t: "A320", hex: HEX })], "enrich_gap"],
  ["route_stale (a text line: the callsign)", ["route_stale cs=TST141 SEA->BUR not_for_position"], "text"],
  ["req (unreachable from the path; dropped if it ever arrives)", [j({ evt: "req", ep: "enrich", status: 200 })], "req"],
  ["an allowed evt with an extra key", [j({ evt: "tn_miss", t: "A320", hex: HEX })], "tn_miss"],
  ["err: a runtime message", [j({ evt: "upstream", id: "adsb_lol", op: "hex", ok: false, ms: 9, err: "Network connection lost." })], "upstream"],
  ["err: Error: HTTP 5000 (4 digits, fe's)", [j({ evt: "upstream", id: "adsb_lol", op: "hex", ok: false, ms: 9, err: "Error: HTTP 5000" })], "upstream"],
  ["err: Error: HTTP 503 extra (a prefix match, fe's)", [j({ evt: "upstream", id: "adsb_lol", op: "hex", ok: false, ms: 9, err: "Error: HTTP 503 extra" })], "upstream"],
  ["id outside the closed set (a hex)", [j({ evt: "upstream", id: HEX, op: "hex", ok: true, ms: 9 })], "upstream"],
  ["id outside the closed set (a name)", [j({ evt: "hold", id: "relay_c", ms: 1000 })], "hold"],
  ["state outside its set", [j({ evt: "breaker", id: "adsb_lol", state: "half_open", consecutiveFailures: 1 })], "breaker"],
  // "Decade" is six hex letters: the name fits the Error-class shape, so only the belt can drop it.
  ["the hex belt: an Error class name holding six hex letters", [j({ evt: "screen_error", method: "enrich", caller: "app", name: "DecadeError" })], "screen_error"],
  ["caller outside the set (a name)", [j({ evt: "screen_error", method: "enrich", caller: "dashboard", name: "TypeError" })], "screen_error"],
  ["caller outside the set (a request path holding a hex)", [j({ evt: "screen_error", method: "enrich", caller: `/v1/enrich/${HEX}`, name: "TypeError" })], "screen_error"],
  ["name not an Error class", [j({ evt: "screen_bg_error", name: "Wrapped" })], "screen_bg_error"],
  ["method outside the map", [j({ evt: "screen_error", method: "photos", caller: "app", name: "TypeError" })], "screen_error"],
  ["a second argument", [j({ evt: "tn_miss", t: "A320" }), "extra"], "tn_miss"],
  ["a non-string argument", [{ evt: "tn_miss", t: "A320" }], "other"],
  ["a JSON array", [j(["tn_miss"])], "other"],
  ["an evt nobody named", [j({ evt: "new_thing", x: 1 })], "other"],
  ["a replacement line whose from is not a name", [j({ evt: "screen_log_dropped", from: HEX })], "screen_log_dropped"],
  ["a number sent as a string", [j({ evt: "upstream", id: "adsb_lol", op: "hex", ok: true, ms: "12" })], "upstream"],
  ["a duplicate key hiding a hex (the parsed object fits; the text does not)", [`{"evt":"hold","id":"${HEX}","id":"adsb_fi","ms":1}`], "hold"],
  ["no argument at all", [], "other"],
  ["an evt that is a prototype key", [j({ evt: "constructor" })], "other"],
];

describe("the guard's shape table (P-NS4)", () => {
  it.each(PASS)("P-NS4 pass: %s comes back byte-identical", (_name, line) => {
    const out = screenLogLine([line]);
    expect(out.length).toBe(1);
    expect(out[0]).toBe(line);
  });
  it.each(DROP)("P-NS4 drop: %s", (_name, args, from) => {
    expect(screenLogLine(args)).toEqual(dropped(from));
  });
});

describe("the closed id set, from the other side (P-NS6)", () => {
  it("P-NS6: the guard's upstream ids equal feedHealth()'s", () => {
    const ids = feedHealth(env as never).map((f) => f.id).sort();
    expect(ids.length).toBeGreaterThan(0); // CONTROL: there is a set to compare
    expect([...UPSTREAM_IDS].sort()).toEqual(ids);
  });
});
