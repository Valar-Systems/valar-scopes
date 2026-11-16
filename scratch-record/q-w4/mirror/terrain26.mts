// SCRATCH (map-server): step8_26.pred Amendment 5d.2's (b) row, R5c-4's GAP. READ-ONLY.
// SEA-OWN's comparator paired sections by FIRST of type, so its "every other section deep-equal" compared only the first terrain
// section of each package. This row compares ALL of them, by position, at the 16 homes:
//   NEW = tb26-out/b/<H>-tb.bmpk, OLD = cp-out/<H>-sea.bmpk, both validated (kOk) and decoded with the dry-clone's format.ts.
//   The two type sequences must be equal (else the pair is OFF). Sections are then paired BY INDEX. Each terrain section (type 6:
//   levelRadiusMm, n, values) is compared deep-equal, labelled level 1, 2, 3 by its position among the type-6 sections.
// THE TYPE CENSUS, recorded for the other tools: per package, the count of each type. d2d3_26, d4_26, pkg2json and seaown26 take
//   types 1, 2, 3 and 7 by first-of-type; those lookups are sound only where each of those types occurs exactly once.
// CONTROLS FIRST: KJFK NEW against itself reads every level equal; against a copy (in memory) with level 2's values[0] flipped, it reads
//   level 2 unequal and levels 1 and 3 equal.
// Prints types, counts and equalities only.
//   (from dry-clone/proxy) node --import tsx ../../mirror/terrain26.mts
import { readFileSync } from "node:fs";
import { decode, validate } from "../dry-clone/proxy/src/basemap/format.ts";

const S = new URL("../", import.meta.url);
const HOMES = ["KBOS", "KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KORD", "KDTW", "KMSP", "KIND", "KJAC", "BUFFALO", "ROUSES", "ZAPATA"];
const dec = (p: string) => { const b = new Uint8Array(readFileSync(new URL(p, S))); if (validate(b).status !== "kOk") throw new Error(`${p}: not kOk`); return decode(b) as any; };
const types = (p: any) => p.sections.map((s: any) => s.type) as number[];
const terr = (s: any) => JSON.stringify({ r: s.levelRadiusMm, n: s.n, v: Array.from(s.values as Uint8Array) });
function compare(n: any, o: any) {
  const tn = types(n), to = types(o);
  if (JSON.stringify(tn) !== JSON.stringify(to)) return { typesEqual: false, levels: [] as boolean[] };
  const levels: boolean[] = [];
  tn.forEach((t, i) => { if (t === 6) levels.push(terr(n.sections[i]) === terr(o.sections[i])); });
  return { typesEqual: true, levels };
}
const census = (p: any) => { const c: Record<number, number> = {}; for (const t of types(p)) c[t] = (c[t] ?? 0) + 1; return c; };

const kn = dec("tb26-out/b/KJFK-tb.bmpk"), self = compare(kn, kn);
const cp = dec("tb26-out/b/KJFK-tb.bmpk");
const lvl2 = cp.sections.map((s: any, i: number) => [s.type, i]).filter(([t]: number[]) => t === 6)[1]?.[1];
if (lvl2 !== undefined) cp.sections[lvl2].values[0] ^= 1;
const red = compare(cp, kn);
const ctl = self.typesEqual && self.levels.length === 3 && self.levels.every(Boolean) && red.typesEqual && JSON.stringify(red.levels) === JSON.stringify([true, false, true]);
console.log(`CONTROLS: self levels [${self.levels}]; level 2 planted: levels [${red.levels}]; ${ctl ? "AS PREDICTED" : "OFF: the comparison is not used"}`);
if (!ctl) process.exit(1);

let eq = 0, single = 0;
for (const h of HOMES) {
  const n = dec(`tb26-out/b/${h}-tb.bmpk`), o = dec(`cp-out/${h}-sea.bmpk`);
  const r = compare(n, o), cn = census(n), co = census(o);
  const one = [1, 2, 3, 7].every((t) => cn[t] === 1 && co[t] === 1);
  if (one) single++;
  if (r.typesEqual && r.levels.length === 3 && r.levels.every(Boolean)) eq++;
  console.log(`${h}: types NEW ${JSON.stringify(types(n))} OLD equal ${r.typesEqual}; terrain levels equal [${r.levels}]; types 1, 2, 3, 7 once in both ${one}`);
}
console.log(`TERRAIN: ${eq} of 16 homes with every terrain level equal by position; TYPE CENSUS: types 1, 2, 3, 7 once in both packages at ${single} of 16`);
process.exit(eq === 16 && single === 16 ? 0 : 1);
