// ---- SCRATCH Q-W4 part 2, pq4d: the SEA PATH behind Q4_SEA=1 (docs/basemap-dense-code.md, #652; the 04:12:32Z ruling).
//   sea.ts (pq4d-sea.part.ts, verbatim) holds the path. store.ts gains kind sea = 6 (attrs: area, src); provenance.ts gains the sea
//   layer after water; LayerSet keeps sea parts and draws them into its water rings; the builder routes sea features to the water sets
//   (eps, x2, x8) and reports the drawn set's sea counts, the pieces the ladder dropped, the sea's package bytes and every piece's fate.
//   With Q4_SEA unset, and on a store with no sea feature, every added path is inert (pq4d's own control, registered with part 2).
src["sea.ts"] = readFileSync(new URL("./pq4d-sea.part.ts", import.meta.url), "utf8").replace(/\r/g, "");
files.push("sea.ts");
// the 06:35:32Z ruling (KB8): frame.ts gains the edge-locked simplify (pq4d-frame-lock.part.ts, verbatim), before waterQ4
fr = src["frame.ts"];
fr = before(fr, "export function waterQ4(ringKm: Pt[], epsM: number, rMm: number): Pt[] {\n",
  readFileSync(new URL("./pq4d-frame-lock.part.ts", import.meta.url), "utf8").replace(/\r/g, ""), "frame waterQ4Locked");
src["frame.ts"] = fr;

let st = src["store.ts"];
st = swap(st, 'export type FeatureKind = "water" | "rivers" | "roads" | "peaks" | "places";\n',
  'export type FeatureKind = "water" | "rivers" | "roads" | "peaks" | "places" | "sea"; // SCRATCH Q-W4 part 2: sea, kind 6\n', "store kind type");
st = swap(st, "const KIND_CODE: Record<FeatureKind, number> = { water: 1, rivers: 2, roads: 3, peaks: 4, places: 5 };\n",
  "const KIND_CODE: Record<FeatureKind, number> = { water: 1, rivers: 2, roads: 3, peaks: 4, places: 5, sea: 6 }; // SCRATCH Q-W4 part 2\n", "store KIND_CODE");
st = swap(st, 'const KIND_OF: Record<number, FeatureKind> = { 1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places" };\n',
  'const KIND_OF: Record<number, FeatureKind> = { 1: "water", 2: "rivers", 3: "roads", 4: "peaks", 5: "places", 6: "sea" }; // SCRATCH Q-W4 part 2\n', "store KIND_OF");
st = after(st, '  else if (f.kind === "peaks") { nums.push(Number(f.attrs["feature_id"])); t(f.attrs["feature_name"]); }\n',
  '  else if (f.kind === "sea") { nums.push(Number(f.attrs["area"])); t(f.attrs["src"]); } // SCRATCH Q-W4 part 2\n', "store encode sea");
st = after(st, "    : kind === \"peaks\" ? { feature_id: nums[0]!, feature_name: texts[0]! }\n",
  "    : kind === \"sea\" ? { area: nums[0]!, src: texts[0]! } // SCRATCH Q-W4 part 2\n", "store decode sea");
src["store.ts"] = st;

let pv = src["provenance.ts"];
pv = swap(pv, 'export const LAYER_ORDER = ["water", "rivers", "roads", "peaks", "places", "population", "terrain"] as const;\n',
  'export const LAYER_ORDER = ["water", "sea", "rivers", "roads", "peaks", "places", "population", "terrain"] as const; // SCRATCH Q-W4 part 2: + sea\n', "provenance LAYER_ORDER");
src["provenance.ts"] = pv;

ly = src["layers.ts"];
ly = swap(ly, "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4, Q4P, REFUSED,\n} from \"./frame\";\n",
  "  simplifyRing, toFrameKm, within3km, Q4W, waterQ4, Q4P, REFUSED,\n} from \"./frame\";\nimport { type SeaCounts, type SeaOut, type SeaPart, newSeaCounts, seaRings, Q4S } from \"./sea\"; // SCRATCH Q-W4 part 2\n", "layers import sea");
ly = swap(ly, 'export type LayerKind = "water" | "rivers" | "roads" | "peaks" | "places";\n',
  'export type LayerKind = "water" | "rivers" | "roads" | "peaks" | "places" | "sea"; // SCRATCH Q-W4 part 2\n', "layers LayerKind");
ly = after(ly, "  private places: Array<Key & Place> = [];\n",
  "  private sea: SeaPart[] = []; // SCRATCH Q-W4 part 2: sea parts, unclipped, in the frame\n  seaN: SeaCounts = newSeaCounts(); seaOut: SeaOut[] | null = null; // SCRATCH Q-W4 part 2\n", "layers sea fields");
ly = before(ly, "      case \"rivers\": {\n",
  "      case \"sea\": { // SCRATCH Q-W4 part 2: kept whole; seaRings() clips, simplifies, bridges and draws them\n" +
  "        this.sea.push({ file: ft.file, record: ft.record, part: ft.part, km: openRing(lonLat(ft.xy)).map(([x, y]) => toFrameKm(f, x, y)),\n" +
  "          lock: openRing(lonLat(ft.xy)).map(([x, y]) => Math.abs(x - Math.round(x)) < 1e-9 || Math.abs(y - Math.round(y)) < 1e-9) }); // KB8 (06:35:32Z): on its cell's boundary\n" +
  "        return;\n      }\n", "layers case sea");
ly = swap(ly, "    const recs = [...this.water].sort(bySource);\n    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring && !(Q4P.prov && (r as any).refused)).length, refused: recs.filter((r) => (r as any).refused).length } as any; // SCRATCH Q-W4 (C)\n",
  "    if (Q4S.on && this.seaOut === null) this.seaOut = seaRings(this.sea, this.f, this.seaN); // SCRATCH Q-W4 part 2\n" +
  "    const seaRecs = (this.seaOut ?? []).flatMap((s) => (s.ring ? [{ file: s.file, record: s.record, part: 0, ring: s.ring }, ...(s.plantedRings ?? []).map((r, i) => ({ file: s.file, record: s.record, part: i + 1, ring: r }))] : []));\n" +
  "    const recs = [...this.water, ...seaRecs].sort(bySource);\n" +
  "    return { rings: recs.flatMap((r) => (r.ring ? [r.ring] : [])), collapsed: recs.filter((r) => !r.ring && !(Q4P.prov && (r as any).refused)).length, refused: recs.filter((r) => (r as any).refused).length,\n" +
  "      sea: { n: this.seaN, fates: this.seaOut ?? [], set: new Set(seaRecs.map((r) => r.ring)) } } as any; // SCRATCH Q-W4 (C), part 2\n", "layers waterRings sea");
src["layers.ts"] = ly;

bd = src["build.ts"];
bd = after(bd, "import { type Box, CURRENT_KEY, type StoreIndex, boxMeets, covered, decodeCell, decodeTerrariumPng, storeKeys, tileBitsOf } from \"./store\";\n",
  "import { Q4S, SeaRepairStop, ringBytes } from \"./sea\"; // SCRATCH Q-W4 part 2; KB4 (B')\n", "build import sea");
bd = swap(bd, "export const q4Out = { refused: -1, steps: 0 }; // SCRATCH Q-W4 (C, ruling 3): the drawn set's refusals and D2's step count, as bm_build would carry them\n",
  "export const q4Out = { refused: -1, steps: 0, sea: null as any }; // SCRATCH Q-W4 (C, ruling 3; part 2's sea): what bm_build would carry\n", "build q4Out sea");
bd = swap(bd, "  const kinds = ([\"water\", \"rivers\", \"roads\", \"peaks\", \"places\"] as const).filter((k) => want(MASK[k]));\n",
  "  const kinds = [...([\"water\", \"rivers\", \"roads\", \"peaks\", \"places\"] as const).filter((k) => want(MASK[k])), ...(Q4S.on && want(MASK.water) ? [\"sea\" as const] : [])]; // SCRATCH Q-W4 part 2\n", "build kinds sea");
bd = swap(bd, "          if (ft.kind === \"water\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft);\n",
  "          if (ft.kind === \"water\" || ft.kind === \"sea\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft); // SCRATCH Q-W4 part 2: + sea\n", "build L2 sea");
bd = swap(bd, "          if (L8 && ft.kind === \"water\") L8.add(ft.kind, ft); // SCRATCH Q-W4\n",
  "          if (L8 && (ft.kind === \"water\" || ft.kind === \"sea\")) L8.add(ft.kind, ft); // SCRATCH Q-W4, part 2: + sea\n", "build L8 sea");
bd = swap(bd, "    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; (c as any).refusedW = (w as any).refused; } // SCRATCH Q-W4 (C)\n",
  "    if (want(MASK.water)) { const w = L.waterRings(); c.water = w.rings; c.collapsed.water = w.collapsed; (c as any).refusedW = (w as any).refused; (c as any).seaW = (w as any).sea; if ((w as any).sea?.fates.length) (c.collapsed as any).sea = (w as any).sea.n.collapsed; } // SCRATCH Q-W4 (C), part 2\n", "build content sea");
bd = after(bd, "  q4Out.refused = fitted ? ((fitted.content as any).refusedW ?? 0) : -1; // SCRATCH Q-W4 (C): the set the package draws\n",
  "  if (Q4S.on) { // SCRATCH Q-W4 part 2: the drawn set's sea, the pieces the ladder dropped, the sea's bytes, every piece's fate\n" +
  "    const sw = fitted ? (fitted.content as any).seaW : null, set: Set<unknown> = sw?.set ?? new Set(), fin: unknown[] = fitted ? ((fitted.content as any).water ?? []) : [];\n" +
  "    const present = new Set(fin.filter((r) => set.has(r)));\n" +
  "    const fates = (sw?.fates ?? []).map((s: any) => ({ file: s.file, record: s.record, fate: s.fate === \"drawn\" && !present.has(s.ring) ? \"dropped\" : s.fate, q: s.q }));\n" +
  "    const ringIdx = fin.flatMap((r, i) => (set.has(r) ? [i] : [])); // the sea rings' positions in the package's water section\n" +
  "    q4Out.sea = { ...(sw?.n ?? {}), dropped: fates.filter((x: any) => x.fate === \"dropped\").length, bytes: [...present].reduce((a: number, r: any) => a + ringBytes(r), 0), fates, ringIdx };\n" +
  "  }\n", "build q4Out.sea");
bd = before(bd, "      if (want(MASK.places)) for (const l of [\"places\", \"population\"] as const) read[l] = manifest.files.filter((f) => f.layer === l).map((f) => fileRef(f.url));\n",
  "      if (Q4S.on && want(MASK.water)) { const s = [...new Set(met)].filter((u) => byUrl.get(u)?.layer === \"sea\").sort().map(fileRef); if (s.length) read.sea = s; } // SCRATCH Q-W4 part 2 (05:11:57Z ruling): the sea's files read (RD-P6), only when one is met\n", "build read.sea");
// KB4 (B') (registered at cf2f65300e89): a sea repair that cannot finish STOPS the build: 500 with its code, never a partial package.
bd = swap(bd, "  | { ok: false; status: 422; code: \"too_large\"; ladder: Ladder };\n",
  "  | { ok: false; status: 422; code: \"too_large\"; ladder: Ladder }\n  | { ok: false; status: 500; code: \"sea_repair_failed\" | \"sea_repair_limit\" | \"sea_repair_state\"; why: string }; // SCRATCH KB4 (B')\n", "build result 500");
bd = swap(bd, "  const at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;\n  if (L4) at[4 * q.epsMm] = content(L4); if (L8) at[8 * q.epsMm] = content(L8); // SCRATCH Q-W4\n",
  "  let at: Record<number, Content>;\n  try { // SCRATCH KB4 (B'): a sea repair that cannot finish stops the build here\n" +
  "    at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;\n" +
  "    if (L4) at[4 * q.epsMm] = content(L4); if (L8) at[8 * q.epsMm] = content(L8); // SCRATCH Q-W4\n" +
  "  } catch (e) { if (e instanceof SeaRepairStop) return { ok: false, status: 500, code: e.code, why: e.message }; throw e; }\n", "build content stop");
src["build.ts"] = bd;

bg = src["budget.ts"];
bg = swap(bg, "    const c2 = (process.env.Q4_PROV === \"1\" ? { ...content, water: water8, collapsed: { ...(content as any).collapsed, water: m8.collapsed?.water }, refusedW: m8.refusedW } : { ...content, water: water8 }) as C;\n",
  "    const c2 = (process.env.Q4_PROV === \"1\" ? { ...content, water: water8, collapsed: { ...(content as any).collapsed, water: m8.collapsed?.water, sea: m8.collapsed?.sea }, refusedW: m8.refusedW, seaW: m8.seaW } : { ...content, water: water8 }) as C; // SCRATCH Q-W4 part 2: + sea\n", "budget c2 sea");
bg = swap(bg, "    const c4 = (): C => { const m4 = make(tol4) as any; return (process.env.Q4_PROV === \"1\" ? { ...m4, water: water8, collapsed: { ...m4.collapsed, water: m8.collapsed?.water }, refusedW: m8.refusedW } : { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } }) as C; }; // SCRATCH Q-W4 (C)\n",
  "    const c4 = (): C => { const m4 = make(tol4) as any; return (process.env.Q4_PROV === \"1\" ? { ...m4, water: water8, collapsed: { ...m4.collapsed, water: m8.collapsed?.water, sea: m8.collapsed?.sea }, refusedW: m8.refusedW, seaW: m8.seaW } : { ...m4, water: water8, collapsed: { ...m4.collapsed, water: (content as any).collapsed?.water } }) as C; }; // SCRATCH Q-W4 (C), part 2: + sea\n", "budget c4 sea");
src["budget.ts"] = bg;

