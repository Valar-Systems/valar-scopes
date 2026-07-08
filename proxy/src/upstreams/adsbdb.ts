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
