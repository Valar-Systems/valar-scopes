// GET /api/v1/weatherscope/{alerts,conditions,forecast}?lat=&lon= (docs/weatherscope-stage4.md S4.11). Reached only
// through index.ts's Weatherscope prefix, past the same IP limit, auth gate and key limit as every API path.
//
// EVERY BODY STARTS {"v":1,"status":...,"fetchedUtc":...} and the status is one of four:
//   ok               NWS answered and it parsed (or did so inside the hold); fetchedUtc is when
//   stale            NWS failed or its breaker is open; this is the last good copy, with ITS fetchedUtc
//   unavailable      NWS failed and there is no last good copy: a `reason`, and NO data key at all -- never an empty
//                    list, because an empty alerts list is the all-clear and must only ever be NWS's own answer
//   out_of_coverage  /points answered 404 (outside NWS, Q114): an answer, not a failure
// fetchedUtc, not an age: an age changes on every serve, which would change the ETag and defeat the 304. For
// `unavailable` it is the time of the attempt.

import { quantise } from "../wx/crop";
import { errorResponse } from "../util";
import { BREAKERS, NWS, type Got, held, nowS, nwsGet, NwsRefused } from "./fetch";
import { type AlertItem, type ConditionsData, type ForecastData, trimAlerts, trimForecast, trimObservation } from "./trim";
import { breakerState } from "../upstreams/types";

export const ROUTES = ["alerts", "conditions", "forecast"] as const;
export const CAP_BYTES = 32 * 1024;   // the device's GetBytes cap

const HOLD = { points: 30 * 86_400, pointsMissing: 86_400, alerts: 60, stations: 86_400, obs: 300, forecast: 1_800 };

type Reason = "upstream_failed" | "breaker_open" | "too_large";
type Body = Record<string, unknown>;

const head = (status: string, fetchedUtc: number): Body => ({ v: 1, status, fetchedUtc });
const unavailable = (reason: Reason): Body => ({ ...head("unavailable", nowS()), reason });

/** A Got's data as a body: `ok` or `stale`, or `unavailable` with its reason. */
function bodyOf<T>(g: Got<T>, data: (d: T) => Body): Body {
  if (g.kind === "none") return unavailable(g.reason);
  return { ...head(g.kind, g.fetchedUtc), ...data(g.data) };
}

// ------------------------------------------------------------------------------------------------------ points, grid

interface Point { coverage: true; grid: string; forecast: string; forecastHourly: string; stations: string }
type PointData = Point | { coverage: false };

const onNws = (u: unknown): u is string => typeof u === "string" && u.startsWith(NWS + "/");

async function pointOf(lat2: string, lon2: string): Promise<Got<PointData>> {
  return held<PointData>(
    `points/${lat2},${lon2}`,
    (d) => (d.coverage ? HOLD.points : HOLD.pointsMissing),
    "nws_points",
    "points",
    async () => {
      const r = await nwsGet(`${NWS}/points/${lat2},${lon2}`, "points");
      if (r.status === 404) return { data: { coverage: false }, countsForBreaker: false };
      if (r.status !== 200) throw new NwsRefused(`status_${r.status}`);
      const p = (r.json as { properties?: Record<string, unknown> } | null)?.properties ?? {};
      const { gridId, gridX, gridY, forecast, forecastHourly, observationStations } = p;
      if (typeof gridId !== "string" || !/^[A-Z]{3}$/.test(gridId) || !Number.isInteger(gridX) || !Number.isInteger(gridY)
          || !onNws(forecast) || !onNws(forecastHourly) || !onNws(observationStations)) throw new NwsRefused("points_shape");
      return { data: { coverage: true, grid: `${gridId}/${gridX},${gridY}`, forecast, forecastHourly, stations: observationStations },
               countsForBreaker: true };
    },
  );
}

/** The route's answer when the point itself can't be used, else null. */
function pointRefusal(g: Got<PointData>): Body | null {
  if (g.kind === "none") return unavailable(g.reason);
  if (!g.data.coverage) return head("out_of_coverage", g.fetchedUtc);
  return null;
}

// ---------------------------------------------------------------------------------------------------------- the three

async function alertsBody(lat2: string, lon2: string): Promise<Body> {
  const g = await held<AlertItem[]>(`alerts/${lat2},${lon2}`, () => HOLD.alerts, "nws_alerts", "alerts", async () => {
    const r = await nwsGet(`${NWS}/alerts/active?point=${lat2},${lon2}`, "alerts");
    if (r.status !== 200) throw new NwsRefused(`status_${r.status}`);
    return { data: trimAlerts(r.json), countsForBreaker: true };
  });
  return bodyOf(g, (items) => ({ items }));
}

async function conditionsBody(lat2: string, lon2: string): Promise<Body> {
  const pt = await pointOf(lat2, lon2);
  const refused = pointRefusal(pt);
  if (refused) return refused;
  const p = (pt as { data: Point }).data;
  // Q124 (a), ruled: the FIRST station in NWS's list, always. A quiet station shows its own old time (A9.1 B).
  const st = await held<string>(`stations/${p.grid}`, () => HOLD.stations, "nws_stations", "stations", async () => {
    const r = await nwsGet(p.stations, "stations");
    if (r.status !== 200) throw new NwsRefused(`status_${r.status}`);
    const id = (r.json as { features?: { properties?: { stationIdentifier?: unknown } }[] } | null)?.features?.[0]
      ?.properties?.stationIdentifier;
    if (typeof id !== "string" || !/^[A-Z0-9]{3,5}$/.test(id)) throw new NwsRefused("no_station");
    return { data: id, countsForBreaker: true };
  });
  if (st.kind === "none") return unavailable(st.reason);
  const station = st.data;
  const g = await held<ConditionsData>(`obs/${station}`, () => HOLD.obs, "nws_obs", "obs", async () => {
    const r = await nwsGet(`${NWS}/stations/${station}/observations/latest`, "obs");
    if (r.status !== 200) throw new NwsRefused(`status_${r.status}`);
    return { data: trimObservation(r.json), countsForBreaker: true };
  });
  return bodyOf(g, (d) => ({ ...d }));
}

async function forecastBody(lat2: string, lon2: string): Promise<Body> {
  const pt = await pointOf(lat2, lon2);
  const refused = pointRefusal(pt);
  if (refused) return refused;
  const p = (pt as { data: Point }).data;
  // Atomic: both documents, or the last good copy of both, or unavailable (S4.11.2).
  const g = await held<ForecastData>(`forecast/${p.grid}`, () => HOLD.forecast, "nws_forecast", "forecast", async () => {
    const [f, h] = await Promise.all([nwsGet(p.forecast, "forecast"), nwsGet(p.forecastHourly, "hourly")]);
    if (f.status !== 200 || h.status !== 200) throw new NwsRefused("status");
    return { data: trimForecast(f.json, h.json), countsForBreaker: true };
  });
  return bodyOf(g, (d) => ({ ...d }));
}

// --------------------------------------------------------------------------------------------------------- the route

async function etagOf(bytes: Uint8Array): Promise<string> {
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return `"${[...d.slice(0, 16)].map((b) => b.toString(16).padStart(2, "0")).join("")}"`;
}

const enc = new TextEncoder();

export async function handleWeatherscope(request: Request, suffix: string): Promise<Response> {
  if (!(ROUTES as readonly string[]).includes(suffix)) return errorResponse(404, "not_found");
  // The same validation as the radar route (wx/handler.ts), so a device's one location is valid for both.
  const url = new URL(request.url);
  const lat = parseFloat(url.searchParams.get("lat") ?? "");
  const lon = parseFloat(url.searchParams.get("lon") ?? "");
  if (!Number.isFinite(lat) || Math.abs(lat) > 90) return errorResponse(400, "bad_lat");
  if (!Number.isFinite(lon) || Math.abs(lon) > 180) return errorResponse(400, "bad_lon");
  const q = quantise(lat, lon, 1);   // the radar's own quantiser: one location keys identically for both (WS12)

  let body = suffix === "alerts" ? await alertsBody(q.lat2, q.lon2)
    : suffix === "conditions" ? await conditionsBody(q.lat2, q.lon2)
    : await forecastBody(q.lat2, q.lon2);
  let bytes = enc.encode(JSON.stringify(body));
  // Over the device's cap: refused whole. Never a shorter ring or a shorter list (S4.11.2).
  if (bytes.byteLength > CAP_BYTES) {
    body = unavailable("too_large");
    bytes = enc.encode(JSON.stringify(body));
  }
  const etag = await etagOf(bytes);
  const headers = { ETag: etag, "Cache-Control": "no-cache" };
  if (request.headers.get("If-None-Match") === etag) return new Response(null, { status: 304, headers });
  return new Response(bytes, { status: 200, headers: { ...headers, "Content-Type": "application/json" } });
}

/** /healthz's `weatherscope` field: each NWS breaker and its state. No key named `commit` at any depth (WS18b):
 *  the deploy scripts read the deploy stamp from /healthz. */
export function weatherscopeHealth(): { breakers: { id: string; state: "closed" | "open" }[] } {
  return { breakers: BREAKERS.map((id) => ({ id, state: breakerState(id) })) };
}
