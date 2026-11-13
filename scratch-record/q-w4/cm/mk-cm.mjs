// SCRATCH, for the code PR's memory and run rows (docs/basemap-dense-code.md: CM-0/CM-P/CM-M and the memory rows (5.1-5.5), SEA-MEM,
// R-D2, R-KMSP). Two trees, each taken from its git COMMIT (blobs via `git show`, never a working tree), the whole of proxy/src:
//   <out>/cp/proxy/src    the code PR's, BYTE-IDENTICAL to its commit. It ships its own phase hook (`buildHooks.phase`, null in
//                         production; doc "The harness's extras and the phase hook"): the six phase calls plus D2's step line.
//   <out>/main/proxy/src  main's (the PR's merge-base, scope ruling u4), with ONLY the seven sampling lines of mk-hooks.mjs (15ae8e946bb0)
//                         added to build.ts, calling globalThis.__mem.
// Refuses unless: no blob carries a CR; every mk-hooks anchor matches main's build.ts exactly once; main's hooked build.ts differs from
// its blob by those seven added lines alone; every other file is byte-identical to its blob; the hooked main and the code PR call the
// same six phases in the same order; the code PR's build.ts holds `export const buildHooks` and the one D2 step call.
//   node mk-cm.mjs <repo dir> <code PR commit> <main commit> <out dir>
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";

const [repo, cpC, mainC, out] = process.argv.slice(2);
const die = (m) => { console.log(`REFUSED: ${m}`); process.exit(2); };
if (!repo || !cpC || !mainC || !out) die("usage");
if (existsSync(join(out, "cp")) || existsSync(join(out, "main"))) die(`${out} already holds cp/ or main/ (a fresh directory only)`);
const git = (...a) => execFileSync("git", ["-C", repo, ...a], { encoding: "buffer", maxBuffer: 1 << 28 });
const sha = (b) => createHash("sha256").update(b).digest("hex");
const full = (c) => git("rev-parse", `${c}^{commit}`).toString().trim();
const CP = full(cpC), MAIN = full(mainC);

// mk-hooks.mjs's seven hooks, verbatim: [anchor, insertion, where]
const HOOKS = [
  ["  const f1 = makeFrame(q.lat0Micro, q.lon0Micro, rMm, q.epsMm), f2 = makeFrame(q.lat0Micro, q.lon0Micro, rMm, EPS_STEP * q.epsMm);", "  const MEM = (globalThis as any).__mem as ((t: string) => void) | undefined; // SCRATCH hook", "after"],
  ["          if (ft.kind === \"water\" || ft.kind === \"rivers\" || ft.kind === \"roads\") L2.add(ft.kind, ft);", null, "cell"],
  ["      } else bitsBin = b;", "      MEM?.(\"tile\"); // SCRATCH hook", "after"],
  ["  let peaks: Array<{ q: Pt; elevM: number; name: string }> = [];", "  MEM?.(\"tiles\"); // SCRATCH hook", "before"],
  ["  // the tiles read: their files (by RD17's /9/<x>/<y>.png tail) and their source bits (the credits)", "  MEM?.(\"terrain\"); // SCRATCH hook", "before"],
  ["  const at = { [q.epsMm]: content(L1), [EPS_STEP * q.epsMm]: content(L2) } as Record<number, Content>;", "  MEM?.(\"layers\"); // SCRATCH hook", "after"],
  ["  const fitted = fitBudget(q.epsMm, opts.capBytes ?? CAP_BYTES, (tol) => at[tol]!, assemble, ladder);", "  MEM?.(\"ladder\"); // SCRATCH hook", "after"],
];
function hook(src) {
  const lines = src.split("\n");
  for (const [anchor, ins, where] of HOOKS) {
    const at = lines.flatMap((l, i) => (l === anchor ? [i] : []));
    if (at.length !== 1) die(`main: anchor matched ${at.length} times: ${anchor.slice(0, 60)}`);
    const i = at[0];
    if (where === "after") lines.splice(i + 1, 0, ins);
    else if (where === "before") lines.splice(i, 0, ins);
    else {
      if (lines[i + 1] !== "        }") die("main: the cell hook's closing brace is not where expected");
      lines.splice(i + 2, 0, "        MEM?.(\"cell\"); // SCRATCH hook");
    }
  }
  return lines.join("\n");
}
const phasesOf = (s) => [...s.matchAll(/MEM\?\.\("(\w+)"\)/g)].map((m) => m[1]);
const report = {};
for (const [tag, c] of [["cp", CP], ["main", MAIN]]) {
  const files = git("ls-tree", "-r", "--name-only", c, "--", "proxy/src").toString().trim().split("\n");
  let n = 0;
  for (const f of files) {
    const blob = git("show", `${c}:${f}`);
    if (blob.includes(13)) die(`${tag} ${f} carries a CR`);
    let body = blob;
    if (tag === "main" && f === "proxy/src/basemap/build.ts") {
      const h = hook(blob.toString("utf8"));
      const a = blob.toString("utf8").split("\n"), b = h.split("\n");
      if (b.filter((l) => l.endsWith("// SCRATCH hook")).length !== 7 || b.length !== a.length + 7 || b.filter((l) => !l.endsWith("// SCRATCH hook")).join("\n") !== a.join("\n")) die("main build.ts: not exactly the seven hook lines added");
      body = Buffer.from(h);
      report.mainBuild = { blob: sha(blob).slice(0, 12), hooked: sha(body).slice(0, 12), phases: phasesOf(h) };
    }
    if (tag === "cp" && f === "proxy/src/basemap/build.ts") {
      const s = blob.toString("utf8");
      if (!s.includes("export const buildHooks: { phase: ((phase: string) => void) | null } = { phase: null };")) die("cp build.ts: no buildHooks export");
      if (s.split('buildHooks.phase?.("d2")').length !== 2) die("cp build.ts: not exactly one D2 step call");
      if (s.split("const MEM = buildHooks.phase;").length !== 2) die("cp build.ts: not exactly one MEM binding");
      report.cpBuild = { blob: sha(blob).slice(0, 12), phases: phasesOf(s) };
    }
    const p = join(out, tag, f); mkdirSync(dirname(p), { recursive: true }); writeFileSync(p, body); n++;
  }
  report[tag] = { commit: c.slice(0, 12), files: n, basemapTree: git("rev-parse", `${c}:proxy/src/basemap`).toString().trim().slice(0, 12) };
}
const want = ["cell", "tile", "tiles", "terrain", "layers", "ladder"];
if (report.mainBuild.phases.join() !== want.join()) die(`main's hooked phases ${report.mainBuild.phases}`);
if (report.cpBuild.phases.join() !== want.join()) die(`the code PR's phases ${report.cpBuild.phases}`);
console.log(JSON.stringify(report));
console.log("OK");
