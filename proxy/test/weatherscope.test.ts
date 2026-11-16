// W5 (docs/weatherscope-stage4.md S4.11, S4.13, S4.14): Weatherscope's three NWS routes, row by row. NWS is mocked
// and every outbound call is counted through a fetch spy; every log line of the file is captured. Each test's title
// starts with its row (WSn:), which is how scripts/sabotage-weatherscope.mjs reads which rows went red.
//
// Every location here is synthetic, at 2 dp, near KMCI, a public airport the /wx tests already use. No raw NWS
// document is in the repo: test/fixtures/wx/nws/w5-raw/ is written by hand.
import { env, fetchMock } from "cloudflare:test";
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it, vi, type MockInstance } from "vitest";
import { __resetBreakersForTests, breakerState } from "../src/upstreams/types";
import { routeTemplate } from "../src/metrics";
import { quantise } from "../src/wx/crop";
import { __setClockForTests, UA } from "../src/weatherscope/fetch";
import { presentOf, trimAlerts, trimForecast, trimObservation, utcOf } from "../src/weatherscope/trim";
import { apiRequest, call, TEST_DEVICE, TEST_KEY } from "./helpers";
import warmHourly from "../../test/fixtures/wx/nws/w5-raw/warm/hourly.json";
import warmForecast from "../../test/fixtures/wx/nws/w5-raw/warm/forecast.json";
import warmObservation from "../../test/fixtures/wx/nws/w5-raw/warm/observation.json";
import warmAlerts from "../../test/fixtures/wx/nws/w5-raw/warm/alerts.json";
import warmStations from "../../test/fixtures/wx/nws/w5-raw/warm/stations.json";
import coldHourly from "../../test/fixtures/wx/nws/w5-raw/cold/hourly.json";
import coldForecast from "../../test/fixtures/wx/nws/w5-raw/cold/forecast.json";
import coldObservation from "../../test/fixtures/wx/nws/w5-raw/cold/observation.json";
import coldAlerts from "../../test/fixtures/wx/nws/w5-raw/cold/alerts.json";
import goldenWarm from "../../test/fixtures/wx/nws/w5-golden/warm.json";
import goldenCold from "../../test/fixtures/wx/nws/w5-golden/cold.json";
import bodyAlerts from "../../test/fixtures/wx/nws/w5/alerts.json";
import bodyConditions from "../../test/fixtures/wx/nws/w5/conditions.json";
import bodyForecast from "../../test/fixtures/wx/nws/w5/forecast.json";

const NWSO = "https://api.weather.gov";
const MODEL = "weatherscope-s3-128";
const ROUTES = ["alerts", "conditions", "forecast"] as const;
const T0 = 1_794_400_000;
let t = T0;

// ------------------------------------------------------------------------------------------------- the instruments

const lines: string[] = [];
let logSpy: MockInstance;
let fetchSpy: MockInstance;
beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
  logSpy = vi.spyOn(console, "log").mockImplementation((...a: unknown[]) => void lines.push(a.map(String).join(" ")));
  fetchSpy = vi.spyOn(globalThis, "fetch");
});
afterAll(() => {
  logSpy.mockRestore();
  fetchSpy.mockRestore();
});
// Each test must consume every mock IT registers. Counted against the count when it began, not against zero, so a test
// that leaves mocks behind (a sabotaged run) is red itself without turning every later test red too.
let pendingAtStart = 0;
beforeEach(() => {
  __resetBreakersForTests();
  t = T0;
  __setClockForTests(() => t);
  fetchSpy.mockClear();
  pendingAtStart = fetchMock.pendingInterceptors().length;
});
afterEach(() => {
  __setClockForTests(null);
  expect(fetchMock.pendingInterceptors().length, "mocks this test registered and never used").toBe(pendingAtStart);
});

/** The NWS calls made since the test began: URL and headers. */
function calls(): { url: string; headers: Record<string, string> }[] {
  return fetchSpy.mock.calls
    .map(([u, init]) => ({ url: String(u instanceof Request ? u.url : u), headers: ((init as RequestInit | undefined)?.headers ?? {}) as Record<string, string> }))
    .filter((c) => c.url.startsWith(NWSO));
}
const callsTo = (frag: string): number => calls().filter((c) => c.url.includes(frag)).length;
const logsSince = (n: number): string[] => lines.slice(n);

// ------------------------------------------------------------------------------------------------------ NWS mocks

const nws = () => fetchMock.get(NWSO);
const json = (doc: unknown) => JSON.stringify(doc);
/** A synthetic /points answer naming grid EAX/<x>,<y>. */
function pointsDoc(x: number, y = 50): unknown {
  const g = `${NWSO}/gridpoints/EAX/${x},${y}`;
  return { properties: { gridId: "EAX", gridX: x, gridY: y, forecast: `${g}/forecast`, forecastHourly: `${g}/forecast/hourly`,
                         observationStations: `${g}/stations` } };
}
const mockPoints = (lat2: string, lon2: string, x: number, status = 200) =>
  nws().intercept({ path: `/points/${lat2},${lon2}` }).reply(status, status === 200 ? json(pointsDoc(x)) : "{}");
const mockStations = (x: number, doc: unknown = warmStations) => nws().intercept({ path: `/gridpoints/EAX/${x},50/stations` }).reply(200, json(doc));
const mockObs = (id: string, doc: unknown = warmObservation, status = 200) =>
  nws().intercept({ path: `/stations/${id}/observations/latest` }).reply(status, json(doc));
const mockForecast = (x: number, delay = 0) => {
  const a = nws().intercept({ path: `/gridpoints/EAX/${x},50/forecast` }).reply(200, json(warmForecast));
  const b = nws().intercept({ path: `/gridpoints/EAX/${x},50/forecast/hourly` }).reply(200, json(warmHourly));
  if (delay) { a.delay(delay); b.delay(delay); }
};
const mockAlerts = (lat2: string, lon2: string, doc: unknown = warmAlerts, status = 200) =>
  nws().intercept({ path: `/alerts/active?point=${lat2},${lon2}` }).reply(status, json(doc));

const ws = (route: string, q: string, h: Record<string, string> = {}) =>
  apiRequest(`/api/v1/weatherscope/${route}?${q}`, { "X-Blip-Model": MODEL, ...h });
async function bodyOf(route: string, q: string, h: Record<string, string> = {}, o = {}): Promise<Record<string, unknown>> {
  const res = await call(ws(route, q, h), o);
  expect(res.status, `${route} ${q}`).toBe(200);
  return (await res.json()) as Record<string, unknown>;
}
const keys = (o: unknown): string[] => Object.keys(o as object).sort();

// Each test keys its own points and grids, so nothing it caches can reach another test.
const P = (n: number) => ({ q: `lat=39.${(30 + n).toString().padStart(2, "0")}&lon=-94.71`, lat2: `39.${(30 + n).toString().padStart(2, "0")}`, lon2: "-94.71", x: 40 + n });

// -------------------------------------------------------------------------------------------------------- the rows

describe("W5: Weatherscope's NWS routes", () => {
  it("WS1: each route answers 401 with no key, and 200 with a valid key", async () => {
    for (const route of ROUTES) {
      const res = await call(new Request(`https://proxy.test/api/v1/weatherscope/${route}?lat=39.31&lon=-94.71`));
      expect(res.status, route).toBe(401);
    }
    const p = P(1);
    mockAlerts(p.lat2, p.lon2);
    mockPoints(p.lat2, p.lon2, p.x); mockStations(p.x); mockObs("KMCI");
    mockForecast(p.x);
    for (const route of ROUTES) expect((await call(ws(route, p.q))).status, route).toBe(200);
  });

  it("WS2: a Weatherscope suffix reaches only Weatherscope's handler, and a POST is a 405", async () => {
    const q = "lat=39.32&lon=-94.71";
    const a = await call(ws("blips", q));
    expect(a.status).toBe(404);
    expect(a.headers.get("Content-Type") ?? "").toContain("application/json");
    expect((await call(apiRequest(`/api/v1/blipscope/alerts?${q}`))).status).toBe(404);
    expect((await call(apiRequest(`/v1/alerts?${q}`))).status).toBe(404);
    for (const route of [...ROUTES, "leaderboard"]) {
      const res = await call(new Request(`https://proxy.test/api/v1/weatherscope/${route}?${q}`, {
        method: "POST", headers: { "X-Blip-Key": TEST_KEY, "X-Blip-Device": TEST_DEVICE } }));
      expect(res.status, route).toBe(405);
    }
    expect(calls().length).toBe(0);
  });

  it("WS3: a bad lat or lon is a 400 with the radar route's code, and makes 0 NWS calls", async () => {
    for (const route of ROUTES) {
      for (const [q, code] of [["lat=91&lon=-94.71", "bad_lat"], ["lat=x&lon=-94.71", "bad_lat"], ["lat=39.33&lon=181", "bad_lon"],
                               ["lat=39.33", "bad_lon"]] as const) {
        const res = await call(ws(route, q));
        expect(res.status, `${route} ${q}`).toBe(400);
        expect(await res.json(), `${route} ${q}`).toMatchObject({ error: code });
      }
    }
    expect(calls().length).toBe(0);
  });

  it("WS4: routeTemplate labels each route as itself", () => {
    for (const route of ROUTES) expect(routeTemplate(`/api/v1/weatherscope/${route}`)).toBe(`/api/v1/weatherscope/${route}`);
  });

  it("WS5: NWS's own empty answer is the only all-clear: ok, items []", async () => {
    const p = P(5);
    mockAlerts(p.lat2, p.lon2, { features: [] });
    expect(await bodyOf("alerts", p.q)).toStrictEqual({ v: 1, status: "ok", fetchedUtc: T0, items: [] });
  });

  it("WS6: a failure with no last good copy is unavailable with no data key; after a good fetch it is stale", async () => {
    // No last good copy: each route's FINAL NWS step fails (no interceptor, and the net is closed), each at its own point.
    const legs = { alerts: P(6), conditions: P(26), forecast: P(27) };
    mockPoints(legs.conditions.lat2, legs.conditions.lon2, legs.conditions.x); mockStations(legs.conditions.x);
    mockPoints(legs.forecast.lat2, legs.forecast.lon2, legs.forecast.x);
    for (const route of ROUTES) {
      const b = await bodyOf(route, legs[route].q);
      expect(keys(b), route).toStrictEqual(["fetchedUtc", "reason", "status", "v"]);
      expect(b, route).toMatchObject({ v: 1, status: "unavailable", reason: "upstream_failed" });
    }
    // A last good copy: a good fetch, the clock past the hold, then a failing refetch serves the good copy as stale.
    const s = P(7);
    mockAlerts(s.lat2, s.lon2); mockPoints(s.lat2, s.lon2, s.x); mockStations(s.x); mockObs("KMCI"); mockForecast(s.x);
    const good: Record<string, Record<string, unknown>> = {};
    for (const route of ROUTES) good[route] = await bodyOf(route, s.q);
    t = T0 + 1_801;   // past alerts' 60 s, obs's 300 s and forecast's 1,800 s; inside points' and stations' holds
    for (const route of ROUTES) {
      const b = await bodyOf(route, s.q);
      const { status: _s, ...rest } = b;
      const { status: _g, ...goodRest } = good[route]!;
      expect(b.status, route).toBe("stale");
      expect(b.fetchedUtc, route).toBe(T0);
      expect(rest, route).toStrictEqual(goodRest);
    }
  });

  it("WS7: an open breaker makes 0 calls, logs one skip, and answers as WS6", async () => {
    const p = P(8);
    for (let i = 0; i < 3; i++) await bodyOf("alerts", p.q);   // three failures open nws_alerts
    expect(breakerState("nws_alerts")).toBe("open");
    const before = calls().length, mark = lines.length;
    const b = await bodyOf("alerts", p.q);
    expect(calls().length).toBe(before);
    const skips = logsSince(mark).filter((l) => l.includes('"evt":"upstream_skip"'));
    expect(skips).toStrictEqual(['{"evt":"upstream_skip","id":"nws_alerts","op":"alerts","reason":"breaker_open"}']);
    expect(keys(b)).toStrictEqual(["fetchedUtc", "reason", "status", "v"]);
    expect(b).toMatchObject({ status: "unavailable", reason: "breaker_open" });
  });

  it("WS8: a /points 404 is out_of_coverage, moves no breaker, and is held", async () => {
    const p = P(9);
    for (let i = 0; i < 3; i++) {
      mockPoints(p.lat2, p.lon2, p.x, 404);
      const b = await bodyOf("conditions", p.q);
      expect(b, `404 #${i + 1}`).toStrictEqual({ v: 1, status: "out_of_coverage", fetchedUtc: t });
      t += 86_401;   // past the 1-day hold of a 404, so the next one asks NWS again
    }
    expect(breakerState("nws_points")).toBe("closed");
    expect(callsTo("/points/")).toBe(3);
    t -= 86_400;   // 1 s after the third answer: inside its hold
    const b = await bodyOf("forecast", p.q);
    expect(b.status).toBe("out_of_coverage");
    expect(callsTo("/points/")).toBe(3);
  });

  it("WS9: two points in one grid make one forecast fetch, one after the other and at once", async () => {
    const a = P(10), b = P(11);
    nws().intercept({ path: `/points/${a.lat2},${a.lon2}` }).reply(200, json(pointsDoc(90)));
    nws().intercept({ path: `/points/${b.lat2},${b.lon2}` }).reply(200, json(pointsDoc(90)));
    mockForecast(90);
    await bodyOf("forecast", a.q);
    await bodyOf("forecast", b.q);
    expect([callsTo("/gridpoints/EAX/90,50/forecast/hourly"), callsTo("/gridpoints/EAX/90,50/forecast")]).toStrictEqual([1, 2]);
    // At once: a delayed answer keeps the first fetch in flight while the second asks.
    const c = P(12), d = P(13);
    nws().intercept({ path: `/points/${c.lat2},${c.lon2}` }).reply(200, json(pointsDoc(91)));
    nws().intercept({ path: `/points/${d.lat2},${d.lon2}` }).reply(200, json(pointsDoc(91)));
    mockForecast(91, 150);
    const [x, y] = await Promise.all([bodyOf("forecast", c.q), bodyOf("forecast", d.q)]);
    expect(x.status).toBe("ok");
    expect(y.status).toBe("ok");
    expect(callsTo("/gridpoints/EAX/91,50/forecast/hourly")).toBe(1);
  });

  it("WS10: inside the hold a repeat makes 0 calls; past it, 1", async () => {
    const p = P(14);
    mockAlerts(p.lat2, p.lon2);
    await bodyOf("alerts", p.q);
    t = T0 + 59;
    await bodyOf("alerts", p.q);
    expect(callsTo("/alerts/")).toBe(1);
    mockAlerts(p.lat2, p.lon2);
    t = T0 + 61;
    await bodyOf("alerts", p.q);
    expect(callsTo("/alerts/")).toBe(2);
  });

  it("WS11: every NWS call carries the User-Agent, and no request-derived value but the quantised point", async () => {
    const p = P(15);
    mockAlerts(p.lat2, p.lon2); mockPoints(p.lat2, p.lon2, p.x); mockStations(p.x); mockObs("KMCI"); mockForecast(p.x);
    for (const route of ROUTES) await bodyOf(route, p.q);
    const cs = calls();
    expect(cs.length).toBe(6);
    for (const c of cs) {
      expect(c.headers["User-Agent"], c.url).toBe(UA);
      for (const leak of [TEST_KEY, TEST_DEVICE, MODEL]) expect(c.url.includes(leak), `${leak} in ${c.url}`).toBe(false);
    }
    const g = `${NWSO}/gridpoints/EAX/${p.x},50`;
    expect(cs.map((c) => c.url).sort()).toStrictEqual([
      `${NWSO}/alerts/active?point=${p.lat2},${p.lon2}`, `${NWSO}/points/${p.lat2},${p.lon2}`,
      `${NWSO}/stations/KMCI/observations/latest`, `${g}/forecast`, `${g}/forecast/hourly`, `${g}/stations`,
    ].sort());
  });

  it("WS12: the alerts key's point is quantise()'s lat2 and lon2 for the same query", async () => {
    const q = quantise(39.4649, -94.7149, 1);
    mockAlerts(q.lat2, q.lon2);
    await bodyOf("alerts", "lat=39.4649&lon=-94.7149");
    expect(calls().map((c) => c.url)).toStrictEqual([`${NWSO}/alerts/active?point=${q.lat2},${q.lon2}`]);
  });

  it("WS13: the TypeScript trim equals trim_nws.py's golden, halves included", () => {
    const cases = [
      { g: goldenWarm, f: warmForecast, h: warmHourly, o: warmObservation, a: warmAlerts },
      { g: goldenCold, f: coldForecast, h: coldHourly, o: coldObservation, a: coldAlerts },
    ];
    for (const { g, f, h, o, a } of cases) {
      const fc = trimForecast(f, h);
      expect(fc.hourly, g.case).toStrictEqual(g.hourly);
      expect(fc.periods, g.case).toStrictEqual(g.periods);
      const ob = trimObservation(o) as unknown as Record<string, unknown>;
      expect(Object.fromEntries(Object.keys(g.observation).map((k) => [k, ob[k]])), g.case).toStrictEqual(g.observation);
      expect(trimAlerts(a).map((x) => ({ event: x.event, startUtc: x.startUtc, endUtc: x.endUtc })), g.case).toStrictEqual(g.alerts);
    }
    // The halves that separate the languages, named: Python's built-in round would give 36, 0 (for 0.5), 2 and 180.
    expect(goldenWarm.observation.tempF).toBe(37);
    expect(goldenCold.observation.tempF).toBe(-8);
    expect(goldenWarm.observation.windDirDeg).toBe(0);
    expect(goldenCold.observation.windDirDeg).toBe(181);
    expect(goldenWarm.hourly.map((x) => x.pop)).toStrictEqual([13, 0, 20, 1]);
  });

  it("WS14: present keeps METAR-shaped entries in order, up to 4, and drops a malformed one whole", async () => {
    const pw = (xs: unknown[]) => presentOf({ presentWeather: xs.map((rawString) => ({ rawString })) });
    expect(pw(["-RA", "BR", "FOO BAR", "+TSRA", "VCSH", "FG"])).toStrictEqual(["-RA", "BR", "+TSRA", "VCSH"]);
    expect(pw(["-RAXYZQ"])).toStrictEqual([]);
    expect(pw([null, 5, ""])).toStrictEqual([]);
    const p = P(16);
    mockPoints(p.lat2, p.lon2, p.x); mockStations(p.x); mockObs("KMCI", coldObservation);
    expect((await bodyOf("conditions", p.q)).present).toStrictEqual(["-SN", "FZFG"]);
  });

  it("WS15: ranked warnings carry their fields and the whole ring; nothing carries a key outside S4.11.2", async () => {
    const p = P(17);
    mockAlerts(p.lat2, p.lon2); mockPoints(p.lat2, p.lon2, p.x); mockStations(p.x); mockObs("KMCI"); mockForecast(p.x);
    const a = await bodyOf("alerts", p.q);
    const items = a.items as Record<string, unknown>[];
    expect(keys(items[0])).toStrictEqual(["endUtc", "event", "expiresIso", "expiresUtc", "maxHailSize", "maxWindGust", "messageType",
                                          "motion", "polygon", "startUtc"]);
    const ring = (warmAlerts.features[0]!.geometry as { coordinates: number[][][] }).coordinates[0]!;
    expect(items[0]!.polygon).toStrictEqual(ring.map(([lon, lat]) => [lat, lon]));
    expect(keys(items[1])).toStrictEqual(["endUtc", "event", "startUtc"]);
    const RANKED_KEYS = new Set(["event", "startUtc", "endUtc", "messageType", "expiresUtc", "expiresIso", "polygon", "motion",
                                 "maxHailSize", "maxWindGust", "tornadoDetection", "tornadoDamageThreat", "flashFloodDetection"]);
    for (const i of items) for (const k of Object.keys(i)) expect(RANKED_KEYS.has(k), k).toBe(true);
    expect(keys(a)).toStrictEqual(["fetchedUtc", "items", "status", "v"]);
    expect(keys(await bodyOf("conditions", p.q))).toStrictEqual(["fetchedUtc", "gustMph", "hasGust", "hasHumidity", "hasTemp", "hasWind",
      "hasWindDir", "humidityPct", "present", "status", "tempF", "timeUtc", "v", "windDirDeg", "windMph"]);
    const f = await bodyOf("forecast", p.q);
    expect(keys(f)).toStrictEqual(["fetchedUtc", "hourly", "issuedUtc", "offsetMin", "periods", "status", "v"]);
    expect(keys((f.hourly as unknown[])[0])).toStrictEqual(["hasPop", "hasTemp", "pop", "startUtc", "tempF"]);
    expect(keys((f.periods as unknown[])[0])).toStrictEqual(["hasPop", "hasTemp", "isDaytime", "name", "pop", "short", "startUtc", "tempF"]);
  });

  it("WS16: the largest synthetic case of each route is under 32,768 B; an alerts case past it is too_large", async () => {
    const ringOfN = (n: number) => [Array.from({ length: n }, (_, i) => [-94.71 + Math.cos(i) * 0.37, 39.3 + Math.sin(i) * 0.37])];
    const ranked = ["Tornado Warning", "Snow Squall Warning", "Severe Thunderstorm Warning", "Flash Flood Warning"];
    const params = { eventMotionDescription: ["2026-11-11T04:40:00-00:00...storm...240DEG...35KT...39.25,-94.75 39.21,-94.70"],
      maxHailSize: ["2.75"], maxWindGust: ["80 MPH"], tornadoDetection: ["OBSERVED"], tornadoDamageThreat: ["CATASTROPHIC"],
      flashFloodDetection: ["RADAR INDICATED"] };
    const big = (vertices: number) => ({ features: Array.from({ length: 16 }, (_, i) => ({
      geometry: i < 4 ? { type: "Polygon", coordinates: ringOfN(vertices) } : null,
      properties: { event: i < 4 ? ranked[i] : "Winter Weather Advisory", messageType: "Update",
                    onset: "2026-11-10T22:40:00-06:00", ends: "2026-11-11T23:30:00-06:00", expires: "2026-11-11T23:30:00-06:00",
                    parameters: params } })) });
    const p = P(18), q = P(19);
    mockAlerts(p.lat2, p.lon2, big(50));
    const res = await call(ws("alerts", p.q));
    const text = await res.text();
    expect(JSON.parse(text).status).toBe("ok");
    expect((JSON.parse(text).items as unknown[]).length).toBe(16);
    expect(new TextEncoder().encode(text).byteLength).toBeLessThan(32_768);
    mockAlerts(q.lat2, q.lon2, big(2_000));
    expect(await bodyOf("alerts", q.q)).toMatchObject({ status: "unavailable", reason: "too_large" });
    // The other two routes' largest: four present entries; 48 hours and 14 periods with long names and 24-char shorts.
    const longPeriods = { properties: { updateTime: "2026-11-10T21:12:44+00:00", periods: Array.from({ length: 20 }, (_, i) => ({
      name: `Washington's Birthday Night ${i}`, startTime: "2026-11-10T18:00:00-06:00", isDaytime: i % 2 === 0, temperature: -40,
      probabilityOfPrecipitation: { value: 100 }, shortForecast: "Chance Rain And Snow Showers Then Freezing Drizzle" })) } };
    const longHours = { properties: { generatedAt: "2026-11-10T23:30:00+00:00", periods: Array.from({ length: 156 }, () => ({
      startTime: "2026-11-10T18:00:00-06:00", temperature: -40, probabilityOfPrecipitation: { value: 100 } })) } };
    const fc = { v: 1, status: "ok", fetchedUtc: T0, ...trimForecast(longPeriods, longHours) };
    expect(fc.hourly.length).toBe(48);
    expect(fc.periods.length).toBe(14);
    expect(new TextEncoder().encode(JSON.stringify(fc)).byteLength).toBeLessThan(32_768);
    const ob = { v: 1, status: "ok", fetchedUtc: T0, ...trimObservation({ properties: { ...warmObservation.properties,
      presentWeather: ["+TSRAGR", "-SHRASN", "VCFC", "BLSN"].map((rawString) => ({ rawString })) } }) };
    expect(ob.present.length).toBe(4);
    expect(new TextEncoder().encode(JSON.stringify(ob)).byteLength).toBeLessThan(32_768);
  });

  it("WS17: one body keeps one ETag, and a matching If-None-Match is a 304 with no body", async () => {
    const p = P(20);
    mockAlerts(p.lat2, p.lon2);
    const first = await call(ws("alerts", p.q));
    const etag = first.headers.get("ETag");
    expect(etag).toMatch(/^"[0-9a-f]+"$/);
    t = T0 + 30;
    const second = await call(ws("alerts", p.q));
    expect(second.headers.get("ETag")).toBe(etag);
    const third = await call(ws("alerts", p.q, { "If-None-Match": etag! }));
    expect(third.status).toBe(304);
    expect(await third.text()).toBe("");
    expect(callsTo("/alerts/")).toBe(1);
  });

  it("WS18: /healthz lists the five breakers; three alerts failures open nws_alerts, logged once across four requests", async () => {
    const hz = async () => (await (await call(new Request("https://proxy.test/healthz"))).json()) as { weatherscope: { breakers: { id: string; state: string }[] } };
    expect((await hz()).weatherscope.breakers).toStrictEqual(
      ["nws_points", "nws_alerts", "nws_stations", "nws_obs", "nws_forecast"].map((id) => ({ id, state: "closed" })));
    const p = P(21), mark = lines.length;
    for (let i = 0; i < 4; i++) await bodyOf("alerts", p.q);
    const opened = logsSince(mark).filter((l) => l.includes('"evt":"breaker"'));
    expect(opened).toStrictEqual(['{"evt":"breaker","id":"nws_alerts","state":"open","consecutiveFailures":3}']);
    expect((await hz()).weatherscope.breakers.find((b) => b.id === "nws_alerts")?.state).toBe("open");
  });

  it("WS18b: nothing W5 adds to /healthz holds a key named commit, at any depth", async () => {
    const body = await (await call(new Request("https://proxy.test/healthz"))).json() as Record<string, unknown>;
    const walk = (o: unknown, out: string[] = []): string[] => {
      if (o && typeof o === "object") for (const [k, v] of Object.entries(o)) { out.push(k); walk(v, out); }
      return out;
    };
    expect(walk(body).includes("commit")).toBe(true);   // CONTROL: the walk sees the body's own top-level commit
    expect(walk(body.weatherscope).includes("commit")).toBe(false);
  });

  it("WS20: the route's bodies for the warm case equal test/fixtures/wx/nws/w5/*.json", async () => {
    t = utcOf(warmHourly.properties.generatedAt);
    const p = P(22);
    mockAlerts(p.lat2, p.lon2); mockPoints(p.lat2, p.lon2, p.x); mockStations(p.x); mockObs("KMCI"); mockForecast(p.x);
    expect(await bodyOf("alerts", p.q)).toStrictEqual(bodyAlerts);
    expect(await bodyOf("conditions", p.q)).toStrictEqual(bodyConditions);
    expect(await bodyOf("forecast", p.q)).toStrictEqual(bodyForecast);
  });

  it("WS22: a Weatherscope request with X-Blip-Boot writes one boot row carrying its model", async () => {
    const p = P(23);
    mockAlerts(p.lat2, p.lon2);
    const writeDataPoint = vi.fn();
    await bodyOf("alerts", p.q, { "X-Blip-Boot": "POWERON" }, { METRICS: { writeDataPoint } });
    const boots = writeDataPoint.mock.calls.map((c) => c[0] as { indexes?: string[]; blobs?: string[] })
      .filter((d) => d.indexes?.[0] === "boot");
    expect(boots.length).toBe(1);
    expect(boots[0]!.blobs![2]).toBe(MODEL);
  });

  it("WS23: an empty station list is unavailable / upstream_failed exactly, and no observation is asked for", async () => {
    const p = P(25);
    mockPoints(p.lat2, p.lon2, p.x); mockStations(p.x, { features: [] });
    expect(await bodyOf("conditions", p.q)).toStrictEqual({ v: 1, status: "unavailable", fetchedUtc: T0, reason: "upstream_failed" });
    expect(callsTo("/observations/")).toBe(0);
  });

  // LAST, on purpose: it reads every log line the file wrote.
  it("WS19: no log line in the file holds a test point, a station or a grid", async () => {
    const p = P(24);
    mockAlerts(p.lat2, p.lon2);
    await bodyOf("alerts", p.q);
    expect(calls().some((c) => c.url.includes(p.lat2))).toBe(true);   // CONTROL: the point was in the URL NWS received
    expect(lines.length).toBeGreaterThan(0);
    const forbidden = [...Array.from({ length: 30 }, (_, n) => P(n).lat2), "39.46", "-94.71", "KMCI", "KMKC", "EAX"];
    for (const l of lines) for (const f of forbidden) expect(l.includes(f), `${f} in ${l}`).toBe(false);
  });
});
