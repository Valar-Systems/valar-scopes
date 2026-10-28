// BMPK v1: the encoder, the decoder and the validator (docs/basemap-package-format.md; docs/basemap-route.md
// A1.7's MV and ME). Pure: Uint8Array in and out, no Worker or Node API, so the builder and its tests share it.
//
// validate() follows A10.11's PINNED order, so each refused package has exactly one reason. Where §8 leaves an edge
// open, it does what Weatherscope's reference validator (tools/weatherscope/basemap/validate.py) does, so the two
// validators cannot disagree on a package: a varint that runs out of its section is kBody, not kVarint; ring and
// line ranges are checked after every delta is read; PEAKS and PLACES positions are not range-checked; and neither a
// TERRAIN section's count nor PROVENANCE's line count is checked (raised in the route doc, not decided here).

export type Status =
  | "kOk" | "kHeader" | "kMagic" | "kVersion" | "kFlags" | "kTotal" | "kCrc" | "kSectionCount" | "kReserved"
  | "kSectionBounds" | "kOverlap" | "kGap" | "kDuplicate" | "kNoProvenance" | "kVarint" | "kVertexRange"
  | "kVertexCount" | "kText" | "kRiverName" | "kRoadClass" | "kTerrainLength" | "kBody";

export const TYPE = { WATER: 1, RIVERS: 2, ROADS: 3, PEAKS: 4, PLACES: 5, TERRAIN: 6, PROVENANCE: 7 } as const;
const HEADER = 32, ENTRY = 16, QMAX = 32767;

export type Pt = [number, number];
export type Section =
  | { type: 1; rings: Pt[][] }
  | { type: 2; names: string[]; lines: Array<{ name: number; pts: Pt[] }> }
  | { type: 3; roads: Array<{ cls: 1 | 2; pts: Pt[] }> }
  | { type: 4; peaks: Array<{ q: Pt; elevM: number; name: string }> }
  | { type: 5; places: Array<{ q: Pt; pop: number; name: string }> }
  | { type: 6; levelRadiusMm: number; n: number; values: Uint8Array }
  | { type: 7; text: Uint8Array; lines: number }
  | { type: number; raw: Uint8Array; count: number }; // an unknown type, kept byte for byte

export interface Package { lat0Micro: number; lon0Micro: number; radiusMm: number; toleranceMm: number; sections: Section[] }

// ------------------------------------------------------------------------------------------------ CRC-32 (IEEE)

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
export function crc32(b: Uint8Array): number {
  let c = 0xffffffff;
  for (let i = 0; i < b.length; i++) c = CRC_TABLE[(c ^ b[i]!) & 0xff]! ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
/** The package's CRC: over all its bytes with the CRC field (28..31) zeroed. */
function packageCrc(b: Uint8Array): number {
  const z = b.slice(); z[28] = z[29] = z[30] = z[31] = 0;
  return crc32(z);
}

// ------------------------------------------------------------------------------------------------ writing

class Writer {
  private parts: number[] = [];
  u8(v: number) { this.parts.push(v & 0xff); }
  varint(v: number) {
    if (!Number.isInteger(v) || v < 0 || v > 0xffffffff) throw new Error(`varint out of range: ${v}`);
    do { let b = v & 0x7f; v = Math.floor(v / 128); if (v) b |= 0x80; this.parts.push(b); } while (v);
  }
  svarint(v: number) { this.varint(((v << 1) ^ (v >> 31)) >>> 0); }
  text(s: string) {
    const b = new TextEncoder().encode(s);
    if (b.length > 255) throw new Error(`text over 255 bytes: ${s}`);
    this.u8(b.length); for (const x of b) this.parts.push(x);
  }
  bytes(b: Uint8Array) { for (const x of b) this.parts.push(x); }
  vertices(pts: Pt[]) {
    this.varint(pts.length);
    let px = 0, py = 0;
    pts.forEach(([x, y], i) => { if (i === 0) { this.svarint(x); this.svarint(y); } else { this.svarint(x - px); this.svarint(y - py); } px = x; py = y; });
  }
  done() { return Uint8Array.from(this.parts); }
}

function body(s: Section): { count: number; bytes: Uint8Array } {
  const w = new Writer();
  if ("raw" in s) return { count: s.count, bytes: s.raw };
  switch (s.type) {
    case 1: for (const r of s.rings) w.vertices(r); return { count: s.rings.length, bytes: w.done() };
    case 2: w.varint(s.names.length); for (const n of s.names) w.text(n); for (const l of s.lines) { w.varint(l.name); w.vertices(l.pts); } return { count: s.lines.length, bytes: w.done() };
    case 3: for (const r of s.roads) { w.u8(r.cls); w.vertices(r.pts); } return { count: s.roads.length, bytes: w.done() };
    case 4: for (const p of s.peaks) { w.svarint(p.q[0]); w.svarint(p.q[1]); w.svarint(p.elevM); w.text(p.name); } return { count: s.peaks.length, bytes: w.done() };
    case 5: for (const p of s.places) { w.svarint(p.q[0]); w.svarint(p.q[1]); w.varint(p.pop); w.text(p.name); } return { count: s.places.length, bytes: w.done() };
    case 6: {
      const out = new Uint8Array(8 + s.values.length), dv = new DataView(out.buffer);
      dv.setUint32(0, s.levelRadiusMm, true); dv.setUint16(4, s.n, true); dv.setUint16(6, 0, true); out.set(s.values, 8);
      return { count: 1, bytes: out };
    }
    case 7: return { count: s.lines, bytes: s.text };
  }
  throw new Error(`no encoder for section type ${(s as { type: number }).type}`);
}

/** A section's body length, exactly as encode() writes it (budget.ts measures with this, so no second encoder). */
export const sectionSize = (s: Section) => body(s).bytes.length;

/** The package's bytes, sections in the order given, contiguous, CRC sealed. */
export function encode(p: Package): Uint8Array {
  const bodies = p.sections.map(body);
  const start = HEADER + ENTRY * p.sections.length;
  const total = start + bodies.reduce((a, b) => a + b.bytes.length, 0);
  const out = new Uint8Array(total), dv = new DataView(out.buffer);
  out.set([0x42, 0x4d, 0x50, 0x4b]);
  dv.setUint8(4, 1); dv.setUint8(5, p.sections.length); dv.setUint16(6, 0, true);
  dv.setInt32(8, p.lat0Micro, true); dv.setInt32(12, p.lon0Micro, true);
  dv.setUint32(16, p.radiusMm, true); dv.setUint32(20, p.toleranceMm, true); dv.setUint32(24, total, true);
  let off = start;
  p.sections.forEach((s, i) => {
    const e = HEADER + ENTRY * i;
    dv.setUint8(e, s.type); dv.setUint8(e + 1, 0); dv.setUint16(e + 2, 0, true);
    dv.setUint32(e + 4, bodies[i]!.count, true); dv.setUint32(e + 8, off, true); dv.setUint32(e + 12, bodies[i]!.bytes.length, true);
    out.set(bodies[i]!.bytes, off); off += bodies[i]!.bytes.length;
  });
  dv.setUint32(28, packageCrc(out), true);
  return out;
}

// ------------------------------------------------------------------------------------------------ reading

class Bad extends Error { constructor(readonly status: Status) { super(status); } }
const td = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });

class Reader {
  i = 0;
  constructor(private b: Uint8Array) {}
  left() { return this.b.length - this.i; }
  u8() { if (this.i >= this.b.length) throw new Bad("kBody"); return this.b[this.i++]!; }
  varint(): number {
    let v = 0, mul = 1, n = 0;
    for (;;) {
      if (this.i >= this.b.length) throw new Bad("kBody"); // ran out mid-item
      const byte = this.b[this.i++]!;
      if (++n > 5) throw new Bad("kVarint");
      v += (byte & 0x7f) * mul; mul *= 128;
      if (!(byte & 0x80)) { if (n > 1 && byte === 0) throw new Bad("kVarint"); return v; } // overlong
    }
  }
  svarint() { const z = this.varint() % 0x100000000; return z % 2 ? -(z + 1) / 2 : z / 2; }
  text() {
    const n = this.u8();
    if (n > this.left()) throw new Bad("kText");
    let s: string;
    try { s = td.decode(this.b.subarray(this.i, this.i + n)); } catch { throw new Bad("kText"); }
    this.i += n;
    return s;
  }
  vertices(n: number): Pt[] {
    let x = this.svarint(), y = this.svarint();
    const pts: Pt[] = [[x, y]];
    for (let k = 1; k < n; k++) { x += this.svarint(); y += this.svarint(); pts.push([x, y]); }
    for (const [px, py] of pts) if (px < -QMAX || px > QMAX || py < -QMAX || py > QMAX) throw new Bad("kVertexRange");
    return pts;
  }
}

function section(type: number, count: number, b: Uint8Array): Section {
  const r = new Reader(b);
  let s: Section;
  switch (type) {
    case 1: {
      const rings: Pt[][] = [];
      for (let k = 0; k < count; k++) { const n = r.varint(); if (n < 3) throw new Bad("kVertexCount"); rings.push(r.vertices(n)); }
      s = { type: 1, rings }; break;
    }
    case 2: {
      const nNames = r.varint(), names: string[] = [];
      for (let k = 0; k < nNames; k++) names.push(r.text());
      const lines: Array<{ name: number; pts: Pt[] }> = [];
      for (let k = 0; k < count; k++) {
        const name = r.varint(); if (name >= names.length) throw new Bad("kRiverName");
        const n = r.varint(); if (n < 2) throw new Bad("kVertexCount");
        lines.push({ name, pts: r.vertices(n) });
      }
      s = { type: 2, names, lines }; break;
    }
    case 3: {
      const roads: Array<{ cls: 1 | 2; pts: Pt[] }> = [];
      for (let k = 0; k < count; k++) {
        const cls = r.u8(); if (cls !== 1 && cls !== 2) throw new Bad("kRoadClass");
        const n = r.varint(); if (n < 2) throw new Bad("kVertexCount");
        roads.push({ cls, pts: r.vertices(n) });
      }
      s = { type: 3, roads }; break;
    }
    case 4: {
      const peaks: Array<{ q: Pt; elevM: number; name: string }> = [];
      for (let k = 0; k < count; k++) { const x = r.svarint(), y = r.svarint(); peaks.push({ q: [x, y], elevM: r.svarint(), name: r.text() }); }
      s = { type: 4, peaks }; break;
    }
    case 5: {
      const places: Array<{ q: Pt; pop: number; name: string }> = [];
      for (let k = 0; k < count; k++) { const x = r.svarint(), y = r.svarint(); places.push({ q: [x, y], pop: r.varint(), name: r.text() }); }
      s = { type: 5, places }; break;
    }
    case 6: {
      if (b.length < 8) throw new Bad("kTerrainLength");
      const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
      const levelRadiusMm = dv.getUint32(0, true), n = dv.getUint16(4, true), res = dv.getUint16(6, true);
      if (n === 0 || b.length !== 8 + n * n) throw new Bad("kTerrainLength");
      if (res !== 0) throw new Bad("kReserved");
      return { type: 6, levelRadiusMm, n, values: b.slice(8) };
    }
    case 7: return { type: 7, text: b.slice(), lines: count }; // clients never parse PROVENANCE (§7)
    default: return { type, raw: b.slice(), count };              // an unknown type: skipped by its length
  }
  if (r.left() !== 0) throw new Bad("kBody");
  return s;
}

/** A10.11's pinned order. kOk with the decoded package, or the one reason it is refused. */
export function validate(b: Uint8Array): { status: Status; pkg?: Package } {
  try {
    if (b.length < HEADER) throw new Bad("kHeader");
    if (b[0] !== 0x42 || b[1] !== 0x4d || b[2] !== 0x50 || b[3] !== 0x4b) throw new Bad("kMagic");
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    if (dv.getUint8(4) !== 1) throw new Bad("kVersion");
    const nsec = dv.getUint8(5);
    if (dv.getUint16(6, true) !== 0) throw new Bad("kFlags");
    const total = dv.getUint32(24, true);
    if (total !== b.length) throw new Bad("kTotal");
    if (packageCrc(b) !== dv.getUint32(28, true)) throw new Bad("kCrc");
    const dirEnd = HEADER + ENTRY * nsec;
    if (nsec < 1 || nsec > 32 || dirEnd > total) throw new Bad("kSectionCount");
    const entries = Array.from({ length: nsec }, (_, i) => {
      const e = HEADER + ENTRY * i;
      return { type: dv.getUint8(e), flags: dv.getUint8(e + 1), res: dv.getUint16(e + 2, true), count: dv.getUint32(e + 4, true), off: dv.getUint32(e + 8, true), len: dv.getUint32(e + 12, true) };
    });
    for (const e of entries) {
      if (e.flags !== 0 || e.res !== 0) throw new Bad("kReserved");
      if (e.off < dirEnd || e.off + e.len > total) throw new Bad("kSectionBounds");
    }
    let expect = dirEnd;
    for (const e of entries) {
      if (e.off < expect) throw new Bad("kOverlap");
      if (e.off > expect) throw new Bad("kGap");
      expect = e.off + e.len;
    }
    if (expect !== total) throw new Bad("kGap");
    const seen = new Set<number>();
    for (const e of entries) {
      if ([1, 2, 3, 4, 5, 7].includes(e.type) && seen.has(e.type)) throw new Bad("kDuplicate");
      seen.add(e.type);
    }
    if (!seen.has(7)) throw new Bad("kNoProvenance");
    const sections = entries.map((e) => section(e.type, e.count, b.subarray(e.off, e.off + e.len)));
    return {
      status: "kOk",
      pkg: { lat0Micro: dv.getInt32(8, true), lon0Micro: dv.getInt32(12, true), radiusMm: dv.getUint32(16, true), toleranceMm: dv.getUint32(20, true), sections },
    };
  } catch (e) {
    if (e instanceof Bad) return { status: e.status };
    throw e;
  }
}

/** The package a valid buffer holds; throws with the reason for one that is refused. */
export function decode(b: Uint8Array): Package {
  const v = validate(b);
  if (v.status !== "kOk" || !v.pkg) throw new Error(`refused: ${v.status}`);
  return v.pkg;
}

/** One svarint's bytes (ME6). */
export function svarintBytes(v: number): Uint8Array { const w = new Writer(); w.svarint(v); return w.done(); }
