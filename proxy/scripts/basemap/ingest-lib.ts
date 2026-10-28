// The basemap ingest's pure core (docs/basemap-route.md §2.2-§2.3; A1.7's MI family binds to these names).
// PURE: Web APIs and fflate only, so the workers pool runs MI1-MI7. The CLI (scripts/ingest-basemap.ts)
// supplies bytes from the fetch cache and writes what this returns.
//
// The store is a SUPERSET filter of format §5, by attribute only. The builder (src/basemap/layers.ts) re-applies
// every rule whose column the store keeps -- AWATER, FULLNAME, MTFCC -- and the Bus rule deliberately lives only
// there (MI1, S-IBUS). Peaks (feature_class = Summit) and places (LSAD = 25) are filtered HERE ALONE: the store
// does not keep those columns, so a change to either predicate is a change to the store, not to the builder.

// ------------------------------------------------------------------------------------------------ filters

export type FeatureKind = "water" | "rivers" | "roads" | "peaks" | "places";

/** The attribute predicates the store applies (recorded in the manifest's `predicates`). */
export const PREDICATES = [
  "water: AWATER >= 1500000",
  "rivers: FULLNAME ends with \" Riv\"",
  "roads: MTFCC in {S1100, S1200} (business routes kept; the builder drops them)",
  "peaks: feature_class = Summit",
  "places: LSAD = 25",
] as const;

/** Whether a source record passes the store's predicate for its layer. A malformed number is refused, not
 *  read as 0 or NaN-false: a record we cannot read is a finding about the file. */
export function filterRecord(kind: FeatureKind, a: Record<string, string>): boolean {
  // A MISSING column is refused; an empty value is not. Returning false for an absent key once read a
  // pipe-delimited Gazetteer as tab-delimited and kept 0 of 32,350 places without a word.
  const need = { water: "AWATER", rivers: "FULLNAME", roads: "MTFCC", peaks: "feature_class", places: "LSAD" }[kind];
  if (!(need in a)) throw new Error(`${kind}: no ${need} column (keys: ${Object.keys(a).slice(0, 6).join(", ")})`);
  switch (kind) {
    case "water": {
      const v = a["AWATER"];
      if (v === undefined || !/^\d+$/.test(v)) throw new Error(`AWATER unreadable: ${JSON.stringify(v)}`);
      return Number(v) >= 1_500_000;
    }
    case "rivers": return (a["FULLNAME"] ?? "").endsWith(" Riv");
    case "roads": return a["MTFCC"] === "S1100" || a["MTFCC"] === "S1200";
    case "peaks": return a["feature_class"] === "Summit";
    case "places": return (a["LSAD"] ?? "").trim() === "25";
  }
}

// ------------------------------------------------------------------------------------------------ population

/** RFC 4180 lines of one CSV row (quotes, doubled quotes; no embedded newlines in SUB-EST2024). */
export function csvRow(line: string): string[] {
  const out: string[] = [];
  let cur = "", q = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i]!;
    if (q) { if (ch === '"') { if (line[i + 1] === '"') { cur += '"'; i++; } else q = false; } else cur += ch; }
    else if (ch === '"') q = true;
    else if (ch === ",") { out.push(cur); cur = ""; }
    else cur += ch;
  }
  out.push(cur);
  return out;
}

/** GEOID (STATE + PLACE) -> POPESTIMATE2024, from SUB-EST2024's SUMLEV 162 rows only (format §5.5).
 *  A second 162 row for one GEOID is refused: two figures for one place would make the join a guess. */
export function joinPopulation(csvText: string): Map<string, number> {
  const lines = csvText.split(/\r?\n/).filter((l) => l.length > 0);
  const head = csvRow(lines[0]!);
  const col = (n: string) => { const i = head.indexOf(n); if (i < 0) throw new Error(`SUB-EST2024 has no ${n} column`); return i; };
  const iS = col("SUMLEV"), iSt = col("STATE"), iPl = col("PLACE"), iPop = col("POPESTIMATE2024");
  const out = new Map<string, number>();
  for (let k = 1; k < lines.length; k++) {
    const r = csvRow(lines[k]!);
    if (r[iS] !== "162") continue;
    const geoid = r[iSt]! + r[iPl]!;
    if (!/^\d{7}$/.test(geoid) || !/^\d+$/.test(r[iPop]!)) throw new Error(`SUB-EST2024 row ${k} unreadable`);
    if (out.has(geoid)) throw new Error(`two SUMLEV 162 rows for ${geoid}`);
    out.set(geoid, Number(r[iPop]));
  }
  return out;
}

// ------------------------------------------------------------------------------------------------ the text sources

/** GNIS (pipe-delimited) and the Gazetteer (tab-delimited) as header-keyed rows; a BOM and trailing
 *  whitespace on any field are removed. A row with the wrong field count is refused. */
export function readDelimited(text: string, sep: "|" | "\t"): Array<Record<string, string>> {
  const lines = text.replace(/^﻿/, "").split(/\r?\n/).filter((l) => l.length > 0);
  const head = lines[0]!.split(sep).map((h) => h.trim());
  return lines.slice(1).map((l, i) => {
    const v = l.split(sep);
    if (v.length !== head.length) throw new Error(`row ${i + 1} has ${v.length} fields, header ${head.length}`);
    return Object.fromEntries(head.map((h, k) => [h, v[k]!.trim()]));
  });
}

// ------------------------------------------------------------------------------------------------ features and cells

export interface Feature {
  kind: FeatureKind;
  file: number;    // index into the manifest's files (sorted by URL), RD2
  record: number;  // record index in that file
  part: number;    // part index in that record
  /** water: AWATER; rivers/roads: FULLNAME; roads: MTFCC; peaks: feature_id + name; places: GEOID, NAME, pop */
  attrs: Record<string, string | number>;
  xy: Float64Array; // [lon, lat, ...] as the source has them
}

export type Box = [minLon: number, minLat: number, maxLon: number, maxLat: number];

/** Shapefile records of one source file into store features: the predicate, then one feature per PART
 *  (RD2's part index), vertices untouched. Holes stay: RD3 is the builder's, by orientation. */
export function featuresFrom(
  kind: "water" | "rivers" | "roads", file: number, records: Array<{ index: number; parts: Float64Array[]; attrs: Record<string, string> }>,
): Feature[] {
  const out: Feature[] = [];
  for (const r of records) {
    if (!filterRecord(kind, r.attrs)) continue;
    const attrs: Record<string, string | number> =
      kind === "water" ? { AWATER: r.attrs["AWATER"]! } : kind === "rivers" ? { FULLNAME: r.attrs["FULLNAME"] ?? "" } : { MTFCC: r.attrs["MTFCC"]!, FULLNAME: r.attrs["FULLNAME"] ?? "" };
    r.parts.forEach((xy, part) => out.push({ kind, file, record: r.index, part, attrs, xy }));
  }
  return out;
}

/** GNIS rows (RD12: prim_lat_dec / prim_long_dec) or Gazetteer rows (INTPTLAT / INTPTLONG, with the
 *  SUB-EST2024 population joined, 0 = no figure) into point features. */
export function pointsFrom(kind: "peaks" | "places", file: number, rows: Array<Record<string, string>>, pops?: Map<string, number>): Feature[] {
  const out: Feature[] = [];
  rows.forEach((r, record) => {
    if (!filterRecord(kind, r)) return;
    const lat = Number(kind === "peaks" ? r["prim_lat_dec"] : r["INTPTLAT"]);
    const lon = Number(kind === "peaks" ? r["prim_long_dec"] : r["INTPTLONG"]);
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) throw new Error(`${kind} row ${record}: unreadable coordinates`);
    const attrs: Record<string, string | number> = kind === "peaks"
      ? { feature_id: Number(r["feature_id"]), feature_name: r["feature_name"]! }
      : { GEOID: r["GEOID"]!, NAME: r["NAME"]!, pop: pops?.get(r["GEOID"]!) ?? 0 };
    out.push({ kind, file, record, part: 0, attrs, xy: new Float64Array([lon, lat]) });
  });
  return out;
}

export function bboxOf(xy: Float64Array): Box {
  let a = Infinity, b = Infinity, c = -Infinity, d = -Infinity;
  for (let i = 0; i < xy.length; i += 2) { const x = xy[i]!, y = xy[i + 1]!; if (x < a) a = x; if (x > c) c = x; if (y < b) b = y; if (y > d) d = y; }
  return [a, b, c, d];
}

const te = new TextEncoder();
const KIND_CODE: Record<FeatureKind, number> = { water: 1, rivers: 2, roads: 3, peaks: 4, places: 5 };

/** One feature's bytes in a cell object: kind, source-order key, attributes, then f64 vertices. */
export function encodeFeature(f: Feature): Uint8Array {
  const texts: Uint8Array[] = [];
  const nums: number[] = [];
  const t = (s: string | number | undefined) => texts.push(te.encode(String(s ?? "")));
  if (f.kind === "water") nums.push(Number(f.attrs["AWATER"]));
  else if (f.kind === "rivers") t(f.attrs["FULLNAME"]);
  else if (f.kind === "roads") { t(f.attrs["MTFCC"]); t(f.attrs["FULLNAME"]); }
  else if (f.kind === "peaks") { nums.push(Number(f.attrs["feature_id"])); t(f.attrs["feature_name"]); }
  else { t(f.attrs["GEOID"]); t(f.attrs["NAME"]); nums.push(Number(f.attrs["pop"])); }
  const len = 1 + 2 + 4 + 2 + 1 + 1 + nums.length * 8 + texts.reduce((s, x) => s + 2 + x.length, 0) + 4 + f.xy.length * 8;
  const out = new Uint8Array(len);
  const dv = new DataView(out.buffer);
  let o = 0;
  dv.setUint8(o, KIND_CODE[f.kind]); o += 1;
  dv.setUint16(o, f.file, true); o += 2;
  dv.setUint32(o, f.record, true); o += 4;
  dv.setUint16(o, f.part, true); o += 2;
  dv.setUint8(o, nums.length); o += 1;
  dv.setUint8(o, texts.length); o += 1;
  for (const v of nums) { dv.setFloat64(o, v, true); o += 8; }
  for (const x of texts) { if (x.length > 0xffff) throw new Error("text over 65535 bytes"); dv.setUint16(o, x.length, true); o += 2; out.set(x, o); o += x.length; }
  dv.setUint32(o, f.xy.length / 2, true); o += 4;
  for (let i = 0; i < f.xy.length; i++) { dv.setFloat64(o, f.xy[i]!, true); o += 8; }
  return out;
}

const td = new TextDecoder("utf-8", { fatal: true, ignoreBOM: false });
const KIND_OF: Record<number, FeatureKind> = { 1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places" };

/** The inverse of encodeFeature, at `o`; returns the feature and where the next one starts. A record that
 *  runs past the buffer, or an unknown kind, is refused. */
export function decodeFeature(buf: Uint8Array, o: number): { f: Feature; end: number } {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const need = (n: number) => { if (o + n > buf.length) throw new Error(`feature runs past its buffer at ${o}`); };
  need(11);
  const kind = KIND_OF[dv.getUint8(o)];
  if (!kind) throw new Error(`unknown feature kind ${dv.getUint8(o)} at ${o}`);
  const file = dv.getUint16(o + 1, true), record = dv.getUint32(o + 3, true), part = dv.getUint16(o + 7, true);
  const nNums = dv.getUint8(o + 9), nTexts = dv.getUint8(o + 10);
  o += 11;
  const nums: number[] = [], texts: string[] = [];
  for (let i = 0; i < nNums; i++) { need(8); nums.push(dv.getFloat64(o, true)); o += 8; }
  for (let i = 0; i < nTexts; i++) { need(2); const n = dv.getUint16(o, true); o += 2; need(n); texts.push(td.decode(buf.subarray(o, o + n))); o += n; }
  need(4);
  const n = dv.getUint32(o, true); o += 4;
  need(n * 16);
  const xy = new Float64Array(n * 2);
  for (let i = 0; i < n * 2; i++) xy[i] = dv.getFloat64(o + 8 * i, true);
  o += n * 16;
  const attrs: Record<string, string | number> =
    kind === "water" ? { AWATER: nums[0]! } : kind === "rivers" ? { FULLNAME: texts[0]! }
    : kind === "roads" ? { MTFCC: texts[0]!, FULLNAME: texts[1]! }
    : kind === "peaks" ? { feature_id: nums[0]!, feature_name: texts[0]! }
    : { GEOID: texts[0]!, NAME: texts[1]!, pop: nums[0]! };
  return { f: { kind, file, record, part, attrs, xy }, end: o };
}

/** A cell object's bytes: "BMSC", version 1, the feature count, then the features (A1.7 MI5/MI6's cells). */
export function encodeCell(features: Uint8Array[]): Uint8Array {
  const n = features.reduce((a, f) => a + f.length, 12);
  const out = new Uint8Array(n), dv = new DataView(out.buffer);
  out.set([0x42, 0x4d, 0x53, 0x43], 0); dv.setUint32(4, 1, true); dv.setUint32(8, features.length, true);
  let o = 12;
  for (const f of features) { out.set(f, o); o += f.length; }
  return out;
}

/** A cell key at a level (cell size 2^-level degrees): its south-west corner in that grid's integers. */
export const cellKey = (level: number, lon: number, lat: number) =>
  `${level}/${Math.floor(lat * 2 ** level)}/${Math.floor(lon * 2 ** level)}`;

export interface Cell { key: string; level: number; features: Uint8Array[]; boxes: Box[]; bytes: number }
export interface CellIndexEntry { key: string; bytes: number; count: number; reach: Box }

/** Features into cells by their bounding box's SOUTH-WEST corner, each stored ONCE (MI5). A cell over
 *  `splitBytes` is split into quarters until none is -- except a cell holding a single feature, which cannot
 *  be split and is kept whole (MI6). So is a cell whose features all share ONE south-west corner: no quarter
 *  can ever separate them, and splitting it would recurse without end. */
export function assignCells(feats: Array<{ bytes: Uint8Array; box: Box }>, splitBytes: number, level = 0): Cell[] {
  const cells = new Map<string, Cell>();
  for (const f of feats) {
    const k = cellKey(level, f.box[0], f.box[1]);
    let c = cells.get(k);
    if (!c) { c = { key: k, level, features: [], boxes: [], bytes: 0 }; cells.set(k, c); }
    c.features.push(f.bytes); c.boxes.push(f.box); c.bytes += f.bytes.length;
  }
  const out: Cell[] = [];
  for (const c of cells.values()) {
    const separable = c.boxes.some((b) => b[0] !== c.boxes[0]![0] || b[1] !== c.boxes[0]![1]);
    if (c.bytes > splitBytes && c.features.length > 1 && separable) {
      out.push(...assignCells(c.features.map((b, i) => ({ bytes: b, box: c.boxes[i]! })), splitBytes, level + 1));
    } else out.push(c);
  }
  return out.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
}

export function reachOf(c: Cell): Box {
  const r: Box = [Infinity, Infinity, -Infinity, -Infinity];
  for (const b of c.boxes) { if (b[0] < r[0]) r[0] = b[0]; if (b[1] < r[1]) r[1] = b[1]; if (b[2] > r[2]) r[2] = b[2]; if (b[3] > r[3]) r[3] = b[3]; }
  return r;
}

// ------------------------------------------------------------------------------------------------ the manifest

export interface ManifestFile { bytes: number; layer: string; sha256: string; url: string; vintage: string }
/** `notes` (ruled 2026-10-28, MI4b): one "<url>: <why>" per file fetched from a mirror; ABSENT when there is
 *  nothing to note, so a store with no mirrored file is byte-identical to one written before the key existed. */
export interface Manifest { builder: string; files: ManifestFile[]; predicates: string[]; notes?: string[] }

const byteCmp = (a: string, b: string) => {
  const x = te.encode(a), y = te.encode(b);
  for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i]! - y[i]!;
  return x.length - y.length;
};

/** MI4b's rules: present means non-empty; each note is "<a listed url>: <why>", one per URL, its why holding no
 *  wall-clock date or time; the list strictly ascending in byte order. Refused otherwise -- never repaired. */
function checkNotes(notes: unknown, urls: string[]): void {
  if (!Array.isArray(notes) || notes.length === 0) throw new Error("manifest notes must be absent, never empty");
  const seen = new Set<string>();
  for (const n of notes) {
    if (typeof n !== "string") throw new Error("a manifest note is not a string");
    const url = urls.filter((u) => n.startsWith(u + ": ")).sort((a, b) => b.length - a.length)[0];
    if (!url) throw new Error(`a manifest note is not "<listed url>: <why>": ${n}`);
    const why = n.slice(url.length + 2);
    if (!why.trim()) throw new Error(`a manifest note has no reason: ${url}`);
    if (/\d{4}-\d{2}-\d{2}|\d{1,2}:\d{2}/.test(why)) throw new Error(`a manifest note holds a wall-clock time: ${url}`);
    if (seen.has(url)) throw new Error(`two manifest notes for ${url}`);
    seen.add(url);
  }
  for (let i = 1; i < notes.length; i++) if (byteCmp(notes[i - 1], notes[i]) >= 0) throw new Error("manifest notes must be sorted, in byte order");
}

/** Keys sorted at every depth. NOT `JSON.stringify(v, keysArray)`: an array replacer is an allowlist applied
 *  at every depth, and silently drops every nested key not in it (it emptied the first fetch log). */
export function sortKeys(v: unknown): unknown {
  if (Array.isArray(v)) return v.map(sortKeys);
  if (v && typeof v === "object") return Object.fromEntries(Object.keys(v as object).sort().map((k) => [k, sortKeys((v as Record<string, unknown>)[k])]));
  return v;
}

/** The manifest's bytes (MI4, ruled): UTF-8 JSON, keys sorted at every depth, one-space indent, final \n.
 *  Exactly the keys {builder, files, predicates}; each file exactly {bytes, layer, sha256, url, vintage};
 *  files sorted by URL. Anything else is refused, so `store:` can only ever name this shape. */
export function writeManifest(m: Manifest): string {
  const top = Object.keys(m).sort().join(",");
  if (top !== "builder,files,predicates" && top !== "builder,files,notes,predicates") throw new Error(`manifest keys ${top}`);
  for (const f of m.files) {
    const k = Object.keys(f).sort().join(",");
    if (k !== "bytes,layer,sha256,url,vintage") throw new Error(`manifest file keys ${k}`);
  }
  if ("notes" in m) checkNotes(m.notes, m.files.map((f) => f.url));
  const files = [...m.files].sort((a, b) => (a.url < b.url ? -1 : a.url > b.url ? 1 : 0));
  return JSON.stringify(sortKeys({ ...m, files }), null, 1) + "\n";
}

export async function sha256Hex(bytes: Uint8Array | string): Promise<string> {
  const b = typeof bytes === "string" ? te.encode(bytes) : bytes;
  const d = await crypto.subtle.digest("SHA-256", b);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

/** `store:` (format §7): the first 12 hex digits of the manifest's sha256. */
export const storeIdOf = async (manifestText: string) => (await sha256Hex(manifestText)).slice(0, 12);

// ------------------------------------------------------------------------------------------------ write, verify, seal

export interface StoreBucket {
  put(key: string, bytes: Uint8Array): Promise<void>;
  get(key: string): Promise<Uint8Array | null>;
}
export interface StoreObject { key: string; bytes: Uint8Array }

/** Every object present, the same size, and the same sha256 (MI7: size alone is not enough, S-ISIZE). */
export async function verifyStore(bucket: StoreBucket, objects: StoreObject[]): Promise<{ missing: string[]; mismatched: string[] }> {
  const missing: string[] = [], mismatched: string[] = [];
  for (const o of objects) {
    const got = await bucket.get(o.key);
    if (!got) { missing.push(o.key); continue; }
    if (got.length !== o.bytes.length || (await sha256Hex(got)) !== (await sha256Hex(o.bytes))) mismatched.push(o.key);
  }
  return { missing, mismatched };
}

/** write -> verify -> repair -> verify, and only then `current`, as the LAST write (MI7, S-ISEAL). A store that
 *  is still not clean after `repairs` rounds is left unsealed and reported, never sealed with a gap in it. */
export async function publishStore(
  bucket: StoreBucket, objects: StoreObject[], currentKey: string, currentBody: Uint8Array, repairs = 2,
): Promise<{ sealed: boolean; rounds: number; missing: string[]; mismatched: string[] }> {
  for (const o of objects) await bucket.put(o.key, o.bytes);
  let v = await verifyStore(bucket, objects);
  let rounds = 0;
  while ((v.missing.length || v.mismatched.length) && rounds < repairs) {
    rounds++;
    const again = new Set([...v.missing, ...v.mismatched]);
    for (const o of objects) if (again.has(o.key)) await bucket.put(o.key, o.bytes);
    v = await verifyStore(bucket, objects);
  }
  if (v.missing.length || v.mismatched.length) return { sealed: false, rounds, ...v };
  await bucket.put(currentKey, currentBody);
  return { sealed: true, rounds, ...v };
}
