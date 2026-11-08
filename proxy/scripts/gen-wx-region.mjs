#!/usr/bin/env node
/**
 * gen-wx-region.mjs -- the region table: which OPERA product answers /wx/cref at each 0.01 deg lattice point
 * (docs/wx-opera-europe.md §2, docs/wx-opera-code-stage.md §1b). Writes src/wx/region.generated.ts, which
 * src/wx/region.ts reads.
 *
 *   npx tsx scripts/gen-wx-region.mjs --ne <ne_10m_admin_0_countries.geojson>            regenerate
 *   npx tsx scripts/gen-wx-region.mjs --ne <ne_10m_admin_0_countries.geojson> --check    refuse any difference (G1)
 *
 * THE INPUT is Natural Earth's 1:10m admin-0 countries, v5.1.2, the file the coverage survey graded (A3.23), and a
 * file whose sha256 is not NE_SHA is refused. It is 13 MB and not committed, so --check runs where the file is, not
 * in CI; CI holds the committed table to its header and to fixtures shapely chose (test/wx-region.test.ts).
 *
 * THE RASTER a polygon part is tested against is read from a real OPERA head, the committed DRY fixture, by the
 * production parser (src/wx/opera.ts), so the table and the ingest agree on where the composite is. A part with no
 * vertex inside it is dropped, and the per-country drops must equal the plan's table (§1), or the generator refuses.
 *
 * THE TEST is the pixel-centre rule on the lattice point itself, even-odd on each part's rings, with the point
 * excluded when it lies exactly on a crossing: the quantised point (c/100, r/100) is the only thing a request can
 * be (src/wx/crop.ts quantise()). Today's UK land box is added whole (Q-UKBOX), and each served point takes the
 * first product rule that holds it (products.ts, in order). Every bound is compared in integer hundredths.
 *
 * EVERY RULE'S EDGES ARE CLOSED (Q-EDGE, ruled 2026-11-08 ~02:33Z): a point on an edge two rules share goes to the
 * first of them in products.ts's order. Lithuania's quantised label point sits on 55.10 N, central's north edge, and is
 * opce's. Open edges are not an option on a lattice: every point on a shared edge (63.00 N, for one) would have no rule.
 *
 * Sabotage switches, for the D1s, D3s-a, D4s, D5s and F2s rows only. The header records them, and G1 refuses a
 * table whose header names any:
 *   --drop <ADMIN>     leave one of the 40 out             --add <ADMIN>    put another country in
 *   --allow-leftover   keep going when a served point has no rule (by default the generator refuses)
 *   --no-ukbox         leave today's UK land box out        --out <path>     write somewhere other than the module
 */
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { HEAD_BYTES, parseHead } from "../src/wx/opera.ts";
import { laea } from "../src/wx/laea.ts";
import { OPUK_LAND, PRODUCTS } from "../src/wx/products.ts";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const OUT = join(PROXY, "src", "wx", "region.generated.ts");
const HEAD_FIXTURE = join(PROXY, "test", "fixtures", "wx", "opera", "20261029T0130.ranges.bin");
export const NE_SHA = "239eec57ac17f100a11e2536cffc56752c318b50ae765b0918ff7aab4ce8f255";

/** The 40 (docs/wx-opera-europe.md §1): A3.23's 36 at 90 % or more, and the four under 100 pixels, as Natural
 *  Earth's ADMIN names. */
export const COUNTRIES = [
  "Aland", "Andorra", "Austria", "Belgium", "Bosnia and Herzegovina", "Croatia", "Czechia", "Denmark", "Estonia",
  "Finland", "France", "Germany", "Gibraltar", "Guernsey", "Hungary", "Iceland", "Ireland", "Isle of Man", "Jersey",
  "Latvia", "Liechtenstein", "Lithuania", "Luxembourg", "Malta", "Moldova", "Monaco", "Montenegro", "Netherlands",
  "Norway", "Poland", "Portugal", "Republic of Serbia", "Romania", "San Marino", "Slovakia", "Slovenia", "Spain",
  "Sweden", "Switzerland", "United Kingdom",
];
/** §1's parts outside the raster, per country. Every other of the 40 drops none. */
const EXPECTED_DROPS = { France: 11, Norway: 23, Spain: 8, Portugal: 14, Netherlands: 3 };

function die(msg) { console.error(`gen-wx-region: REFUSED -- ${msg}`); process.exit(1); }

// ---- arguments ---------------------------------------------------------------------------------------------------

const argv = process.argv.slice(2);
const opt = { ne: null, check: false, out: OUT, drop: [], add: [], allowLeftover: false, ukBox: true };
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  const val = () => { const v = argv[++i]; if (v === undefined) die(`${a} needs a value`); return v; };
  if (a === "--ne") opt.ne = val();
  else if (a === "--check") opt.check = true;
  else if (a === "--out") opt.out = resolve(val());
  else if (a === "--drop") opt.drop.push(val());
  else if (a === "--add") opt.add.push(val());
  else if (a === "--allow-leftover") opt.allowLeftover = true;
  else if (a === "--no-ukbox") opt.ukBox = false;
  else die(`unknown argument ${a}`);
}
if (!opt.ne) die("--ne <ne_10m_admin_0_countries.geojson> is required");
const sabotage = [...opt.drop.map((n) => `drop ${n}`), ...opt.add.map((n) => `add ${n}`),
                  ...(opt.allowLeftover ? ["allow-leftover"] : []), ...(opt.ukBox ? [] : ["no-ukbox"])];
if (opt.check && sabotage.length) die("--check compares the committed table, so it takes no sabotage switch");
for (const n of opt.drop) if (!COUNTRIES.includes(n)) die(`--drop ${n}: not one of the 40`);
for (const n of opt.add) if (COUNTRIES.includes(n)) die(`--add ${n}: already one of the 40`);

// ---- inputs ------------------------------------------------------------------------------------------------------

const neBytes = readFileSync(opt.ne);
const neSha = createHash("sha256").update(neBytes).digest("hex");
if (neSha !== NE_SHA) die(`${opt.ne} has sha256 ${neSha}, not Natural Earth v5.1.2's ${NE_SHA}`);
const feat = new Map(JSON.parse(neBytes.toString("utf8")).features.map((f) => [f.properties.ADMIN, f]));

const geo = parseHead(new Uint8Array(readFileSync(HEAD_FIXTURE).subarray(0, HEAD_BYTES)));
const P = laea(geo.proj);
const xE = geo.ulx + geo.width * geo.xs, yS = geo.uly - geo.height * geo.ys;
const inRaster = (lon, lat) => {
  const [x, y] = P.forward(lon, lat);
  return x >= geo.ulx && x <= xE && y <= geo.uly && y >= yS;
};

const ruled = PRODUCTS.filter((p) => p.source === "opera");
if (!ruled.length || ruled.some((p) => !p.rule || !p.grid)) die("every OPERA product needs a rule and a grid");
const H = (deg) => Math.round(deg * 100);
const rules = ruled.map((p) => ({ id: p.id, w: H(p.rule.lonW), s: H(p.rule.latS), e: H(p.rule.lonE), n: H(p.rule.latN) }));

// ---- the parts ---------------------------------------------------------------------------------------------------

const countries = [...COUNTRIES.filter((n) => !opt.drop.includes(n)), ...opt.add];
const parts = [];
const drops = {};
for (const name of countries) {
  const f = feat.get(name);
  if (!f) die(`${name} is not an ADMIN name in the file`);
  const g = f.geometry;
  const polys = g.type === "MultiPolygon" ? g.coordinates : g.type === "Polygon" ? [g.coordinates] : die(`${name}: ${g.type}`);
  for (const rings of polys) {
    const ext = rings[0];
    const inside = ext.filter(([lon, lat]) => inRaster(lon, lat)).length;
    if (inside > 0 && inside < ext.length) die(`${name} has a part straddling the raster's edge (the plan found none)`);
    if (inside) parts.push(rings); else drops[name] = (drops[name] ?? 0) + 1;
  }
}
for (const name of countries) {
  if (!COUNTRIES.includes(name)) continue;          // an --add country has no expected count; it is printed below
  const want = EXPECTED_DROPS[name] ?? 0;
  if ((drops[name] ?? 0) !== want) die(`${name} drops ${drops[name] ?? 0} parts outside the raster, the plan says ${want}`);
}

// ---- the lattice -------------------------------------------------------------------------------------------------

let N = -Infinity, S = Infinity, W = Infinity, E = -Infinity;
for (const rings of parts) for (const [lon, lat] of rings[0]) {
  if (lat > N) N = lat; if (lat < S) S = lat; if (lon < W) W = lon; if (lon > E) E = lon;
}
if (opt.ukBox) { N = Math.max(N, OPUK_LAND.latN); S = Math.min(S, OPUK_LAND.latS); W = Math.min(W, OPUK_LAND.lonW); E = Math.max(E, OPUK_LAND.lonE); }
const row0 = Math.ceil(N * 100), rowS = Math.floor(S * 100), col0 = Math.floor(W * 100);
const nRows = row0 - rowS + 1;
const spans = Array.from({ length: nRows }, () => []);        // per row: [first col, last col] in hundredths

for (const rings of parts) {
  const cross = new Map();                                   // row -> x of every crossing, all rings (even-odd)
  for (const ring of rings) {
    const n = ring.length - (ring[0][0] === ring.at(-1)[0] && ring[0][1] === ring.at(-1)[1] ? 1 : 0);
    for (let i = 0; i < n; i++) {
      const [x1, y1] = ring[i], [x2, y2] = ring[(i + 1) % n];
      if (y1 === y2) continue;
      const lo = Math.min(y1, y2), hi = Math.max(y1, y2);
      for (let r = Math.floor(lo * 100) - 1; r <= Math.ceil(hi * 100) + 1; r++) {
        const lat = r / 100;
        if ((y1 > lat) === (y2 > lat)) continue;               // half-open: crosses iff exactly one end is above
        const x = x1 + ((lat - y1) * (x2 - x1)) / (y2 - y1);
        (cross.get(r) ?? cross.set(r, []).get(r)).push(x);
      }
    }
  }
  for (const [r, xs] of cross) {
    if (xs.length % 2) die(`a ring is not closed (odd crossings at row ${r})`);
    xs.sort((a, b) => a - b);
    for (let k = 0; k < xs.length; k += 2) {
      const xa = xs[k], xb = xs[k + 1];
      let a = Math.floor(xa * 100) - 1, b = Math.ceil(xb * 100) + 1;   // strictly between: xa < c/100 < xb
      while (a / 100 <= xa) a++;
      while (b / 100 >= xb) b--;
      if (a <= b) spans[row0 - r].push([a, b]);
    }
  }
}
if (opt.ukBox) {
  for (let r = H(OPUK_LAND.latS); r <= H(OPUK_LAND.latN); r++) spans[row0 - r].push([H(OPUK_LAND.lonW), H(OPUK_LAND.lonE)]);
}

// ---- the runs: each served point takes the first rule that holds it ------------------------------------------------

const runs = [];                                             // [first col, last col, rule index], absolute hundredths
const rowIndex = new Uint32Array(nRows + 1);
const per = rules.map(() => ({ points: 0, n: -Infinity, s: Infinity, w: Infinity, e: -Infinity }));
let leftover = 0, points = 0;
for (let i = 0; i < nRows; i++) {
  rowIndex[i] = runs.length;
  const r = row0 - i;
  const merged = [];
  for (const [a, b] of spans[i].sort((u, v) => u[0] - v[0])) {
    const last = merged.at(-1);
    if (last && a <= last[1] + 1) last[1] = Math.max(last[1], b); else merged.push([a, b]);
  }
  const active = rules.map((q, k) => ({ ...q, k })).filter((q) => q.s <= r && r <= q.n);
  for (const [a, b] of merged) {
    let c = a;
    while (c <= b) {
      const hit = active.find((q) => q.w <= c && c <= q.e);   // closed on every side, the first rule wins (Q-EDGE)
      if (!hit) {
        const next = Math.min(b + 1, ...active.filter((q) => q.w > c).map((q) => q.w));
        leftover += next - c;
        c = next;
        continue;
      }
      let end = Math.min(b, hit.e);
      for (const q of active) if (q.k < hit.k && q.w > c) end = Math.min(end, q.w - 1);   // an earlier rule takes over there
      const prev = runs.at(-1);
      if (prev && runs.length > rowIndex[i] && prev[2] === hit.k && prev[1] + 1 === c) prev[1] = end;
      else runs.push([c, end, hit.k]);
      const st = per[hit.k];
      st.points += end - c + 1;
      st.n = Math.max(st.n, r); st.s = Math.min(st.s, r); st.w = Math.min(st.w, c); st.e = Math.max(st.e, end);
      points += end - c + 1;
      c = end + 1;
    }
  }
}
rowIndex[nRows] = runs.length;
if (leftover && !opt.allowLeftover) die(`${leftover} served points have no rule (the plan measured a left-over area of 0)`);

// The table's own invariants, before anything is written.
for (let i = 0; i < nRows; i++) {
  for (let j = rowIndex[i]; j < rowIndex[i + 1]; j++) {
    const [a, b, k] = runs[j];
    if (a > b || k < 0 || k >= rules.length) die(`row ${i}: run ${j} is malformed`);
    if (j > rowIndex[i] && a <= runs[j - 1][1]) die(`row ${i}: runs ${j - 1} and ${j} overlap or are unsorted`);
    if (a - col0 < 0 || b - col0 > 0xffff) die(`row ${i}: run ${j} does not fit a Uint16 column`);
  }
}
// Every product's grid holds every point it serves with the margin (2.02 deg; the lattice sits inside the polygons'
// bounds, so 2.01 is the floor). A smaller margin is a crop that runs off the frame.
// Under a sabotage switch these two guards report instead of refusing: a table without Malta has a rule that serves
// nothing, and one with Italy has points past a grid's margin. That is the sabotage, and G1 refuses its header anyway.
const guard = (why) => { if (sabotage.length) console.log(`gen-wx-region: SABOTAGE: would refuse -- ${why}`); else die(why); };
const margins = rules.map((q, k) => {
  const g = ruled[k].grid, st = per[k];
  if (!st.points) { guard(`${q.id}'s rule serves no point`); return { n: NaN, s: NaN, w: NaN, e: NaN }; }
  const m = { n: g.la1 - st.n / 100, s: st.s / 100 - (g.la1 - (g.nj - 1) * g.dj),
              w: st.w / 100 - g.lo1, e: g.lo1 + (g.ni - 1) * g.di - st.e / 100 };
  for (const [side, v] of Object.entries(m)) if (v < 2.01 - 1e-9) guard(`${q.id}'s grid leaves only ${v.toFixed(3)} deg ${side} of its land`);
  return m;
});

// ---- the module --------------------------------------------------------------------------------------------------

const le16 = new Uint8Array(runs.length * 6);
const dv = new DataView(le16.buffer);
runs.forEach(([a, b, k], j) => { dv.setUint16(j * 6, a - col0, true); dv.setUint16(j * 6 + 2, b - col0, true); dv.setUint16(j * 6 + 4, k, true); });
const le32 = new Uint8Array(rowIndex.length * 4);
const dv32 = new DataView(le32.buffer);
rowIndex.forEach((v, i) => dv32.setUint32(i * 4, v, true));
const b64lines = (u8) => {
  const s = Buffer.from(u8).toString("base64"), out = [];
  for (let i = 0; i < s.length; i += 112) out.push(`  "${s.slice(i, i + 112)}",`);
  return out.join("\n");
};
const header = {
  source: "Natural Earth 1:10m admin-0 countries v5.1.2",
  sha256: neSha,
  sabotage: sabotage.length ? sabotage.join(", ") : null,
  countries,
  ukBox: opt.ukBox ? { latS: OPUK_LAND.latS, latN: OPUK_LAND.latN, lonW: OPUK_LAND.lonW, lonE: OPUK_LAND.lonE } : null,
  rules: ruled.map((p) => ({ id: p.id, latS: p.rule.latS, latN: p.rule.latN, lonW: p.rule.lonW, lonE: p.rule.lonE })),
  row0,
  rows: nRows,
  col0,
  runs: runs.length,
  points,
};
const text = `// GENERATED by proxy/scripts/gen-wx-region.mjs -- do not edit. Regenerate it with the script; its --check refuses any
// difference (G1, docs/wx-opera-code-stage.md). Read by src/wx/region.ts only.${sabotage.length ? "\n// SABOTAGED (" + sabotage.join(", ") + "): never commit this file." : ""}
//
// The served region (docs/wx-opera-europe.md §2): the countries below, their Natural Earth parts inside the OPERA
// raster, plus today's UK land box. Each 0.01 deg lattice point is tested by the pixel-centre rule (even-odd) and
// takes the first product rule that holds it; rows and columns are in hundredths of a degree.

export const REGION_HEADER = ${JSON.stringify(header, null, 2)} as const;

/** Per row, from row0 southward, the index of its first run: rows + 1 entries, little-endian Uint32, base64. */
export const REGION_ROW_INDEX: string = [
${b64lines(le32)}
].join("");

/** The runs: (first col, last col, rule index) as little-endian Uint16 triples, columns counted from col0, base64. */
export const REGION_RUNS: string = [
${b64lines(le16)}
].join("");
`;

const fmt = (m) => Object.entries(m).map(([k, v]) => `${k} ${v.toFixed(2)}`).join(" ");
console.log(`gen-wx-region: ${countries.length} countries, ${parts.length} parts kept, dropped ${JSON.stringify(drops)}` +
            `${sabotage.length ? `, SABOTAGE: ${sabotage.join(", ")}` : ""}`);
console.log(`gen-wx-region: ${nRows} rows from ${row0}, col0 ${col0}, ${runs.length} runs, ${points} points, ` +
            `${le16.length} B of runs + ${le32.length} B of row index, leftover ${leftover}`);
rules.forEach((q, k) => console.log(`  ${q.id} ${per[k].points} points; margins ${fmt(margins[k])}`));

if (opt.check) {
  let have;
  try { have = readFileSync(opt.out, "utf8"); } catch { die(`${opt.out} is missing`); }
  if (have !== text) {
    const a = have.split("\n"), b = text.split("\n");
    let i = 0;
    while (i < Math.max(a.length, b.length) && a[i] === b[i]) i++;
    die(`${opt.out} differs from the generator's output at line ${i + 1}`);
  }
  console.log(`gen-wx-region: --check: ${opt.out} is byte-identical`);
} else {
  writeFileSync(opt.out, text);
  console.log(`gen-wx-region: wrote ${opt.out} (${text.length} B)`);
}
