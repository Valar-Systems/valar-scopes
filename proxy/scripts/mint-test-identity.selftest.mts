// The mint script's selftest (docs/test-identity.md, TM1 and TM1s): mint-test-identity.ts against a LOCAL STUB of the
// provision route, with a scratch token file outside any git tree. EXAMPLE values only: no real token, id or key.
// Then its sabotage, S-TMPRINT: a copy that prints the key must be caught by the output scan, or the scan proves
// nothing. Exit 0 every row as predicted, 1 any row off.
//
//   npx tsx scripts/mint-test-identity.selftest.mts
import { spawn } from "node:child_process";
import { chmodSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { deviceIdFromMac } from "../src/provision.ts";
import { tokenFileKeys } from "./token-file.ts";

const HERE = dirname(fileURLToPath(import.meta.url)), PROXY = join(HERE, "..");
const KEY_PROD = "a".repeat(64), KEY_STAGING = "b".repeat(64); // EXAMPLE stub keys, not real ones
let mode: "prod" | "staging" | "wrongid" = "prod";
const server = createServer((req, res) => {
  let body = "";
  req.on("data", (c) => { body += c; });
  req.on("end", async () => {
    if (req.method !== "POST" || req.url !== "/blipscope/provision" || req.headers["x-provision-token"] !== "EXAMPLE-provision-token") {
      res.writeHead(403, { "content-type": "application/json" }); res.end('{"error":"forbidden"}'); return;
    }
    const mac = (JSON.parse(body) as { mac: string }).mac;
    const deviceId = mode === "wrongid" ? "0123456789abcde".concat("f") : await deviceIdFromMac(mac); // built at run time: no 16-hex literal in the repo
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({ deviceId, key: mode === "staging" ? KEY_STAGING : KEY_PROD, mintsToday: 1 }));
  });
});
await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
const base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;

const T = mkdtempSync(join(tmpdir(), "mint-selftest-"));
const ptoken = join(T, "provision-token"); writeFileSync(ptoken, "EXAMPLE-provision-token\n");
// chmod 600: token-file.ts refuses, on POSIX, a token file others can read. Without it every run is refused on Linux
// (CI on 3f9e655), while Windows, where the mode is not checked, passed every row.
const fresh = (name: string, text = "# EXAMPLE token file\nkv-read=EXAMPLE-not-a-token\nblip-device=0000000000000000\n") => { const f = join(T, name); writeFileSync(f, text); chmodSync(f, 0o600); return f; };
// ASYNC on purpose: the stub runs in THIS process, so a blocking spawnSync would starve it and the child would wait
// forever on its fetch (the first version of this selftest deadlocked exactly that way).
const run = (script: string, tokens: string, args: string[]) => new Promise<{ code: number | null; out: string }>((done) => {
  const c = spawn("npx", ["tsx", script, ...args, "--base", base, "--provision-token-file", ptoken], {
    cwd: PROXY, shell: true, env: { ...process.env, BLIPSCOPE_TOKEN_FILE: tokens },
  });
  let out = "";
  c.stdout.on("data", (d) => { out += d; }); c.stderr.on("data", (d) => { out += d; });
  c.on("close", (code) => done({ code, out }));
});
const field = (f: string, k: string) => new RegExp(`^${k}=(.*)$`, "m").exec(readFileSync(f, "utf8"))?.[1]?.trim() ?? "";
const count = (hay: string, needles: string[]) => needles.filter(Boolean).reduce((n, s) => n + hay.split(s).length - 1, 0);
let off = 0;
const row = (name: string, ok: boolean, detail: string) => { if (!ok) off++; console.log(`${name}: ${ok ? "as predicted" : "OFF-PREDICTION"} -- ${detail}`); };
const MINT = "scripts/mint-test-identity.ts";

// TM1: production writes exactly test-mac, test-device, test-key; prints none of them; a second run refuses
const f1 = fresh("tokens-prod");
const a = await run(MINT, f1, ["--env", "production"]);
const k1 = [...tokenFileKeys(readFileSync(f1, "utf8"))].sort().join(",");
const secrets1 = [KEY_PROD, field(f1, "test-mac"), field(f1, "test-device")];
row("TM1", a.code === 0 && k1 === "blip-device,kv-read,test-device,test-key,test-mac" && count(a.out, secrets1) === 0,
  `exit ${a.code}; fields now {${k1}}; key/MAC/id in the output: ${count(a.out, secrets1)} times`);
const b = await run(MINT, f1, ["--env", "production"]);
row("TM1 (second run)", b.code === 2 && /already in the token file/.test(b.out), `exit ${b.code}; refuses because the fields exist`);

// TM1s: staging writes exactly test-key-staging from test-mac; prints nothing secret; a different id is refused
mode = "staging";
const c = await run(MINT, f1, ["--env", "staging"]);
const k2 = [...tokenFileKeys(readFileSync(f1, "utf8"))].sort().join(",");
row("TM1s", c.code === 0 && k2 === "blip-device,kv-read,test-device,test-key,test-key-staging,test-mac" && count(c.out, [...secrets1, KEY_STAGING]) === 0,
  `exit ${c.code}; fields now {${k2}}; secrets in the output: ${count(c.out, [...secrets1, KEY_STAGING])} times`);
const f2 = fresh("tokens-wrongid"); mode = "prod"; await run(MINT, f2, ["--env", "production"]); mode = "wrongid";
const d = await run(MINT, f2, ["--env", "staging"]);
// by its REASON: an exit code alone passed in CI when every run was refused for another reason
const byReason = /the route's id equals test-device: no/.test(d.out);
row("TM1s (a different id)", d.code === 2 && byReason && !tokenFileKeys(readFileSync(f2, "utf8")).has("test-key-staging"), `exit ${d.code}; refused because the id differs: ${byReason}; test-key-staging written: ${tokenFileKeys(readFileSync(f2, "utf8")).has("test-key-staging")}`);

// records-device, in place (Q-T4)
const e = await run(MINT, f1, ["--add-records-device"]);
row("TM-R", e.code === 0 && field(f1, "records-device") === "0000000000000000" && count(e.out, ["0000000000000000"]) === 0, `exit ${e.code}; records-device equals blip-device; the id in the output: ${count(e.out, ["0000000000000000"])} times`);

// S-TMPRINT: a copy that prints the key. The scan must catch it, or TM1's zero means nothing.
const src = readFileSync(join(PROXY, MINT), "utf8");
const anchor = 'return say("wrote test-mac, test-device and test-key (values not shown)");';
if (src.split(anchor).length !== 2) { row("S-TMPRINT", false, "anchor not found exactly once (BLIND)"); }
else {
  const sab = join(PROXY, "scripts", "zz-mint-sabotaged.ts");
  writeFileSync(sab, src.replace(anchor, 'return say("wrote " + got.key); // SABOTAGE S-TMPRINT'));
  try {
    mode = "prod";
    const f3 = fresh("tokens-sab");
    const s = await run("scripts/zz-mint-sabotaged.ts", f3, ["--env", "production"]);
    row("S-TMPRINT", count(s.out, [KEY_PROD]) > 0, `the sabotaged copy's output holds the key ${count(s.out, [KEY_PROD])} time(s): the scan sees it`);
  } finally { rmSync(sab, { force: true }); }
}

server.close();
rmSync(T, { recursive: true, force: true });
console.log(off ? `SELFTEST: ${off} row(s) off` : "SELFTEST PASSED");
process.exit(off ? 1 : 0);
