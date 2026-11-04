// Blipscope web renderer: a SYNTHETIC scene for the demo page and the smoke test, NOT part of a release's data
// path. It answers the two routes the firmware fetches, in the Worker's shapes, from a deterministic generator:
// about 20 aircraft on straight tracks across a 42 km disc centred on whatever lat/lon the firmware asks for,
// each away long enough each cycle for the radar's retention to drop it (35 s, past its 30 s ceiling), so the
// scene loops. Hex codes sit in the US civil block, clear of every military and non-ICAO range. Callsigns,
// types and routes are illustrative, not recorded flights; registrations are empty.
//
//   mount(canvas, { ..., fetch: makeScene().fetch })   // the page answers instead of a Worker
"use strict";

// [hex, callsign, type, typeName, operator, from, to, altFt, gsKt, hdg, vrateFpm, cat, crossKm, phaseS]
const PLANES = [
  ["a4f2c1", "ASA321", "B739", "Boeing 737-900", "Alaska Airlines", "SEA", "LAX", 36000, 462, 168, 0, 4, 12, 0],
  ["a6b3d2", "SWA2177", "B38M", "Boeing 737 MAX 8", "Southwest Airlines", "PDX", "LAS", 38000, 450, 152, 0, 4, -8, 140],
  ["a1c4e5", "QXE2061", "DH8D", "Dash 8-400", "Horizon Air", "SEA", "RDM", 12500, 265, 162, -1100, 3, 21, 60],
  ["a3d5f7", "SKW5412", "E75L", "Embraer 175", "SkyWest Airlines", "DEN", "RDM", 15000, 300, 284, -1400, 3, 17, 210],
  ["a8e6a9", "UAL1734", "A320", "Airbus A320", "United Airlines", "SFO", "SEA", 35000, 446, 348, 0, 4, -25, 30],
  ["a2f7b4", "DAL1520", "A321", "Airbus A321", "Delta Air Lines", "SLC", "PDX", 37000, 468, 301, 0, 4, 5, 260],
  ["a5a8c6", "FDX1288", "B763", "Boeing 767-300F", "FedEx", "MEM", "PDX", 33000, 474, 292, 0, 6, -15, 110],
  ["a7b9d8", "AAL2490", "A21N", "Airbus A321neo", "American Airlines", "PHX", "SEA", 39000, 455, 336, 0, 4, 30, 320],
  ["a9c1e2", "N731DM", "C172", "Cessna 172", "", "", "", 7500, 108, 75, 0, 2, 3, 400],
  ["a0d2f3", "MED12", "EC35", "Airbus H135", "", "", "", 5800, 122, 210, 0, 8, -4, 150],
  ["a3e4a5", "N412PC", "PC12", "Pilatus PC-12", "", "", "", 17500, 268, 120, 0, 2, -18, 500],
  ["a6f5b7", "EJA512", "C68A", "Cessna Citation Latitude", "NetJets", "SUN", "BFI", 41000, 430, 322, 0, 4, 24, 80],
  ["a8a6c9", "UPS2915", "B748", "Boeing 747-8F", "UPS Airlines", "ANC", "ONT", 40000, 488, 148, 0, 6, -33, 190],
  ["a1b7d1", "N522SR", "SR22", "Cirrus SR22", "", "", "", 9500, 172, 255, 0, 2, 10, 700],
  ["a4c8e3", "QXE2190", "DH8D", "Dash 8-400", "Horizon Air", "RDM", "SEA", 6500, 230, 338, 1800, 3, 23, 300],
  ["a7d9f5", "SKW3307", "CRJ9", "Bombardier CRJ900", "SkyWest Airlines", "SLC", "SEA", 34000, 438, 318, 0, 4, -20, 360],
  ["a9e1a7", "FFT1503", "A20N", "Airbus A320neo", "Frontier Airlines", "DEN", "PDX", 36000, 452, 296, 0, 4, -1, 430],
  ["a2f2b9", "N181CP", "P28A", "Piper Cherokee", "", "", "", 6900, 104, 340, 0, 2, -11, 900],
  ["a5a3c2", "ASA602", "B39M", "Boeing 737 MAX 9", "Alaska Airlines", "SFO", "PAE", 35000, 458, 345, 0, 4, 35, 240],
  ["a8b4d4", "N35KB", "BE35", "Beechcraft Bonanza", "", "", "", 9000, 165, 190, 0, 2, 27, 600],
  ["a0c5e6", "SWA1394", "B737", "Boeing 737-700", "Southwest Airlines", "BOI", "OAK", 34000, 441, 238, 0, 4, -29, 170],
];
const DISC_KM = 42, ABSENT_S = 35, KT_TO_KMS = 1.852 / 3600, KM_PER_DEG = 111.32;

function fixAt(p, t, lat0, lon0) {
  const [, , , , , , , altFt, gsKt, hdg, vrate, , cross, phase] = p;
  const v = gsKt * KT_TO_KMS;
  const half = Math.sqrt(Math.max(1, DISC_KM * DISC_KM - cross * cross));
  const cycle = (2 * half) / v + ABSENT_S;
  const ts = (((t + phase) % cycle) + cycle) % cycle;
  if (ts > (2 * half) / v) return null;
  const s = ts * v - half, h = hdg * Math.PI / 180, ex = Math.sin(h), ny = Math.cos(h);
  const x = -ny * cross + ex * s, y = ex * cross + ny * s;
  let alt = altFt + Math.trunc(vrate * (ts / 60));
  if (alt < 5000) alt = 5000;
  if (alt > 22000 && vrate > 0) alt = 22000;
  return { lat: lat0 + y / KM_PER_DEG, lon: lon0 + x / (KM_PER_DEG * Math.cos(lat0 * Math.PI / 180)), alt };
}

// enrich: true answers /enrich for the scene's hexes; false answers 404 for every one (the smoke test's R4).
export function makeScene({ enrich = true, t0 = Date.now() } = {}) {
  const log = [];
  const enc = new TextEncoder();
  const answer = (status, obj) => ({ status, body: enc.encode(obj === undefined ? "" : JSON.stringify(obj)) });
  function fetch(url) {
    log.push({ url, t: Date.now() });
    const u = new URL(url, "https://page.invalid/");
    if (u.pathname.endsWith("/api/v1/blipscope/blips")) {
      const lat0 = Number(u.searchParams.get("lat")), lon0 = Number(u.searchParams.get("lon"));
      const t = (Date.now() - t0) / 1000, now = Math.floor(Date.now() / 1000);
      const a = [];
      for (const p of PLANES) {
        const f = fixAt(p, t, lat0, lon0);
        if (f) a.push([p[0], p[1], Math.round(f.lat * 1e4), Math.round(f.lon * 1e4), f.alt, p[8], p[9], p[10], p[11], 1]);
      }
      return Promise.resolve(answer(200, { v: 1, t: now, a }));
    }
    const m = u.pathname.match(/\/api\/v1\/blipscope\/enrich\/([0-9a-f]+)$/);
    const p = m && PLANES.find((q) => q[0] === m[1]);
    if (p && enrich) return Promise.resolve(answer(200, { v: 1, r: "", t: p[2], tn: p[3], op: p[4], o: p[5], d: p[6], p: null, pk: "" }));
    return Promise.resolve(answer(404));
  }
  return { fetch, log, planes: PLANES };
}
