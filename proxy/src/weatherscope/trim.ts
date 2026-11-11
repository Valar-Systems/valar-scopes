// Weatherscope's trim (docs/weatherscope-stage4.md S4.11.2): NWS's documents reduced to the bodies the device reads.
// Pure functions, no I/O. Every number shared with tools/weatherscope/nws/trim_nws.py is computed the way that script
// computes it, and test/fixtures/wx/nws/w5-golden.json (written by that script) holds the two to each other (WS13):
//   - the rounding is pinned as floor(x + 0.5), NOT Python's round() (half to even) -- 36.5 F rounds to 37 in both;
//   - an integer temperature NWS sends is truncated as Python's int() truncates (toward zero);
//   - a wind direction is taken mod 360 the way Python's % does it (never negative);
//   - a time is the ISO string's instant in whole seconds, truncated as int(datetime.timestamp()) does;
//   - `short` is cut to 24 CODE POINTS, as a Python slice cuts, not 24 UTF-16 units.
// A document missing a field the device needs throws (TrimError): the caller counts that as an upstream failure, so a
// malformed answer can never become an empty, all-clear body.

export class TrimError extends Error {}

/** The pinned rounding, the same in every language that reads these records. */
export const r = (x: number): number => Math.floor(x + 0.5);

// `+ 0` turns Math.trunc's -0 (for -1 < x < 0) into 0, as Python's int() gives: JSON can't carry -0, and a test
// comparing with Object.is would read it as a different number.
const truncInt = (x: number): number => Math.trunc(x) + 0;

/** Python's `x % m` for a positive m: never negative. */
const pyMod = (x: number, m: number): number => ((x % m) + m) % m;

export function utcOf(iso: unknown): number {
  if (typeof iso !== "string") throw new TrimError("time_missing");
  const ms = Date.parse(iso);
  if (!Number.isFinite(ms)) throw new TrimError("time_unparseable");
  return Math.trunc(ms / 1000);
}

/** A QuantitativeValue's number, or null when NWS sends null or nothing. */
function val(o: unknown): number | null {
  if (o === null || typeof o !== "object") return null;
  const v = (o as { value?: unknown }).value;
  return typeof v === "number" && Number.isFinite(v) ? v : null;
}

function props(doc: unknown): Record<string, unknown> {
  const p = (doc as { properties?: unknown } | null)?.properties;
  if (p === null || typeof p !== "object") throw new TrimError("no_properties");
  return p as Record<string, unknown>;
}

const cut24 = (s: unknown): string => Array.from(typeof s === "string" ? s : "").slice(0, 24).join("");

/** The ISO string's own offset in minutes ("Z" is 0), or 0 when it carries none. */
export function offsetMinOf(iso: string): number {
  const m = /([+-])(\d{2}):(\d{2})$/.exec(iso);
  if (!m) return 0;
  const v = Number(m[2]) * 60 + Number(m[3]);
  return m[1] === "-" ? -v : v;
}

// ---------------------------------------------------------------------------------------------------------- forecast

export interface Hour { startUtc: number; tempF: number; hasTemp: boolean; pop: number; hasPop: boolean }
export interface Period {
  name: string; startUtc: number; isDaytime: boolean; tempF: number; hasTemp: boolean; pop: number; hasPop: boolean;
  short: string;
}
export interface ForecastData { issuedUtc: number; offsetMin: number; hourly: Hour[]; periods: Period[] }

export const MAX_HOURS = 48;
export const MAX_PERIODS = 14;

function periodsOf(doc: unknown): Record<string, unknown>[] {
  const ps = props(doc).periods;
  if (!Array.isArray(ps)) throw new TrimError("no_periods");
  return ps as Record<string, unknown>[];
}

function tempOf(p: Record<string, unknown>): { tempF: number; hasTemp: boolean } {
  const t = p.temperature;
  if (typeof t === "number" && Number.isFinite(t)) return { tempF: truncInt(t), hasTemp: true };
  return { tempF: 0, hasTemp: false };
}

function popOf(p: Record<string, unknown>): { pop: number; hasPop: boolean } {
  const v = val(p.probabilityOfPrecipitation);
  return v === null ? { pop: 0, hasPop: false } : { pop: r(v), hasPop: true };
}

export function trimHourly(doc: unknown): Hour[] {
  return periodsOf(doc).slice(0, MAX_HOURS).map((p) => {
    const t = tempOf(p), q = popOf(p);
    return { startUtc: utcOf(p.startTime), tempF: t.tempF, hasTemp: t.hasTemp, pop: q.pop, hasPop: q.hasPop };
  });
}

export function trimPeriods(doc: unknown): Period[] {
  return periodsOf(doc).slice(0, MAX_PERIODS).map((p) => {
    if (typeof p.name !== "string") throw new TrimError("period_name");
    const t = tempOf(p), q = popOf(p);
    return { name: p.name, startUtc: utcOf(p.startTime), isDaytime: p.isDaytime === true, tempF: t.tempF,
             hasTemp: t.hasTemp, pop: q.pop, hasPop: q.hasPop, short: cut24(p.shortForecast) };
  });
}

/** Both NWS forecast documents, or a TrimError: the body is atomic (S4.11.2). */
export function trimForecast(forecast: unknown, hourly: unknown): ForecastData {
  const periods = trimPeriods(forecast);
  const hours = trimHourly(hourly);
  const first = periodsOf(forecast)[0]?.startTime;
  return {
    issuedUtc: utcOf(props(forecast).updateTime),
    offsetMin: typeof first === "string" ? offsetMinOf(first) : 0,
    hourly: hours,
    periods,
  };
}

// -------------------------------------------------------------------------------------------------------- conditions

export interface ConditionsData {
  timeUtc: number; present: string[];
  tempF: number; hasTemp: boolean; windDirDeg: number; hasWindDir: boolean; windMph: number; hasWind: boolean;
  gustMph: number; hasGust: boolean; humidityPct: number; hasHumidity: boolean;
}

export const MAX_PRESENT = 4;   // wx::Observation::kMaxPresent
/** A METAR present-weather group: an optional intensity or proximity, then one to four two-letter codes. An entry
 *  outside this set is DROPPED, never cut: it is drawn from a known set, and a cut entry is a different entry. */
export const METAR_PRESENT = /^(?:[+-]|VC)?(?:MI|PR|BC|DR|BL|SH|TS|FZ|DZ|RA|SN|SG|IC|PL|GR|GS|UP|BR|FG|FU|VA|DU|SA|HZ|PY|PO|SQ|FC|SS|DS){1,4}$/;

export function presentOf(o: Record<string, unknown>): string[] {
  const pw = o.presentWeather;
  if (!Array.isArray(pw)) return [];
  const out: string[] = [];
  for (const e of pw) {
    const raw = (e as { rawString?: unknown } | null)?.rawString;
    if (typeof raw === "string" && METAR_PRESENT.test(raw) && out.length < MAX_PRESENT) out.push(raw);
  }
  return out;
}

export function trimObservation(doc: unknown): ConditionsData {
  const o = props(doc);
  const t = val(o.temperature), wd = val(o.windDirection), ws = val(o.windSpeed), wg = val(o.windGust),
        rh = val(o.relativeHumidity);
  return {
    timeUtc: utcOf(o.timestamp),
    present: presentOf(o),
    tempF: t !== null ? r(t * 9 / 5 + 32) : 0, hasTemp: t !== null,
    windDirDeg: wd !== null ? pyMod(r(wd), 360) : 0, hasWindDir: wd !== null,
    windMph: ws !== null ? r(ws / 1.609344) : 0, hasWind: ws !== null,
    gustMph: wg !== null ? r(wg / 1.609344) : 0, hasGust: wg !== null,
    humidityPct: rh !== null ? r(rh) : 0, hasHumidity: rh !== null,
  };
}

// ------------------------------------------------------------------------------------------------------------ alerts

/** The four warnings the warning screen shows (screens doc 3a, 3d). Only these carry the fields wxlogic::Warn reads. */
export const RANKED = new Set(["Tornado Warning", "Snow Squall Warning", "Severe Thunderstorm Warning", "Flash Flood Warning"]);

const PARAMS: [string, string][] = [
  ["motion", "eventMotionDescription"], ["maxHailSize", "maxHailSize"], ["maxWindGust", "maxWindGust"],
  ["tornadoDetection", "tornadoDetection"], ["tornadoDamageThreat", "tornadoDamageThreat"],
  ["flashFloodDetection", "flashFloodDetection"],
];

export type AlertItem = Record<string, unknown>;

/** The outer ring as [lat, lon] pairs, WHOLE -- or null when the geometry isn't a Polygon. Never a shorter ring. */
export function ringOf(geometry: unknown): [number, number][] | null {
  const g = geometry as { type?: unknown; coordinates?: unknown } | null;
  if (!g || g.type !== "Polygon" || !Array.isArray(g.coordinates)) return null;
  const outer = (g.coordinates as unknown[])[0];
  if (!Array.isArray(outer)) return null;
  const ring: [number, number][] = [];
  for (const v of outer) {
    if (!Array.isArray(v) || typeof v[0] !== "number" || typeof v[1] !== "number") throw new TrimError("ring_vertex");
    ring.push([v[1], v[0]]);   // GeoJSON is [lon, lat]
  }
  return ring;
}

export function trimAlerts(doc: unknown): AlertItem[] {
  const fs = (doc as { features?: unknown } | null)?.features;
  if (!Array.isArray(fs)) throw new TrimError("no_features");
  return fs.map((f) => {
    const a = props(f);
    if (typeof a.event !== "string") throw new TrimError("event");
    const item: AlertItem = {
      event: a.event,
      // `||`, not `??`: trim_nws.py writes `a.get("onset") or a.get("effective")`, and Python's `or` skips an empty string.
      startUtc: utcOf(a.onset || a.effective),
      endUtc: utcOf(a.ends || a.expires),
    };
    if (!RANKED.has(a.event)) return item;
    item.messageType = typeof a.messageType === "string" ? a.messageType : "";
    item.expiresUtc = utcOf(a.expires);
    item.expiresIso = a.expires;
    const ring = ringOf((f as { geometry?: unknown }).geometry);
    if (ring) item.polygon = ring;
    const p = (a.parameters ?? {}) as Record<string, unknown>;
    for (const [k, nws] of PARAMS) {
      const v = p[nws];
      if (Array.isArray(v) && typeof v[0] === "string") item[k] = v[0];
    }
    return item;
  });
}
