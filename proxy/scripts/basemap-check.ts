// --check (docs/basemap-package-format.md §7; the route doc's MD3): is this package what its own builder commit builds
// from its own store? Exit 0 identical; 1 any check fails; 2 BLIND (the commit, the store or a source cannot be found),
// never 0. Prints identifiers and counts, never a coordinate.
//
//   npx tsx scripts/basemap-check.ts <package.bmpk> --store <dir> [--sources <dir>]
//
// <dir> holds the store's objects by key (bm/v1/current, bm/v1/<id>/...); --sources holds the source files as the
// ingest caches them (files/<host>/<path>). The rebuild runs the builder code AT THE COMMIT the package's PROVENANCE
// names, checked out into a temporary worktree -- not HEAD's -- so a later commit cannot fail a package it did not build.
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { decode, validate } from "../src/basemap/format";

const argv = process.argv, arg = (n: string) => { const i = argv.indexOf(n); return i > 0 ? argv[i + 1] : undefined; };
const pkgPath = argv[2], storeDir = arg("--store"), sources = arg("--sources");
const out = (code: number, why: string) => { console.log(`check: ${code === 0 ? "IDENTICAL" : code === 1 ? "DIFFERENT" : "BLIND"} -- ${why}`); process.exit(code); };
if (!pkgPath || !storeDir) out(2, "usage: basemap-check.ts <package> --store <dir> [--sources <dir>]");
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const pkg = new Uint8Array(readFileSync(pkgPath!));
if (validate(pkg).status !== "kOk") out(1, `the package does not validate (${validate(pkg).status})`);
const p = decode(pkg);
const prov = new TextDecoder().decode((p.sections.find((s) => s.type === 7) as { text: Uint8Array }).text).split("\n");
const val = (k: string) => prov.find((l) => l.startsWith(`${k}: `))?.slice(k.length + 2);
const builder = /^blipscope-basemap ([0-9a-f]{40})$/.exec(val("builder") ?? "")?.[1], storeId = val("store");
if (!builder) out(2, "no blipscope-basemap builder commit in PROVENANCE");

// the store the package names
const obj = (k: string) => { const f = join(storeDir!, ...k.split("/")); return existsSync(f) ? new Uint8Array(readFileSync(f)) : null; };
const cur = obj("bm/v1/current");
if (!cur) out(2, "the store has no current");
const curId = (JSON.parse(new TextDecoder().decode(cur!)) as { storeId?: string }).storeId;
const manifest = curId ? obj(`bm/v1/${curId}/manifest.json`) : null;
if (!manifest) out(2, "the store has no manifest for current");
if (sha(manifest!).slice(0, 12) !== curId) out(1, "the store's manifest does not hash to current's id");
if (curId !== storeId) out(1, `the package names store ${storeId}, the store is ${curId}`);

// every source file is the one the manifest pins
if (sources) {
  const files = (JSON.parse(new TextDecoder().decode(manifest!)) as { files: Array<{ url: string; sha256: string }> }).files;
  for (const f of files) {
    const u = new URL(f.url), path = join(sources, "files", u.hostname, ...u.pathname.split("/").filter(Boolean));
    if (!existsSync(path)) out(2, `a source file is missing (${u.pathname.split("/").pop()})`);
    if (sha(readFileSync(path)) !== f.sha256) out(1, `a source file is not the one the manifest pins (${u.pathname.split("/").pop()})`);
  }
}

// the rebuild, at the package's own builder commit
const repo = execFileSync("git", ["rev-parse", "--show-toplevel"], { encoding: "utf8" }).trim();
try { execFileSync("git", ["cat-file", "-e", `${builder}^{commit}`], { stdio: "ignore" }); } catch { out(2, `builder commit ${builder!.slice(0, 12)} is not in this repository`); }
const wt = join(mkdtempSync(join(tmpdir(), "bm-check-")), "wt");
let code = 2, why = "the rebuild did not run";
try {
  execFileSync("git", ["-C", repo, "worktree", "add", "--detach", "--quiet", wt, builder!], { stdio: "ignore" });
  const mod = (await import(pathToFileURL(resolve(wt, "proxy/src/basemap/build.ts")).href)) as { build: (s: unknown, q: unknown, o: unknown) => Promise<{ ok: boolean; bytes?: Uint8Array; status?: number }> };
  const eps = val("epsilon.m");
  if (!eps) throw new Error("no epsilon.m");
  const types = new Set(p.sections.map((s) => s.type));
  const mask = [1, 2, 3, 4, 5].reduce((m, t) => (types.has(t) ? m | (1 << (t - 1)) : m), 0) | (types.has(6) ? 32 : 0);
  const levels = p.sections.filter((s) => s.type === 6).map((s) => ({ radiusMm: (s as { levelRadiusMm: number }).levelRadiusMm, n: (s as { n: number }).n }));
  const r = await mod.build({ get: async (k: string) => obj(k) }, { lat0Micro: p.lat0Micro, lon0Micro: p.lon0Micro, rKm: p.radiusMm / 1e6, epsMm: Math.round(Number(eps) * 1000), mask, levels }, { commit: builder });
  if (!r.ok || !r.bytes) { code = 1; why = `the rebuild answered ${r.status}`; }
  else if (Buffer.compare(Buffer.from(r.bytes), Buffer.from(pkg)) === 0) { code = 0; why = `rebuilt at ${builder!.slice(0, 12)}: ${r.bytes.length} B, byte for byte`; }
  else { code = 1; why = `rebuilt at ${builder!.slice(0, 12)}: ${r.bytes.length} B against ${pkg.length} B, different`; }
} catch (e) { code = 2; why = `the rebuild could not run: ${(e as Error).message.slice(0, 160)}`; }
finally {
  try { execFileSync("git", ["-C", repo, "worktree", "remove", "--force", wt], { stdio: "ignore" }); } catch { /* reported below */ }
  if (existsSync(wt)) console.log(`note: the check's worktree was left at ${wt}`);
  rmSync(join(wt, ".."), { recursive: true, force: true });
}
out(code, why);
