// W3 (docs/weatherscope-stage4.md S4.1.3, S4.2, S4.4): can a DEPLOYED Worker reach api.weather.gov?
//
// STAGING ONLY, AND TEMPORARY (Q122, ruled 2026-11-08). The route answers 404 unless W3_PROBE is "on", which only
// [env.staging.vars] sets, and staging is rolled back to its previous version once the reading is taken. This branch is
// never merged. Like every API route it sits behind the proxy's device auth and per-key limit, and it makes no call
// unless asked.
//
// One request runs one round: for each of 5 public airports, the 7 calls W5 will make, in order, then one control call
// with no User-Agent. It returns ONLY each call's status, time, size and max-age. No body, no URL and no coordinate
// leaves it: an airport's position comes from NWS's own station record, inside the Worker.
import type { Env } from "../types";
import { errorResponse } from "../util";

const API = "https://api.weather.gov";
const UA = "Weatherscope (support@valarsystems.com)";
export const W3_AIRPORTS = ["KBOS", "KDEN", "KDFW", "KMIA", "KSEA"] as const;
export const W3_KINDS = ["station", "points", "forecast", "hourly", "stations", "observation", "alerts"] as const;

export interface W3Row { kind: string; status: number; ms: number; bytes: number; maxAge: number | null; err: string | null }
export interface W3Round { round: number; airports: Record<string, W3Row[]>; control: W3Row }
export type W3Fetch = (url: string, init: RequestInit) => Promise<Response>;

async function one(f: W3Fetch, kind: string, url: string, withUa: boolean): Promise<{ row: W3Row; json: unknown }> {
  const t0 = Date.now();
  const row: W3Row = { kind, status: 0, ms: 0, bytes: 0, maxAge: null, err: null };
  let json: unknown = null;
  try {
    const headers: Record<string, string> = { Accept: "application/geo+json" };
    if (withUa) headers["User-Agent"] = UA;
    const r = await f(url, { headers });
    row.status = r.status;
    const buf = await r.arrayBuffer();
    row.bytes = buf.byteLength;
    const m = /max-age=(\d+)/.exec(r.headers.get("Cache-Control") ?? "");
    row.maxAge = m ? Number(m[1]) : null;
    if (r.status === 200) {
      try { json = JSON.parse(new TextDecoder().decode(buf)); } catch { row.err = "not json"; }
    }
  } catch (e) {
    row.err = e instanceof Error ? e.name : "fetch failed";   // the error's kind only: its message can carry the URL
  }
  row.ms = Date.now() - t0;
  return { row, json };
}

const skipped = (kind: string): W3Row => ({ kind, status: 0, ms: 0, bytes: 0, maxAge: null, err: "skipped" });

async function airport(f: W3Fetch, id: string): Promise<W3Row[]> {
  const rows: W3Row[] = [];
  const rest = (from: number) => { for (const k of W3_KINDS.slice(from)) rows.push(skipped(k)); return rows; };
  let r = await one(f, "station", `${API}/stations/${id}`, true);
  rows.push(r.row);
  const c = (r.json as { geometry?: { coordinates?: number[] } } | null)?.geometry?.coordinates;
  if (!c || c.length < 2) return rest(1);
  const pt = `${Number(c[1]).toFixed(4)},${Number(c[0]).toFixed(4)}`;
  r = await one(f, "points", `${API}/points/${pt}`, true);
  rows.push(r.row);
  const p = (r.json as { properties?: Record<string, string> } | null)?.properties;
  if (!p || !p.forecast || !p.forecastHourly || !p.observationStations) return rest(2);
  rows.push((await one(f, "forecast", p.forecast, true)).row);
  rows.push((await one(f, "hourly", p.forecastHourly, true)).row);
  r = await one(f, "stations", p.observationStations, true);
  rows.push(r.row);
  const st = (r.json as { features?: Array<{ properties?: { stationIdentifier?: string } }> } | null)
    ?.features?.[0]?.properties?.stationIdentifier;
  rows.push(st ? (await one(f, "observation", `${API}/stations/${st}/observations/latest`, true)).row : skipped("observation"));
  rows.push((await one(f, "alerts", `${API}/alerts/active?point=${pt}`, true)).row);
  return rows;
}

export async function runW3(f: W3Fetch, round: number): Promise<W3Round> {
  const airports: Record<string, W3Row[]> = {};
  for (const id of W3_AIRPORTS) airports[id] = await airport(f, id);
  const control = (await one(f, "control: station, no User-Agent", `${API}/stations/KDFW`, false)).row;
  return { round, airports, control };
}

export async function handleW3Probe(request: Request, env: Env, f: W3Fetch = (u, i) => fetch(u, i)): Promise<Response> {
  if (env.W3_PROBE !== "on") return errorResponse(404, "not_found");
  const raw = new URL(request.url).searchParams.get("round") ?? "";
  if (!/^[1-9]$/.test(raw)) return errorResponse(400, "bad_round");   // a round is one digit, 1-9: anything else is refused
  return new Response(JSON.stringify(await runW3(f, Number(raw))), {
    headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
  });
}
