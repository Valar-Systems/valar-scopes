// Ellipsoidal Lambert azimuthal equal-area, oblique aspect (Snyder 1987, "Map Projections: A Working Manual",
// eqs 3-11, 3-12, 3-13, 24-13 to 24-17, 24-27 to 24-29). OPERA's composites are on it (docs/wx-opuk.md): the parameters
// are read from each frame's own GeoKeys (opera.ts), never assumed. Pure; test/wx-opera.test.ts holds it to pyproj.

export interface LaeaParams { lat0: number; lon0: number; x0: number; y0: number; a: number; invF: number }

export interface Laea {
  forward(lon: number, lat: number): [number, number];
  inverse(x: number, y: number): [number, number];
}

const RAD = Math.PI / 180;

export function laea(p: LaeaParams): Laea {
  const f = 1 / p.invF;
  const e2 = f * (2 - f), e = Math.sqrt(e2);
  const q = (s: number) => (1 - e2) * (s / (1 - e2 * s * s) - (1 / (2 * e)) * Math.log((1 - e * s) / (1 + e * s)));
  const qp = q(1);
  const Rq = p.a * Math.sqrt(qp / 2);
  const phi1 = p.lat0 * RAD, lam0 = p.lon0 * RAD;
  const s1 = Math.sin(phi1);
  const beta1 = Math.asin(q(s1) / qp);
  const m1 = Math.cos(phi1) / Math.sqrt(1 - e2 * s1 * s1);
  const D = (p.a * m1) / (Rq * Math.cos(beta1));
  const sb1 = Math.sin(beta1), cb1 = Math.cos(beta1);

  return {
    forward(lon, lat) {
      const s = Math.sin(lat * RAD);
      const beta = Math.asin(Math.max(-1, Math.min(1, q(s) / qp)));
      const dl = lon * RAD - lam0;
      const sb = Math.sin(beta), cb = Math.cos(beta), cl = Math.cos(dl);
      const B = Rq * Math.sqrt(2 / (1 + sb1 * sb + cb1 * cb * cl));
      return [p.x0 + B * D * cb * Math.sin(dl), p.y0 + (B / D) * (cb1 * sb - sb1 * cb * cl)];
    },
    inverse(x, y) {
      const X = x - p.x0, Y = y - p.y0;
      const rho = Math.sqrt((X / D) ** 2 + (D * Y) ** 2);
      if (rho === 0) return [p.lon0, p.lat0];
      const ce = 2 * Math.asin(rho / (2 * Rq));
      const sc = Math.sin(ce), cc = Math.cos(ce);
      const qv = qp * (cc * sb1 + (D * Y * sc * cb1) / rho);
      const lam = lam0 + Math.atan2(X * sc, D * rho * cb1 * cc - D * D * Y * sb1 * sc);
      let phi = Math.asin(qv / 2);
      for (let i = 0; i < 20; i++) {                    // Snyder eq 3-16, converging in a few steps
        const sp = Math.sin(phi);
        const t = 1 - e2 * sp * sp;
        const d = ((t * t) / (2 * Math.cos(phi))) *
          (qv / (1 - e2) - sp / t + (1 / (2 * e)) * Math.log((1 - e * sp) / (1 + e * sp)));
        phi += d;
        if (Math.abs(d) < 1e-14) break;
      }
      return [lam / RAD, phi / RAD];
    },
  };
}

export const WGS84 = { a: 6378137, invF: 298.257223563 };
