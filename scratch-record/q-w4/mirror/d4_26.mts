// SCRATCH (map-server): step8_26.pred Amendment 5c.1's D4 (the orchestrator's addition): the WATER DECOMPOSITION, D3's water twin. READ-ONLY.
// At the 11 points where SEA-OWN's water multisets differed (KJFK KLGA KEWR MIDTOWN KMIA KSFO KSEA KDTW BUFFALO ROUSES ZAPATA):
//   NEW = tb26-out/b/<H>-tb.bmpk with its build report <H>-tb.report.json (R5c-2);
//   OLD = cp-out/<H>-sea.bmpk with the report of the build that made those exact bytes: tb26-out/ctl/<H>-ctl.report.json (R5c-1, whose
//         package is byte-equal to cp-out's; checked here by sha, else INSTRUMENT).
// ATTRIBUTION is the builder's own: report.sea.ringIdx lists the positions of the SEA rings in the package's water section (build.ts:265,
//   by object identity); every other water ring is TIGER-sourced. Recorded beside it: ringIdx's length against drawn - dropped.
// TIGER part: the TIGER rings of NEW and OLD as multisets (exact coordinates): only-new, only-old. TIGER drops per side =
//   PROVENANCE's dropped.water - report.sea.dropped (budget.ts:123-124: rung 4 drops the smallest-area rings from ALL water rings, sea
//   included; the report counts the sea ones). "Budget consequence" when the TIGER difference is one-sided, on the side the drop
//   counts say, and of size exactly |TIGER drops(NEW) - TIGER drops(OLD)|. Anything else in TIGER water: a REAL water difference.
// SEA part: the sea rings' only-new / only-old, recorded: what D1 explains.
// CONTROLS: KJFK NEW against itself 0/0 TIGER, 0/0 sea; a copy of KJFK NEW with its first TIGER ring removed (in memory, ringIdx shifted)
//   against KJFK NEW reads TIGER only-new 0, only-old 1, and sea 0/0.
// Prints counts and booleans only.
//   (from dry-clone/proxy) node --import tsx ../../mirror/d4_26.mts
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { decode, validate } from "../dry-clone/proxy/src/basemap/format.ts";

const S = new URL("../", import.meta.url);
const rd = (p: string) => new Uint8Array(readFileSync(new URL(p, S)));
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex").slice(0, 12);
const ELEVEN = ["KJFK", "KLGA", "KEWR", "MIDTOWN", "KMIA", "KSFO", "KSEA", "KDTW", "BUFFALO", "ROUSES", "ZAPATA"];
type Side = { rings: number[][][]; seaIdx: Set<number>; droppedWater: number; seaDropped: number; seaDrawn: number };
function side(pkgPath: string, repPath: string): Side {
  const b = rd(pkgPath); if (validate(b).status !== "kOk") throw new Error("not kOk");
  const p = decode(b) as any, rep = JSON.parse(new TextDecoder().decode(rd(repPath)));
  const prov = new Map((new TextDecoder().decode(p.sections.find((s: any) => s.type === 7).text) as string).split("\n").map((l) => [l.split(": ")[0]!, l.split(": ").slice(1).join(": ")]));
  const rings = (p.sections.find((s: any) => s.type === 1)?.rings ?? []) as number[][][];
  const idx = rep.sea.ringIdx as number[];
  if (idx.some((i) => i < 0 || i >= rings.length)) throw new Error("ringIdx outside the water section");
  return { rings, seaIdx: new Set(idx), droppedWater: Number(prov.get("dropped.water") ?? 0), seaDropped: rep.sea.dropped, seaDrawn: rep.sea.drawn };
}
const ms = (rs: number[][][]) => { const c = new Map<string, number>(); for (const r of rs) { const k = JSON.stringify(r); c.set(k, (c.get(k) ?? 0) + 1); } return c; };
function diff(a: number[][][], b: number[][][]) { const c = ms(a); for (const r of b) { const k = JSON.stringify(r); c.set(k, (c.get(k) ?? 0) - 1); } let x = 0, y = 0; for (const v of c.values()) { if (v > 0) x += v; if (v < 0) y -= v; } return [x, y] as const; }
const split = (s: Side) => ({ tiger: s.rings.filter((_, i) => !s.seaIdx.has(i)), sea: s.rings.filter((_, i) => s.seaIdx.has(i)) });

// CONTROLS
const kn = side("tb26-out/b/KJFK-tb.bmpk", "tb26-out/b/KJFK-tb.report.json");
const a = split(kn), selfT = diff(a.tiger, a.tiger), selfS = diff(a.sea, a.sea);
const firstT = kn.rings.findIndex((_, i) => !kn.seaIdx.has(i));
const cp: Side = { ...kn, rings: kn.rings.filter((_, i) => i !== firstT), seaIdx: new Set([...kn.seaIdx].map((i) => (i > firstT ? i - 1 : i))) };
const b = split(cp), redT = diff(b.tiger, a.tiger), redS = diff(b.sea, a.sea);
const ctl = selfT[0] === 0 && selfT[1] === 0 && selfS[0] === 0 && selfS[1] === 0 && redT[0] === 0 && redT[1] === 1 && redS[0] === 0 && redS[1] === 0;
console.log(`D4 CONTROLS: self TIGER ${selfT.join("/")} sea ${selfS.join("/")}; first TIGER ring removed: TIGER ${redT.join("/")} sea ${redS.join("/")}; ${ctl ? "AS PREDICTED" : "OFF: the decomposition is not used"}`);
if (!ctl) process.exit(1);

let consequence = 0, inst = 0;
for (const h of ELEVEN) {
  if (sha(rd(`tb26-out/ctl/${h}-ctl.bmpk`)) !== sha(rd(`cp-out/${h}-sea.bmpk`))) { console.log(`${h}: INSTRUMENT: the control's package is not cp-out's`); inst++; continue; }
  const n = side(`tb26-out/b/${h}-tb.bmpk`, `tb26-out/b/${h}-tb.report.json`), o = side(`cp-out/${h}-sea.bmpk`, `tb26-out/ctl/${h}-ctl.report.json`);
  const ns = split(n), os = split(o);
  const [tN, tO] = diff(ns.tiger, os.tiger), [sN, sO] = diff(ns.sea, os.sea);
  const tdN = n.droppedWater - n.seaDropped, tdO = o.droppedWater - o.seaDropped, d = tdN - tdO;
  const ok = (tN === 0 || tO === 0) && tN + tO === Math.abs(d) && (d > 0 ? tO === d : d < 0 ? tN === -d : tN + tO === 0);
  consequence += ok ? 1 : 0;
  console.log(`${h}: TIGER only-new ${tN} only-old ${tO}; TIGER drops NEW ${tdN} OLD ${tdO} (dropped.water ${n.droppedWater}/${o.droppedWater}, sea dropped ${n.seaDropped}/${o.seaDropped}); `
    + `budget consequence ${ok}; SEA only-new ${sN} only-old ${sO}; ringIdx ${n.seaIdx.size}/${o.seaIdx.size} vs drawn-dropped ${n.seaDrawn - n.seaDropped}/${o.seaDrawn - o.seaDropped}`);
}
console.log(`D4: ${consequence} of 11 points read the TIGER difference as a budget consequence${inst ? `; ${inst} INSTRUMENT` : ""}`);
process.exit(consequence === 11 && inst === 0 ? 0 : 1);
