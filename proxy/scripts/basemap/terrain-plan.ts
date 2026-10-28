// Which Terrarium z9 tiles the store must mirror (docs/basemap-route.md §2.2): every tile that some covered
// origin could read. That is the square of half-width `reachKm`, in format §2's metric, around every covered
// two-decimal lattice point, plus format §5.6's one-tile margin. PURE.
//
// reachKm is the largest of the request's bounds that reads tiles: a level's radius (at most 250 km, §3) and
// R for the peaks' elevations (at most 222 km). The tile of a point follows RD21, ruled for both builders.

const PI = Math.PI;
export const z9X = (lon: number) => ((lon + 180) / 360) * 2 ** 17;
export const z9Y = (lat: number) => ((1 - Math.log(Math.tan(PI / 4 + (lat * PI / 180) / 2)) / PI) / 2) * 2 ** 17;
const MERC_MAX_LAT = 85.0511287798066;

/** The set of tiles, as x * 512 + y, for a coverage lattice (row h -> covered [c0, c1] intervals, hundredths). */
export function tilesForCoverage(rows: Map<number, Array<[number, number]>>, reachKm: number): Set<number> {
  const seen = new Uint8Array(512 * 512);
  for (const [h, iv] of rows) {
    const lat0 = h / 100;
    const c0 = Math.max(Math.cos(lat0 * (PI / 180)), 0.01);
    const dLat = reachKm / 111.0, dLon = reachKm / (111.0 * c0);
    const latN = Math.min(lat0 + dLat, MERC_MAX_LAT), latS = Math.max(lat0 - dLat, -MERC_MAX_LAT);
    const yN = Math.floor(z9Y(latN) / 256) - 1, yS = Math.floor(z9Y(latS) / 256) + 1;
    for (const [a, b] of iv) {
      const xW = Math.floor(z9X(a / 100 - dLon) / 256) - 1, xE = Math.floor(z9X(b / 100 + dLon) / 256) + 1;
      for (let x = xW; x <= xE; x++) {
        const xx = ((x % 512) + 512) % 512; // the antimeridian: the Aleutians reach past 180°
        for (let y = Math.max(0, yN); y <= Math.min(511, yS); y++) seen[xx * 512 + y] = 1;
      }
    }
  }
  const out = new Set<number>();
  for (let i = 0; i < seen.length; i++) if (seen[i]) out.add(i);
  return out;
}
