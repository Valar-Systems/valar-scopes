// routes-fixture.mts -- TESTS ONLY. Builds synthetic standing-data snapshots for routes-writer.selftest.sh, so the
// key-level writer can be graded red-first through the fake KV with no network and no real source
// (docs/route-weekly-code.md, section 2c). Usage: npx tsx scripts/routes-fixture.mts <outdir>
//
// The sizes are chosen to pass the guards a real run must pass: G1's absolute band (400,000-900,000 usable rows) and
// 99% ratio, G2's anchors (scripts/routes-anchors.json, included in every snapshot and never printed), G3's surviving
// share, G4 (no removed shard) and G5's cap (deletes under 250). The hash rule is imported from routerule.ts, never
// restated, so the manifest this writes is the one the writer itself would compute.
//
// Writes, in <outdir>:
//   prev.tar.gz      the snapshot the store's meta:routes was built from
//   new.tar.gz       prev with CHANGED values changed, ADDED callsigns added and DELETED callsigns removed
//   pfx.tar.gz       new plus ONE callsign that renders as a key outside ^rt:[A-Z0-9]{2,8}$ (the PFX1 plant)
//   meta-prev.json   prev's manifest, as meta:routes would hold it
//   expect.json      the counts and the new manifest, for the assertions (keys, never printed by the selftest)
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { RULE_REV } from "./routerule";

const out = process.argv[2];
if (!out) { console.error("usage: routes-fixture.mts <outdir>"); process.exit(2); }
mkdirSync(out, { recursive: true });

export const COMMITS = { prev: "1".repeat(40), new: "2".repeat(40), pfx: "3".repeat(40) };
const SHARDS = 41, PER = 10_000, CHANGED = 30, ADDED = 40, DELETED = 25;
const anchors = (JSON.parse(readFileSync("scripts/routes-anchors.json", "utf8")) as { anchors: string[] }).anchors
  .map((k) => k.replace(/^rt:/, ""));
const HEADER = "﻿Callsign,Code,Number,AirlineCode,AirportCodes";
const AIRPORTS = "Code,Name,ICAO,IATA\nKAAA,Alpha,KAAA,AAA\nKBBB,Bravo,KBBB,BBB\nKCCC,Charlie,KCCC,CCC\n";
const letters = (i: number) => String.fromCharCode(65 + Math.floor(i / 26) % 26) + String.fromCharCode(65 + (i % 26));

type Rows = Map<string, [string, string][]>; // shard path -> [callsign, airport codes]
function base(): Rows {
  const r: Rows = new Map();
  for (let s = 0; s < SHARDS; s++) {
    const p = letters(s + 26); // BA.. so no shard starts with the anchors' own letters by accident of naming
    const rows: [string, string][] = [];
    for (let i = 0; i < PER; i++) rows.push([`${p}${String(i).padStart(5, "0")}`, "KAAA-KBBB"]);
    r.set(`${p[0]}/${p}-all.csv`, rows);
  }
  r.set("Z/ZY-anchors.csv", anchors.map((cs) => [cs, "KAAA-KCCC"]));
  return r;
}
function variant(kind: "new" | "pfx"): Rows {
  const r = base();
  const [s0, s1, s2] = [...r.keys()];
  const changedRows = r.get(s0 as string) as [string, string][];
  for (let i = 0; i < CHANGED; i++) (changedRows[i] as [string, string])[1] = "KBBB-KCCC";
  const addRows = r.get(s1 as string) as [string, string][];
  for (let i = 0; i < ADDED; i++) addRows.push([`QX${String(i).padStart(5, "0")}`, "KCCC-KAAA"]);
  r.set(s2 as string, (r.get(s2 as string) as [string, string][]).slice(DELETED));
  if (kind === "pfx") addRows.push(["ABCDEFGHIJ", "KAAA-KBBB"]); // 10 characters: outside the 2-8 of the mirror's shape
  return r;
}
const csv = (rows: [string, string][]) =>
  [HEADER, ...rows.map(([cs, codes], i) => `${cs},${cs},${i},${cs.slice(0, 2)},${codes}`)].join("\n") + "\n";
const hash = (raw: string) => createHash("sha256").update("rule" + RULE_REV).update(raw).digest("hex").slice(0, 16);

function snapshot(name: string, commit: string, rows: Rows): Record<string, string> {
  const root = join(out, `standing-data-${commit}`);
  rmSync(root, { recursive: true, force: true });
  mkdirSync(join(root, "airports", "schema-01", "K"), { recursive: true });
  writeFileSync(join(root, "airports", "schema-01", "K", "K.csv"), AIRPORTS);
  const manifest: Record<string, string> = {};
  for (const [path, rs] of rows) {
    const dir = join(root, "routes", "schema-01", path.split("/")[0] as string);
    mkdirSync(dir, { recursive: true });
    const raw = csv(rs);
    writeFileSync(join(dir, path.split("/")[1] as string), raw);
    manifest[`routes/schema-01/${path}`] = `${hash(raw)}:${rs.length}`; // parseSource's name: the path under the root
  }
  execFileSync("tar", ["czf", `${name}.tar.gz`, `standing-data-${commit}`], { cwd: out });
  rmSync(root, { recursive: true, force: true });
  return manifest;
}

const prevManifest = snapshot("prev", COMMITS.prev, base());
const newRows = variant("new");
const newManifest = snapshot("new", COMMITS.new, newRows);
snapshot("pfx", COMMITS.pfx, variant("pfx"));
const usable = (r: Rows) => [...r.values()].reduce((n, rs) => n + rs.length, 0);
writeFileSync(join(out, "meta-prev.json"), JSON.stringify({ v: 1, ruleRev: RULE_REV, rows: usable(base()), shards: prevManifest }));
const [s0, s1, s2] = [...newRows.keys()];
const changedKeys = (newRows.get(s0 as string) as [string, string][]).slice(0, CHANGED).map(([cs]) => `rt:${cs}`);
const addedKeys = Array.from({ length: ADDED }, (_, i) => `rt:QX${String(i).padStart(5, "0")}`);
const deletedKeys = (base().get(s2 as string) as [string, string][]).slice(0, DELETED).map(([cs]) => `rt:${cs}`);
void s1;
writeFileSync(join(out, "expect.json"), JSON.stringify({
  commits: COMMITS, rows: { prev: usable(base()), new: usable(newRows) },
  writes: CHANGED + ADDED, changed: CHANGED, added: ADDED, deletes: DELETED,
  changedKeys, addedKeys, deletedKeys, newManifest,
}));
console.log(`fixture: prev ${usable(base())} rows, new ${usable(newRows)} rows; writes ${CHANGED + ADDED}, deletes ${DELETED}`);
