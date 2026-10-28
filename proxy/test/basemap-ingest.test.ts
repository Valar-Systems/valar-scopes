// MI, the basemap ingest: docs/basemap-route.md A1.7 (frozen before this code) and A1.11 (MI4's keys).
// Each test is named by its case id; scripts/sabotage-basemap.mjs reads the names to grade its exact sets.
import { describe, expect, it } from "vitest";
import {
  assignCells, bboxOf, decodeFeature, encodeFeature, featuresFrom, joinPopulation, pointsFrom, publishStore, reachOf,
  readDelimited, storeIdOf, verifyStore, writeManifest, type Manifest, type StoreBucket,
} from "../scripts/basemap/ingest-lib";
import { readShapefile, shapefileFromZip } from "../scripts/basemap/shapefile";
import { writeDbf, writeShp, zipShapefile } from "./basemap-shp";

const sq = (x: number, y: number, s = 0.01) => [x, y, x, y + s, x + s, y + s, x + s, y, x, y]; // clockwise: an outer ring

function shapefile(type: 3 | 5, parts: number[][][], fields: Array<{ name: string; len: number }>, rows: Array<Record<string, string>>) {
  const zip = zipShapefile("t", writeShp(parts.map((p) => ({ type, parts: p }))), writeDbf(fields, rows));
  const { shp, dbf } = shapefileFromZip(zip);
  return readShapefile(shp, dbf);
}

describe("MI, the ingest", () => {
  it("MI1: the store's predicates are a superset; the Bus rule is left to the builder", () => {
    const water = shapefile(5, [[sq(0, 0)], [sq(1, 1)], [sq(2, 2)]], [{ name: "AWATER", len: 14 }],
      [{ AWATER: "1499999" }, { AWATER: "1500000" }, { AWATER: "2000000" }]);
    const rivers = shapefile(3, [[[0, 0, 1, 1]], [[0, 0, 1, 2]], [[0, 0, 2, 1]]], [{ name: "FULLNAME", len: 40 }],
      [{ FULLNAME: "A Riv" }, { FULLNAME: "Riverside Dr" }, { FULLNAME: "" }]);
    const roads = shapefile(3, [[[0, 0, 1, 1]], [[0, 0, 1, 2]], [[0, 0, 2, 1]]], [{ name: "MTFCC", len: 5 }, { name: "FULLNAME", len: 40 }],
      [{ MTFCC: "S1100", FULLNAME: "US Hwy 97 Bus" }, { MTFCC: "S1200", FULLNAME: "OR 126" }, { MTFCC: "S1400", FULLNAME: "Main St" }]);
    const gnis = readDelimited("feature_id|feature_name|feature_class|prim_lat_dec|prim_long_dec\n1|Mount Test|Summit|1.5|2.5\n2|Test Lake|Lake|1.5|2.5\n", "|");
    const gaz = readDelimited("USPS|GEOID|NAME|LSAD|INTPTLAT|INTPTLONG   \nOR|4105800|Test city|25|1.25|2.25  \nOR|4199999|Test CDP|57|1.25|2.25\n", "|");
    const w = featuresFrom("water", 0, water), r = featuresFrom("rivers", 1, rivers), rd = featuresFrom("roads", 2, roads);
    const pk = pointsFrom("peaks", 3, gnis), pl = pointsFrom("places", 4, gaz, new Map([["4105800", 1234]]));
    expect(w.map((f) => f.attrs["AWATER"])).toEqual(["1500000", "2000000"]);
    expect(r.map((f) => f.attrs["FULLNAME"])).toEqual(["A Riv"]);
    expect(rd.map((f) => [f.attrs["MTFCC"], f.attrs["FULLNAME"]])).toEqual([["S1100", "US Hwy 97 Bus"], ["S1200", "OR 126"]]);
    expect(pk.map((f) => f.attrs["feature_name"])).toEqual(["Mount Test"]);
    expect(pl.map((f) => [f.attrs["NAME"], f.attrs["pop"]])).toEqual([["Test city", 1234]]);
  });

  it("MI2: population joins on STATE + PLACE from SUMLEV 162 rows only; no row is 0", () => {
    const csv = "SUMLEV,STATE,COUNTY,PLACE,COUSUB,CONCIT,PRIMGEO_FLAG,FUNCSTAT,NAME,STNAME,POPESTIMATE2024\n"
      + "162,41,000,05800,00000,00000,0,A,\"Test city, part\",Oregon,100000\n"
      + "157,41,017,05800,00000,00000,0,A,Test city (pt.),Oregon,77\n";
    const pops = joinPopulation(csv);
    expect(pops.get("4105800")).toBe(100000);
    expect(pops.size).toBe(1);
    const gaz = readDelimited("GEOID|NAME|LSAD|INTPTLAT|INTPTLONG\n4105800|Test city|25|1|2\n4100001|No row city|25|1|2\n", "|");
    expect(pointsFrom("places", 0, gaz, pops).map((f) => f.attrs["pop"])).toEqual([100000, 0]);
  });

  it("MI3: a stored vertex is the source's double, bit for bit", () => {
    const x = 0.1 + 0.2; // 0.30000000000000004
    const recs = shapefile(3, [[[x, -x, x * 3, 1 / 3]]], [{ name: "FULLNAME", len: 10 }], [{ FULLNAME: "B Riv" }]);
    const f = featuresFrom("rivers", 0, recs)[0]!;
    const bits = (v: number) => new DataView(new Float64Array([v]).buffer).getBigUint64(0, true);
    expect([...f.xy].map(bits)).toEqual([x, -x, x * 3, 1 / 3].map(bits));
    const enc = encodeFeature(f), dv = new DataView(enc.buffer);
    const n = dv.getUint32(enc.length - 4 * 8 - 4, true);
    expect(n).toBe(2);
    expect(bits(dv.getFloat64(enc.length - 4 * 8, true))).toBe(bits(x));
  });

  it("MI4: the manifest's bytes: sorted keys, one-space indent, a final newline, exactly the ruled keys", async () => {
    const m: Manifest = {
      predicates: ["p"], builder: "1".repeat(40),
      files: [
        { vintage: "2024", url: "https://example.invalid/b.zip", sha256: "4".repeat(64), layer: "rivers", bytes: 2 },
        { url: "https://example.invalid/a.zip", layer: "counties", bytes: 1, sha256: "3".repeat(64), vintage: "2024" },
      ],
    };
    const t = writeManifest(m);
    expect(t.endsWith("}\n")).toBe(true);
    expect(t.split("\n")[1]).toBe(' "builder": "' + "1".repeat(40) + '",');
    expect(t.indexOf("a.zip")).toBeLessThan(t.indexOf("b.zip"));
    expect(t).toContain('\n  {\n   "bytes": 1,\n   "layer": "counties",\n   "sha256": "');
    expect(writeManifest(JSON.parse(t))).toBe(t);
    const id = await storeIdOf(t);
    expect(id).toMatch(/^[0-9a-f]{12}$/);
    expect(await storeIdOf(writeManifest(m))).toBe(id);
    expect(() => writeManifest({ ...m, extra: 1 } as unknown as Manifest)).toThrow(/manifest keys/);
    expect(() => writeManifest({ ...m, files: [{ ...m.files[0]!, mtime: 1 } as never] })).toThrow(/file keys/);
  });

  it("MI4b: the optional notes key (re-registered 2026-10-28), beside MI4's frozen row", async () => {
    const m: Manifest = {
      predicates: ["p"], builder: "1".repeat(40),
      files: [
        { vintage: "2024", url: "https://example.invalid/b.zip", sha256: "4".repeat(64), layer: "rivers", bytes: 2 },
        { url: "https://example.invalid/a.zip", layer: "counties", bytes: 1, sha256: "3".repeat(64), vintage: "2024" },
      ],
    };
    const a = "https://example.invalid/a.zip: fetched from the Census Bureau's mirror";
    const b = "https://example.invalid/b.zip: fetched from the Census Bureau's mirror";
    // (a) no notes: byte-identical to MI4's output (the same manifest, written the same way)
    const plain = writeManifest(m);
    expect(plain).not.toContain('"notes"');
    expect(plain.split("\n").slice(0, 2)).toEqual(["{", ' "builder": "' + "1".repeat(40) + '",']);
    // (b) one note: keys builder, files, notes, predicates; printed as given; round trip; a different store id
    const t = writeManifest({ ...m, notes: [a] });
    const keys = [...t.matchAll(/^ "([a-z]+)":/gm)].map((k) => k[1]);
    expect(keys).toEqual(["builder", "files", "notes", "predicates"]);
    expect(t).toContain(` "notes": [\n  ${JSON.stringify(a)}\n ],`);
    expect(writeManifest(JSON.parse(t))).toBe(t);
    expect(await storeIdOf(t)).not.toBe(await storeIdOf(plain));
    expect(writeManifest({ ...m, notes: [a, b] })).toContain(JSON.stringify(b));
    // (c) unsorted: refused, not sorted for the caller
    expect(() => writeManifest({ ...m, notes: [b, a] })).toThrow(/sorted/);
    // (d) an unknown top-level key: refused
    expect(() => writeManifest({ ...m, notes: [a], extra: 1 } as unknown as Manifest)).toThrow(/manifest keys/);
    // (e) present but empty: refused
    expect(() => writeManifest({ ...m, notes: [] })).toThrow(/absent, never empty/);
    // (f) a URL not among the files; two notes for one URL
    expect(() => writeManifest({ ...m, notes: ["https://example.invalid/c.zip: mirrored"] })).toThrow(/listed url/);
    expect(() => writeManifest({ ...m, notes: [a, a + " again"] })).toThrow(/two manifest notes/);
    // (g) a wall-clock time in the reason
    expect(() => writeManifest({ ...m, notes: ["https://example.invalid/a.zip: mirrored on 2026-10-28"] })).toThrow(/wall-clock/);
    expect(() => writeManifest({ ...m, notes: ["https://example.invalid/a.zip: mirrored at 04:00"] })).toThrow(/wall-clock/);
    // (h) not "<url>: <why>"
    expect(() => writeManifest({ ...m, notes: ["https://example.invalid/a.zip mirrored"] })).toThrow(/listed url/);
    expect(() => writeManifest({ ...m, notes: ["https://example.invalid/a.zip:  "] })).toThrow(/no reason/);
  });

  it("MI5: a feature crossing a cell border is stored once, in its south-west corner's cell", () => {
    const xy = new Float64Array([-0.5, 0.5, 0.5, 1.5]); // crosses lon 0 and lat 1
    const f = featuresFrom("rivers", 0, [{ index: 0, parts: [xy], attrs: { FULLNAME: "C Riv" } }])[0]!;
    const cells = assignCells([{ bytes: encodeFeature(f), box: bboxOf(f.xy) }], 1e9);
    expect(cells.map((c) => [c.key, c.features.length])).toEqual([["0/0/-1", 1]]);
    expect(reachOf(cells[0]!)).toEqual([-0.5, 0.5, 0.5, 1.5]);
  });

  it("MI6: a cell over the threshold splits into quarters until none is; a single feature never splits", () => {
    const feats = [];
    for (let i = 0; i < 40; i++) {
      const xy = new Float64Array([0.01 + (i % 8) * 0.12, 0.01 + Math.floor(i / 8) * 0.19, 0.02 + (i % 8) * 0.12, 0.02 + Math.floor(i / 8) * 0.19]);
      const f = featuresFrom("rivers", 0, [{ index: i, parts: [xy], attrs: { FULLNAME: "D Riv" } }])[0]!;
      feats.push({ bytes: encodeFeature(f), box: bboxOf(f.xy) });
    }
    const big = new Float64Array(400).fill(0.5);
    const lone = featuresFrom("rivers", 0, [{ index: 99, parts: [big], attrs: { FULLNAME: "E Riv" } }])[0]!;
    feats.push({ bytes: encodeFeature(lone), box: bboxOf(lone.xy) });
    const cells = assignCells(feats, 1000);
    expect(cells.reduce((a, c) => a + c.features.length, 0)).toBe(41);
    for (const c of cells) expect(c.bytes <= 1000 || c.features.length === 1).toBe(true);
    expect(cells.some((c) => c.bytes > 1000 && c.features.length === 1)).toBe(true);
    expect(cells.some((c) => c.level > 0)).toBe(true);
  });

  it("MI-column (not a frozen row): a MISSING column is refused, an empty value is not", () => {
    // The 2025 Gazetteer is pipe-delimited; read as tab-delimited, every row had one column and LSAD was absent,
    // and the old filter kept 0 of 32,350 places without a word.
    const asTabs = readDelimited("USPS|GEOID|NAME|LSAD|INTPTLAT|INTPTLONG\nOR|4105800|Test city|25|1|2\n", "\t");
    expect(() => pointsFrom("places", 0, asTabs)).toThrow(/no LSAD column/);
    expect(() => featuresFrom("rivers", 0, [{ index: 0, parts: [new Float64Array([0, 0, 1, 1])], attrs: { NAME: "x" } }])).toThrow(/no FULLNAME column/);
    expect(featuresFrom("rivers", 0, [{ index: 0, parts: [new Float64Array([0, 0, 1, 1])], attrs: { FULLNAME: "" } }])).toEqual([]); // CONTROL
  });

  it("MI-roundtrip (not a frozen row): decodeFeature inverts encodeFeature for every kind", () => {
    const feats = [
      ...featuresFrom("water", 7, [{ index: 3, parts: [new Float64Array(sq(0.5, 0.5))], attrs: { AWATER: "2000000" } }]),
      ...featuresFrom("roads", 8, [{ index: 4, parts: [new Float64Array([0, 0, 1, 1])], attrs: { MTFCC: "S1200", FULLNAME: "Rue Élan" } }]),
      ...pointsFrom("peaks", 9, readDelimited("feature_id|feature_name|feature_class|prim_lat_dec|prim_long_dec\n42|Mount Test|Summit|1.5|2.5\n", "|")),
      ...pointsFrom("places", 10, readDelimited("GEOID|NAME|LSAD|INTPTLAT|INTPTLONG\n4105800|Test city|25|1|2\n", "|"), new Map([["4105800", 5]])),
    ];
    const buf = new Uint8Array(feats.reduce((a, f) => a + encodeFeature(f).length, 0));
    let o = 0;
    for (const f of feats) { const e = encodeFeature(f); buf.set(e, o); o += e.length; }
    const back = [];
    for (let p = 0; p < buf.length;) { const { f, end } = decodeFeature(buf, p); back.push(f); p = end; }
    expect(back.map((f) => [f.kind, f.file, f.record, f.part, [...f.xy]])).toEqual(feats.map((f) => [f.kind, f.file, f.record, f.part, [...f.xy]]));
    expect(back[1]!.attrs).toEqual({ MTFCC: "S1200", FULLNAME: "Rue Élan" });
    expect(back[2]!.attrs).toEqual({ feature_id: 42, feature_name: "Mount Test" });
    expect(back[3]!.attrs).toEqual({ GEOID: "4105800", NAME: "Test city", pop: 5 });
    expect(() => decodeFeature(buf.subarray(0, buf.length - 1), encodeFeature(feats[0]!).length + encodeFeature(feats[1]!).length + encodeFeature(feats[2]!).length)).toThrow(/runs past/);
  });

  describe("MI7: write -> verify -> repair -> verify -> seal", () => {
    function bucket(opts: { dropFirstPutOf?: string; corrupt?: string } = {}) {
      const m = new Map<string, Uint8Array>(), writes: string[] = [];
      let dropped = false;
      const b: StoreBucket = {
        async put(k, v) {
          writes.push(k);
          if (k === opts.dropFirstPutOf && !dropped) { dropped = true; return; }
          const copy = v.slice();
          if (k === opts.corrupt) copy[0] = copy[0]! ^ 0xff; // same size, different bytes
          m.set(k, copy);
        },
        async get(k) { return m.get(k) ?? null; },
      };
      return { b, m, writes };
    }
    const objs = [{ key: "bm/v1/x/a", bytes: new Uint8Array([1, 2, 3]) }, { key: "bm/v1/x/b", bytes: new Uint8Array([4, 5, 6]) }];
    const cur = new TextEncoder().encode('{"storeId":"x"}');

    it("MI7 (a): an intact store is sealed", async () => {
      const { b, m } = bucket();
      expect((await publishStore(b, objs, "bm/v1/current", cur)).sealed).toBe(true);
      expect(m.has("bm/v1/current")).toBe(true);
    });
    it("MI7 (b): a lost write fails the first verify, is repaired, and only then sealed", async () => {
      const { b, writes } = bucket({ dropFirstPutOf: "bm/v1/x/b" });
      expect((await verifyStore(b, objs)).missing).toEqual(["bm/v1/x/a", "bm/v1/x/b"]); // nothing written yet
      const r = await publishStore(b, objs, "bm/v1/current", cur);
      expect(r).toMatchObject({ sealed: true, rounds: 1 });
      expect(writes.indexOf("bm/v1/current")).toBeGreaterThan(writes.lastIndexOf("bm/v1/x/b"));
    });
    it("MI7 (c): a same-size corruption is caught by sha256 and the store is not sealed", async () => {
      const { b, m } = bucket({ corrupt: "bm/v1/x/a" });
      const r = await publishStore(b, objs, "bm/v1/current", cur);
      expect(r.sealed).toBe(false);
      expect(r.mismatched).toEqual(["bm/v1/x/a"]);
      expect(m.has("bm/v1/current")).toBe(false);
    });
    it("MI7 (d): current is the last write", async () => {
      const { b, writes } = bucket();
      await publishStore(b, objs, "bm/v1/current", cur);
      expect(writes[writes.length - 1]).toBe("bm/v1/current");
      expect(writes.filter((k) => k === "bm/v1/current").length).toBe(1);
    });
  });
});
