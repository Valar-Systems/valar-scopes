// The values the code PR's SEA-CI tests assert, from the PINNED PROTOTYPE pq4d on the tests' own fixtures (wt-code/proxy/test/
// basemap-seaci-fixtures.ts, imported, so the geometry is the tests' byte for byte) -- never from the code under test. Ruled settings only
// (Q4_LADDER=b Q4_D2=1 Q4_TP=1 Q4_PROV=1 Q4_SEA=1, no plant); the script refuses otherwise. pq4d's refusal counter is process-global,
// so it is reset before every build here (refusedBuilt is per build, R4).
// For the two new fixtures it first FINDS their caps on pq4d: provB's cap = the size of the package it builds at cap (rung 0's size - 1),
// which must land on rung 2 (the script stops otherwise); d2's, D2 off for the probes, d2's cap = (the 162 km floor) - 1, the floor being the smallest cap at which 162 km fits with D2 off.
//   (from wt-main/proxy) Q4_... node --import tsx ../../pq4d-seaci.mts > ../../cp-out/seaci-pq4d.json
import { build, Q4, q4Out } from "./pq4d/src/basemap/build.ts";
import { decode, validate } from "./pq4d/src/basemap/format.ts";
import { Q4P, Q4W, properPairs, q4Stats } from "./pq4d/src/basemap/frame.ts";
import { Q4S } from "./pq4d/src/basemap/sea.ts";
import { nonTerrainBytes } from "./pq4d/src/basemap/budget.ts";
import { lonLatOf, writeSynthStore } from "./pq4d/test/basemap-synth.ts";
import { SEACI, seaciDef, seaciQuery } from "./wt-code/proxy/test/basemap-seaci-fixtures.ts";

// PLANT MODE (SEACI_PLANT=1): the ruled settings plus ONE prototype plant (Q4_SEAPLANT or Q4_BACKSTOP=0 or Q4_PROV unset), every fixture at
// the caps the clean run chose (provB 458, d2 600) -- the readings the sabotage driver's red sets are predicted from.
const PLANT = process.env.SEACI_PLANT === "1";
const ruled = Q4.ladder && Q4.d2 && Q4W.tp && Q4W.backstop && Q4P.prov && Q4S.on && !Q4S.plant;
const onePlant = Q4.ladder && Q4.d2 && Q4W.tp && Q4S.on && ((Q4S.plant !== "") + (!Q4W.backstop ? 1 : 0) + (!Q4P.prov ? 1 : 0)) === 1;
if (PLANT ? !onePlant : !ruled) { console.log(JSON.stringify({ error: PLANT ? "plant mode needs exactly one plant on the ruled settings" : "not the ruled settings" })); process.exit(2); }
const COMMIT = "0".repeat(40);
async function one(name: string, cap?: number) {
  const fx = SEACI[name]!();
  const { store } = await writeSynthStore(seaciDef(fx, lonLatOf) as any);
  q4Stats.refused = 0;
  const r = await build(store, seaciQuery(fx), { commit: COMMIT, ...(cap !== undefined ? { capBytes: cap } : {}) });
  const o: any = { name, cap: cap ?? null, status: r.ok ? 200 : r.status, code: r.ok ? null : (r as any).code, steps: q4Out.steps, refusedBuilt: q4Stats.refused };
  if (!r.ok) { if ("ladder" in r) o.rung = (r as any).ladder.rung; return o; }
  const pkg = decode(r.bytes) as any, water = ((pkg.sections.find((s: any) => s.type === 1)?.rings ?? []) as number[][][]);
  const provT = new TextDecoder().decode(pkg.sections.find((s: any) => s.type === 7).text);
  const pv = (k: string) => provT.split("\n").find((l) => l.startsWith(`${k}: `))?.slice(k.length + 2) ?? null;
  const sea = q4Out.sea, seaIdx: number[] = sea?.ringIdx ?? [];
  Object.assign(o, { kOk: validate(r.bytes).status, rung: r.ladder.rung, radiusMm: pkg.radiusMm, totalBytes: r.bytes.length, ntb: nonTerrainBytes(pkg.sections),
    rings: water.length, seaRings: seaIdx.length, crossings: water.reduce((a, g) => a + properPairs(g as any).length, 0),
    seaLines: provT.split("\n").filter((l) => /^(source|vintage|files|sha256|dropped|collapsed)\.sea: /.test(l)).length,
    collapsedWater: pv("collapsed.water"), droppedWater: pv("dropped.water"), refused: q4Out.refused,
    sea: sea ? { refused: sea.refused, collapsed: sea.collapsed, dropped: sea.dropped, islandsDropped: sea.islandsDropped, islandsSmall: sea.islandsSmall, bridges: sea.bridges,
      outside: sea.outside, drawn: sea.drawn, refused5: sea.refused5, repairs: sea.repairs, repairVertices: sea.repairVertices, fates: (sea.fates ?? []).map((x: any) => x.fate) } : null,
    sha12: Buffer.from(await crypto.subtle.digest("SHA-256", r.bytes)).toString("hex").slice(0, 12) });
  if (name === "edge2") o.edge = seaIdx.map((i) => [...new Set(water[i]!.filter((p) => p[0] === 0).map((p) => p[1]!))].sort((a, b) => a - b));
  return o;
}
async function smallestFit(name: string, lo: number, hi: number): Promise<number> { // D2 off: the smallest cap at which `name` builds
  Q4.d2 = false;
  try { while (hi - lo > 1) { const m = (lo + hi) >> 1; ((await one(name, m)).status === 200 ? (hi = m) : (lo = m)); } return hi; }
  finally { Q4.d2 = true; }
}
const out: Record<string, unknown> = {};
if (PLANT) {
  out.plant = { seaPlant: Q4S.plant, backstop: Q4W.backstop, prov: Q4P.prov };
  for (const n of ["kb1", "kb1c", "edge2", "kb2cross", "tsea0-without", "tsea0-with", "bowtie", "xisland", "xisland-tall", "provA-both", "provA-square"]) out[n] = await one(n);
  out["provB"] = await one("provB", 458); out["d2"] = await one("d2", 600); out["d2-control"] = await one("d2-control", 600);
  console.log(JSON.stringify(out, null, 1)); process.exit(0);
}
for (const n of ["kb1", "kb1c", "edge2", "kb2cross", "tsea0-without", "tsea0-with", "bowtie", "xisland", "xisland-tall", "provA-both", "provA-square"]) out[n] = await one(n);
// provB: rung 1's package bytes (the smallest cap at which it fits at rung <= 1 is rung 0's size, so read rung 1 at cap rung0 - 1)
const pb0 = await one("provB");
const pb1 = await one("provB", pb0.totalBytes - 1);
if (pb1.rung !== 2) { console.log(JSON.stringify({ error: "provB's probe did not land on rung 2", pb0, pb1 })); process.exit(3); }
const capB = pb1.totalBytes; // the rung-2 package's own size: rungs 0 and 1 are over it, rung 2 fits
out["provB-rung0"] = pb0; out["provB"] = { ...(await one("provB", capB)), chosenCap: capB };
// d2: the 162 km floor with D2 off, then the cap one under it, with D2 on; the control at the same cap
const floor162 = await smallestFit("d2", 0, 1 << 20);
const capD = floor162 - 1;
out["d2"] = { ...(await one("d2", capD)), floor162, chosenCap: capD };
out["d2-control"] = { ...(await one("d2-control", capD)), chosenCap: capD };
console.log(JSON.stringify(out, null, 1));
