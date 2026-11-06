// MD2 (docs/basemap-route.md A1.7): the same 50 builds in Node and in workerd give identical sha256 lists.
//
//   npx tsx scripts/basemap-md2.ts             # the check
//   npx tsx scripts/basemap-md2.ts --control   # its control: one sha256 changed in the list workerd is handed; must exit 1
//
// Builds ME2's first 50 synthetic stores here, in Node, then runs test/basemap-md2.test.ts in workerd with the Node
// list handed in as a pool binding (MD2_NODE_SHAS), and reads vitest's JSON report. Exit 0 identical, 1 different,
// 2 BLIND: the workerd test skipped, did not run, or the report could not be read -- never 0.
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "../src/basemap/build";
import { COMMIT, me2Store } from "../test/basemap-synth";

const PROXY = join(dirname(fileURLToPath(import.meta.url)), "..");
const shas: string[] = [];
for (let i = 0; i < 50; i++) {
  const s = await me2Store(i);
  const r = await build(s.store, s.query, { commit: COMMIT });
  shas.push(r.ok ? createHash("sha256").update(r.bytes).digest("hex") : `${r.status}:${r.code}`);
}
if (process.argv.includes("--control")) shas[7] = "0".repeat(64);
const list = shas.join(",");
console.log(`MD2: Node built 50 (${shas.filter((x) => /^[0-9a-f]{64}$/.test(x)).length} packages); list digest ${createHash("sha256").update(list).digest("hex").slice(0, 12)}`);

const dir = mkdtempSync(join(tmpdir(), "bm-md2-")), out = join(dir, "r.json");
const r = spawnSync("npx", ["vitest", "run", "test/basemap-md2.test.ts", "--reporter=json", `--outputFile=${out}`], { cwd: PROXY, shell: true, encoding: "utf8", env: { ...process.env, MD2_NODE_SHAS: list } });
let status = "unread";
try {
  const j = JSON.parse(readFileSync(out, "utf8")) as { testResults?: Array<{ assertionResults?: Array<{ title: string; status: string }> }> };
  status = j.testResults?.flatMap((f) => f.assertionResults ?? []).find((a) => a.title.startsWith("MD2"))?.status ?? "absent";
} catch { /* unread */ }
rmSync(dir, { recursive: true, force: true });
console.log(`MD2: workerd ${status}`);
if (status === "passed") process.exit(0);
if (status === "failed") { console.log((r.stdout ?? "").split("\n").filter((l) => /MD2|expected/.test(l)).slice(0, 6).join("\n")); process.exit(1); }
console.log("MD2: BLIND -- the workerd half did not run with the Node list");
process.exit(2);
