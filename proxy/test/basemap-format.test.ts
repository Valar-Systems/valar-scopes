/// <reference types="vite/client" />
// MV and ME (docs/basemap-route.md A1.7, A1.11, A2.2): the BMPK encoder, decoder and validator against Weatherscope's
// fixtures on main. Each test is named by its case id; scripts/sabotage-basemap.mjs grades its exact sets from them.
import { describe, expect, it } from "vitest";
import { type Package, type Status, crc32, decode, encode, svarintBytes, validate } from "../src/basemap/format";
import bend from "../../test/fixtures/wx/basemap/bend.bmpk";
import synMin from "../../test/fixtures/wx/basemap/syn-min.bmpk";
import synUnknown from "../../test/fixtures/wx/basemap/syn-unknown.bmpk";
import synthPy from "../../tools/weatherscope/basemap/synth.py?raw";

// Every negative package on disk, enumerated at build time, so a missing or extra file cannot go unseen.
const negs = import.meta.glob<Uint8Array>("../../test/fixtures/wx/basemap/neg-*.bmpk", { eager: true, import: "default" });

/** A10.11's 25 negative cases, each with its frozen reason. MV binds neg-<x>.bmpk to PK-<x> (A2.2). */
const PK: Record<string, Status> = {
  short: "kHeader", magic: "kMagic", version: "kVersion", flags: "kFlags", total: "kTotal", crc: "kCrc",
  nsec0: "kSectionCount", nsec33: "kSectionCount", bounds: "kSectionBounds", overlap: "kOverlap", gap: "kGap",
  dup: "kDuplicate", noprov: "kNoProvenance", varint6: "kVarint", overlong: "kVarint", walk: "kVertexRange",
  ring2: "kVertexCount", line1: "kVertexCount", count: "kBody", text: "kText", rivername: "kRiverName",
  roadclass: "kRoadClass", terrainlen: "kTerrainLength", terrainN0: "kTerrainLength", reserved: "kReserved",
};
const onDisk = Object.fromEntries(Object.entries(negs).map(([p, b]) => [/neg-([A-Za-z0-9]+)\.bmpk$/.exec(p)![1]!, b]));

describe("MV, the validator", () => {
  it("MV binding: the 25 neg-*.bmpk on disk are exactly A10.11's 25 PK cases (else MV is BLIND)", () => {
    expect(Object.keys(onDisk).sort()).toEqual(Object.keys(PK).sort());
  });
  it("MV/PK-min: syn-min is kOk", () => expect(validate(synMin).status).toBe("kOk"));
  it("MV/PK-unknown: syn-unknown is kOk, and every known section decodes as syn-min's", () => {
    const v = validate(synUnknown);
    expect(v.status).toBe("kOk");
    const known = v.pkg!.sections.filter((s) => !("raw" in s));
    expect(known).toEqual(decode(synMin).sections);
    expect(v.pkg!.sections.filter((s) => "raw" in s).map((s) => [s.type, (s as { raw: Uint8Array }).raw.length])).toEqual([[9, 3]]);
  });
  it("MV/PK-bend: bend.bmpk is kOk", () => expect(validate(bend).status).toBe("kOk"));
  for (const [name, want] of Object.entries(PK)) {
    it(`MV/PK-${name}: refused as ${want}`, () => {
      const b = onDisk[name];
      expect(b, `neg-${name}.bmpk is missing`).toBeDefined();
      expect(validate(b!).status).toBe(want);
    });
  }
});

// ------------------------------------------------------------------------------------------------ ME

/** syn-min's contents, read from synth.py's own data lines (the OTHER side of the contract), not transcribed. */
function synMinFromSynthPy(): Package {
  const line = (name: string) => {
    const m = new RegExp(`^${name}\\s*=\\s*(.+)$`, "m").exec(synthPy);
    if (!m) throw new Error(`synth.py has no ${name} line (ME1b is BLIND)`);
    // a trailing "# comment" (synth.py's LAT0 line has one); none of these literals holds a '#' in a string
    return m[1]!.replace(/\s+#[^"]*$/, "").trim();
  };
  // Python literal -> JSON: tuples as arrays, digit separators removed, list(range(n)) written out
  const py = (s: string) => JSON.parse(s.replace(/list\(range\((\d+)\)\)/g, (_, n) => JSON.stringify([...Array(Number(n)).keys()]))
    .replace(/(\d)_(?=\d)/g, "$1").replace(/\(/g, "[").replace(/\)/g, "]"));
  const [lat0, lon0] = py(`[${line("LAT0, LON0")}]`) as number[];
  const [rMm, tolMm] = py(`[${line("R_MM, TOL_MM")}]`) as number[];
  const ring = py(line("RING")) as [number, number][];
  const names = py(line("RIVER_NAMES")) as string[];
  const [rIdx, rPts] = py(line("RIVER")) as [number, [number, number][]];
  const [cls, roadPts] = py(line("ROAD")) as [1 | 2, [number, number][]];
  const [pkx, pky, pelev, pname] = py(line("PEAK")) as [number, number, number, string];
  const [plx, ply, ppop, plname] = py(line("PLACE")) as [number, number, number, string];
  const [lvR, lvN, lvVals] = py(line("LEVEL")) as [number, number, number[]];
  const prov = py(line("PROV")) as string[];
  // the section order, read from synth.py's sections()
  const order = [.../def sections\([\s\S]*?return \[([\s\S]*?)\n {4}\]/.exec(synthPy)![1]!.matchAll(/\((WATER|RIVERS|ROADS|PEAKS|PLACES|TERRAIN|PROVENANCE),/g)].map((m) => m[1]);
  expect(order).toEqual(["WATER", "RIVERS", "ROADS", "PEAKS", "PLACES", "TERRAIN", "PROVENANCE"]);
  return {
    lat0Micro: lat0!, lon0Micro: lon0!, radiusMm: rMm!, toleranceMm: tolMm!,
    sections: [
      { type: 1, rings: [ring] },
      { type: 2, names, lines: [{ name: rIdx, pts: rPts }] },
      { type: 3, roads: [{ cls, pts: roadPts }] },
      { type: 4, peaks: [{ q: [pkx, pky], elevM: pelev, name: pname }] },
      { type: 5, places: [{ q: [plx, ply], pop: ppop, name: plname }] },
      { type: 6, levelRadiusMm: lvR!, n: lvN!, values: Uint8Array.from(lvVals!) },
      { type: 7, text: new TextEncoder().encode(prov.join("\n")), lines: prov.length },
    ],
  };
}

/** The first offset where two byte arrays differ, or -1. */
function firstDiff(a: Uint8Array, b: Uint8Array): number {
  for (let i = 0; i < Math.max(a.length, b.length); i++) if (a[i] !== b[i]) return i;
  return -1;
}

describe("ME, the encoder", () => {
  it("ME1: encode(decode(f)) is f, byte for byte, for syn-min, syn-unknown and bend", () => {
    for (const f of [synMin, synUnknown, bend]) expect(firstDiff(encode(decode(f)), f)).toBe(-1);
  });
  it("ME1b: encode() of syn-min's contents as synth.py writes them is syn-min.bmpk, byte for byte", () => {
    const mine = encode(synMinFromSynthPy());
    expect(firstDiff(mine, synMin)).toBe(-1);
    // CONTROL (ruled 2026-10-28, (a)): against a copy with one body byte changed and its CRC NOT recomputed, the
    // comparator finds that byte, at 271. (As first frozen, the CRC was recomputed too, and the comparator rightly
    // reported 28, the CRC field: that reading is kept on record beside this one.)
    const copy = synMin.slice();
    const at = copy.length - 3;
    expect(at).toBe(271);
    copy[at] = copy[at]! ^ 0x01;
    expect(firstDiff(mine, copy)).toBe(271);
  });
  it("ME5: crc32('123456789') is 0xCBF43926", () => {
    expect(crc32(new TextEncoder().encode("123456789"))).toBe(0xcbf43926);
  });
  it("ME6: svarint encodings", () => {
    const hex = (v: number) => [...svarintBytes(v)].map((x) => x.toString(16).padStart(2, "0")).join(" ");
    expect([0, 63, -64, 64, 32767, -32767].map(hex)).toEqual(["00", "7e", "7f", "80 01", "fe ff 03", "fd ff 03"]);
  });
});
