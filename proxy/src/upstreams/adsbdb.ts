import { USER_AGENT } from "./types";

// ROUTE FALLBACK ONLY. adsb.lol's routeset API is the primary route source, but
// as of 2026-07-08 it returns "201 Created" with an EMPTY body even for major
// scheduled callsigns (its OpenAPI docs promise 200 + JSON) -- effectively no
// data. adsbdb.com is the route source the firmware used for years and remains
// reliable, so it earns its place back for callsign->route specifically.
// Aircraft metadata stays on adsb.lol. Drop this again when routeset stabilizes
// (flip ROUTE_ADSBDB_ENABLED to "false").
export function adsbdbRouteUrl(callsign: string): string {
  return `https://api.adsbdb.com/v0/callsign/${callsign}`;
}

export function adsbdbHeaders(): Record<string, string> {
  return { "User-Agent": USER_AGENT };
}

// Aircraft metadata by hex. Used as a TYPE BACKFILL only: our failover feed
// (airplanes.live) returns a hex's registration but not its ICAO type, so a
// failover away from adsb.lol loses the type -- and with it the type name and
// the type-keyed stock photo. adsbdb is a different provider/host, so it isn't
// subject to the same shared-egress 429 that pushes us off adsb.lol.
export function adsbdbAircraftUrl(hex: string): string {
  return `https://api.adsbdb.com/v0/aircraft/${hex}`;
}

export interface AdsbdbAircraft {
  r: string; // registration
  t: string; // ICAO type designator
  tn: string; // adsbdb's verbose type description (fallback friendly name)
}

// Parse an adsbdb /v0/aircraft body. Returns null for unrecognized shapes or an
// all-empty record (nothing worth backfilling).
export function parseAdsbdbAircraft(json: unknown): AdsbdbAircraft | null {
  const root = json as Record<string, unknown> | null;
  if (root === null || typeof root !== "object") return null;
  const response = root.response;
  if (response === null || typeof response !== "object") return null;
  const aircraft = (response as Record<string, unknown>).aircraft;
  if (aircraft === null || typeof aircraft !== "object") return null;
  const a = aircraft as Record<string, unknown>;
  const t = typeof a.icao_type === "string" ? a.icao_type.trim().toUpperCase() : "";
  const r = typeof a.registration === "string" ? a.registration.trim() : "";
  const tn = typeof a.type === "string" ? a.type.trim() : "";
  if (!t && !r && !tn) return null;
  return { r, t, tn };
}

export interface AdsbdbRoute {
  o: string;
  d: string;
  known: boolean; // false = adsbdb answered definitively "unknown callsign"
}

// Parse an adsbdb /v0/callsign body. Returns null only for shapes we don't
// recognize at all; an explicit "unknown callsign" is a definitive negative.
export function parseAdsbdbRoute(json: unknown): AdsbdbRoute | null {
  const root = json as Record<string, unknown> | null;
  if (root === null || typeof root !== "object") return null;
  const response = root.response;
  if (typeof response === "string") return { o: "", d: "", known: false }; // "unknown callsign"
  if (response === null || typeof response !== "object") return null;

  const flightroute = (response as Record<string, unknown>).flightroute;
  if (flightroute === null || typeof flightroute !== "object") return { o: "", d: "", known: false };

  const airportCode = (airport: unknown): string => {
    const a = airport as Record<string, unknown> | null;
    if (a === null || typeof a !== "object") return "";
    if (typeof a.iata_code === "string" && a.iata_code) return a.iata_code;
    if (typeof a.icao_code === "string" && a.icao_code) return a.icao_code;
    return "";
  };

  const fr = flightroute as Record<string, unknown>;
  return { o: airportCode(fr.origin), d: airportCode(fr.destination), known: true };
}
