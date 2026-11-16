// SCRATCH (map-server): step8_26.pred Amendment 5c, SEA-OWN's comparator (doc :3317, :4118-4119; R1). READ-ONLY.
// At each of cp-run.mts's 16 homes: NEW = tb26-out/b/<H>-tb.bmpk (the code PR's builder on its own store, tree B) against
// OLD = cp-out/<H>-sea.bmpk (SEA-PKG, the prototype store). Both are validated (kOk) and decoded with the dry-clone's format.ts.
//   header      lat0Micro, lon0Micro, radiusMm, toleranceMm equal
//   sections    the same type sequence
//   water (1)   the rings equal AS A MULTISET (each ring compared as its exact coordinate list)
//   7, PROV     line by line by key (the text before ": "): the set of keys whose line differs
//   every other the decoded section deep-equal (typed arrays as arrays), the stand-in for byte-identical
// Graded at the 14 (R1: KBOS and KJAC are REPORTED with the same figures, not graded):
//   header, types, water multiset and every other section equal; PROVENANCE's differing keys EXACTLY the predicted set:
//   {store} plus {source.sea, vintage.sea, sha256.sea} when both packages read the sea (both carry a source.sea line).
// CONTROL first: the comparator on a planted COPY of KJFK NEW (one roads/rivers coordinate changed in the decoded package, re-encoded with
// format.ts encode, its changed sha proved) must read that section unequal; and on KJFK NEW against itself, everything equal.
// Prints keys, counts and equalities only.
//   (from dry-clone/proxy) node --import tsx ../../mirror/seaown26.mts
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { decode, encode, validate } from "../dry-clone/proxy/src/basemap/format.ts";

const HOMES = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC", "BUFFALO", "ROUSES", "ZAPATA"];
const REPORT = new Set(["KBOS", "KJAC"]);
const S = new URL("../", import.meta.url);
const NEW = (h: string) => new Uint8Array(readFileSync(new URL(`tb26-out/b/${h}-tb.bmpk`, S)));
const OLD = (h: string) => new Uint8Array(readFileSync(new URL(`cp-out/${h}-sea.bmpk`, S)));
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex").slice(0, 12);
const plain = (v: unknown): unknown => (ArrayBuffer.isView(v) ? Array.from(v as unknown as ArrayLike<number>) : Array.isArray(v) ? v.map(plain)
  : v && typeof v === "object" ? Object.fromEntries(Object.entries(v as object).map(([k, x]) => [k, plain(x)])) : v);
const dec = (b: Uint8Array) => { if (validate(b).status !== "kOk") throw new Error("not kOk"); return decode(b) as any; };
const provLines = (s: any) => new TextDecoder().decode(s.text).split("\n");
const keyOf = (l: string) => l.split(": ")[0]!;

function compare(nb: Uint8Array, ob: Uint8Array) {
  const n = dec(nb), o = dec(ob);
  const header = ["lat0Micro", "lon0Micro", "radiusMm", "toleranceMm"].every((k) => n[k] === o[k]);
  const tn = n.sections.map((s: any) => s.type), to = o.sections.map((s: any) => s.type);
  const types = JSON.stringify(tn) === JSON.stringify(to);
  const peaks = tn.includes(4) || to.includes(4);   // TYPE.PEAKS = 4 (format.ts); the client's query asks for no peaks
  const unequal: number[] = [];
  let waterOnlyNew = 0, waterOnlyOld = 0, provKeys: string[] = [], readsSea = false;
  for (const t of new Set([...tn, ...to])) {
    const a = n.sections.find((s: any) => s.type === t), b = o.sections.find((s: any) => s.type === t);
    if (!a || !b) { unequal.push(t); continue; }
    if (t === 1) {
      const c = new Map<string, number>();
      for (const r of a.rings) { const k = JSON.stringify(plain(r)); c.set(k, (c.get(k) ?? 0) + 1); }
      for (const r of b.rings) { const k = JSON.stringify(plain(r)); c.set(k, (c.get(k) ?? 0) - 1); }
      for (const v of c.values()) { if (v > 0) waterOnlyNew += v; if (v < 0) waterOnlyOld -= v; }
    } else if (t === 7) {
      const la = provLines(a), lb = provLines(b);
      const ma = new Map(la.map((l) => [keyOf(l), l])), mb = new Map(lb.map((l) => [keyOf(l), l]));
      provKeys = [...new Set([...ma.keys(), ...mb.keys()])].filter((k) => ma.get(k) !== mb.get(k)).sort();
      readsSea = ma.has("source.sea") && mb.has("source.sea");
    } else if (JSON.stringify(plain(a)) !== JSON.stringify(plain(b))) unequal.push(t);
  }
  const want = (readsSea ? ["sha256.sea", "source.sea", "store", "vintage.sea"] : ["store"]).sort();
  const ok = !peaks && header && types && unequal.length === 0 && waterOnlyNew === 0 && waterOnlyOld === 0 && JSON.stringify(provKeys) === JSON.stringify(want);
  return { header, types, peaks, unequal, waterOnlyNew, waterOnlyOld, provKeys, readsSea, ok };
}

// CONTROL
const kn = NEW("KJFK"), self = compare(kn, kn);
const pk = dec(kn);
const target = pk.sections.find((s: any) => s.type === 3) ?? pk.sections.find((s: any) => s.type === 2);
let planted = false;
const item = target ? (target.type === 3 ? target.roads[0] : target.lines[0]) : undefined;   // format.ts:24-25; Pt = [number, number]
if (item && item.pts.length) { item.pts[0][0] = item.pts[0][0] + 1; planted = true; }
const pb = planted ? encode(pk) : kn;
const applied = planted && sha(pb) !== sha(kn);
const red = applied ? compare(pb, kn) : null;
const ctlOk = self.unequal.length === 0 && self.waterOnlyNew === 0 && self.provKeys.length === 0 && applied && red !== null && red.unequal.includes(target.type);
console.log(`CONTROL: KJFK NEW against itself: unequal [${self.unequal}], water only ${self.waterOnlyNew}/${self.waterOnlyOld}, prov keys [${self.provKeys}]; `
  + `plant in section ${target?.type} applied ${applied}; planted copy reads unequal [${red?.unequal ?? ""}]; ${ctlOk ? "AS PREDICTED" : "OFF: the comparator is not used"}`);
if (!ctlOk) process.exit(1);

let graded = 0, good = 0;
for (const h of HOMES) {
  const r = compare(NEW(h), OLD(h));
  const tag = REPORT.has(h) ? "REPORTED" : r.ok ? "AS PREDICTED" : "OFF";
  if (!REPORT.has(h)) { graded++; good += r.ok ? 1 : 0; }
  console.log(`${h}: peaks section in either ${r.peaks}; header ${r.header}; types ${r.types}; unequal sections [${r.unequal}]; water rings only-new ${r.waterOnlyNew} only-old ${r.waterOnlyOld}; `
    + `prov differing keys [${r.provKeys.join(" ")}] (reads sea ${r.readsSea}); ${tag}`);
}
console.log(`SEA-OWN: ${good} of ${graded} graded points AS PREDICTED${good === graded ? "" : " -- OFF: STOP"}; KBOS and KJAC reported above`);
process.exit(good === graded ? 0 : 1);
