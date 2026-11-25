// THE ONE MAP (P-F4) of the screen service, `blipscope-screen` (the screen registration, accepted 2026-11-25).
// Every list the service and its checks need is read from here and nowhere else: the two entrypoints' methods
// (test/screen-entry.test.ts compares each class with these rows), the parity check against the boards' route table
// (scripts/check-screen.ts reads src/index.ts), the release gate, and /health's counts.
//
// PURE DATA, no imports: scripts/check-screen.ts loads this file in plain Node.
//
// A ROW PER BOARDS' ROUTE. Route keys are as the parity check reads index.ts: "blips" for `api.suffix === "blips"`,
// "enrich/*" for `api.suffix.match(/^enrich\/...`, "edition:weatherscope" for `api.edition === "weatherscope"`.
//
// TIERS (the owner's rulings): the cloud service is the aircraft feed and details, photos, routes, the map, rain on
// the radar and terrain; warning banners are free with no end; the weather screens, Logbook Cloud and the ISS are
// Plus. The website's entrypoint carries only free and cloud rows, by construction (P-T1).
//
// FIRMWARE (parity, "planned-allowed"): a method whose feature the firmware has RELEASED answers; a PLANNED one names
// the firmware version it waits for and refuses `not_released` until the published release reads that version or
// later (P-F2, P-F3); an UNSCHEDULED one always refuses. Read from the release tags at registration: /blips, /enrich/,
// /photo/ and /airports are in v15; the radar's weather and map ship together at the release that sets
// `[weather] layer` (include/WxLayerSwitch.h), v20 by plan; no firmware calls airport/<code> (FollowArc.h's
// LookupAirport reads a baked table).

export type Tier = "free" | "cloud" | "plus";
export type Firmware = { released: number } | { waitsFor: number | "unscheduled" };
export type MethodName = "snapshot" | "enrich" | "photo" | "airports" | "weather" | "basemap" | "airport";

export interface MethodRow { kind: "method"; method: MethodName; route: string; tier: Tier; firmware: Firmware }
export interface ExcludedRow { kind: "excluded"; route: string; reason: string; ruling: string }
export interface NotRuledRow { kind: "not-ruled"; route: string; note: string }
export type Row = MethodRow | ExcludedRow | NotRuledRow;

export const SCREEN_MAP: readonly Row[] = [
  { kind: "method", method: "snapshot", route: "blips", tier: "cloud", firmware: { released: 15 } },
  { kind: "method", method: "enrich", route: "enrich/*", tier: "cloud", firmware: { released: 15 } },
  { kind: "method", method: "photo", route: "photo/*", tier: "cloud", firmware: { released: 15 } },
  { kind: "method", method: "airports", route: "airports", tier: "cloud", firmware: { released: 15 } },
  { kind: "method", method: "weather", route: "wx/*", tier: "cloud", firmware: { waitsFor: 20 } },
  { kind: "method", method: "basemap", route: "basemap", tier: "cloud", firmware: { waitsFor: 20 } },
  { kind: "method", method: "airport", route: "airport/*", tier: "cloud", firmware: { waitsFor: "unscheduled" } },
  { kind: "excluded", route: "config", reason: "a device's own settings, keyed by its verified id; a screen's settings are the app's", ruling: "screen sizing, addendum 1" },
  { kind: "excluded", route: "leaderboard", reason: "a device's identity and its hourly submission", ruling: "screen sizing, addendum 1" },
  { kind: "not-ruled", route: "edition:weatherscope", note: "another edition, build-only with no slug (unreleased); its weather screens are Plus" },
];

/** The firmware's own version file (src/OtaUpdater.cpp: OTA_RELEASE_BASE + "/version.txt"); P-F3 checks they agree. */
export const RELEASE_URL = "https://github.com/Valar-Systems/valar-scopes/releases/latest/download/version.txt";

/** The tiers the website's entrypoint may carry (the owner: "website: free"). */
export const WEBSITE_TIERS: readonly Tier[] = ["free", "cloud"];

export const methodRows = (): MethodRow[] => SCREEN_MAP.filter((r): r is MethodRow => r.kind === "method");

/** /health's counts, from the rows (not from index.ts, which the Worker cannot read). */
export function mapCounts(): { routes: number; released: number; planned: number; excluded: number; notRuled: number } {
  const methods = methodRows();
  const released = methods.filter((r) => "released" in r.firmware).length;
  return {
    routes: SCREEN_MAP.length,
    released,
    planned: methods.length - released,
    excluded: SCREEN_MAP.filter((r) => r.kind === "excluded").length,
    notRuled: SCREEN_MAP.filter((r) => r.kind === "not-ruled").length,
  };
}
