// The checks' selftest (docs/test-identity.md: TI1, TI1b, S-TIGUARD, TI2, TI7 and its control). Runs the REAL
// smoke-prod.sh and watch-upstream.sh against a LOCAL STUB, with scratch token files outside any git tree and EXAMPLE
// values only. Prints booleans, counts and labels, never a value. Exit 0 every row as predicted, 1 any row off.
//
//   npx tsx scripts/test-identity.selftest.mts
//
// TI1/TI1b: a `curl` stand-in first on PATH counts every request the script would send; a refusal must leave it at 0.
// S-TIGUARD: token-file.ts's BLIP_* refusal removed (restored from a byte copy, sha256 checked); TI1 must then fail.
// TI2: the stub records which device and key each request carried. TI7: the stub answers slowly so each curl lives
// long enough to be seen, and a sampler reads EVERY process's command line, comparing in memory. Its control: a curl
// with the key on its command line, the old way, must read true; and a sampler that never saw a curl at all is BLIND.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { chmodSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { delimiter, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url)), PROXY = join(HERE, "..");
// EXAMPLE values, built at run time so no 16-hex literal sits in the repo
const TDEV = "ab".repeat(8), TKEY = "c".repeat(64), RDEV = "0000000000000000"; // records-device: the allowlisted EXAMPLE id
const BKEY = "e".repeat(64), BDEV = "cd".repeat(8); // an EXAMPLE stand-in for a board's pair
let delayMs = 0;
const seen = { requests: 0, devices: new Set<string>(), keys: new Set<string>() };
const server = createServer((req, res) => {
  seen.requests++;
  const d = req.headers["x-blip-device"], k = req.headers["x-blip-key"];
  if (typeof d === "string") seen.devices.add(d);
  if (typeof k === "string") seen.keys.add(k);
  setTimeout(() => { res.writeHead(200, { "content-type": "application/json" }); res.end("{}"); }, delayMs);
});
await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
const BASE = `http://127.0.0.1:${(server.address() as { port: number }).port}`;

const T = mkdtempSync(join(tmpdir(), "tid-selftest-"));
const tokens = (name: string, device = TDEV) => {
  const f = join(T, name);
  writeFileSync(f, `# EXAMPLE token file\nkv-read=EXAMPLE-not-a-token\nblip-key=${BKEY}\nblip-device=${BDEV}\ntest-device=${device}\ntest-key=${TKEY}\ntest-key-staging=${"d".repeat(64)}\nrecords-device=${RDEV}\n`);
  chmodSync(f, 0o600); // token-file.ts refuses an others-readable file on POSIX (CI on 3f9e655)
  return f;
};
// the curl stand-in: counts each call and answers nothing
const SHIM = join(T, "shim"); const COUNT = join(T, "curl-count");
writeFileSync(COUNT, "");
await import("node:fs").then((fs) => fs.mkdirSync(SHIM));
writeFileSync(join(SHIM, "curl"), '#!/usr/bin/env bash\necho x >> "$SHIM_COUNT"\nexit 0\n');
chmodSync(join(SHIM, "curl"), 0o755);
const shimCount = () => readFileSync(COUNT, "utf8").split("\n").filter(Boolean).length;

const run = (script: string, env: Record<string, string | undefined>, timeoutMs = 240_000) => new Promise<{ code: number | null; out: string }>((done) => {
  const base: Record<string, string | undefined> = { ...process.env, BASE, HOURS: "0.0005", INTERVAL: "1", OUT: join(T, "watch.log") };
  delete base.BLIP_KEY; delete base.BLIP_DEVICE;
  const c = spawn("bash", [script], { cwd: PROXY, env: { ...base, ...env } });
  let out = "";
  c.stdout.on("data", (x) => { out += x; }); c.stderr.on("data", (x) => { out += x; });
  const t = setTimeout(() => c.kill(), timeoutMs);
  c.on("close", (code) => { clearTimeout(t); done({ code, out }); });
});
let off = 0;
const row = (name: string, ok: boolean, detail: string) => { if (!ok) off++; console.log(`${name}: ${ok ? "as predicted" : "OFF-PREDICTION"} -- ${detail}`); };
const withShim = { PATH: `${SHIM}${delimiter}${process.env.PATH}`, SHIM_COUNT: COUNT };

// ---- TI1: a board's pair in BLIP_KEY/BLIP_DEVICE is refused, with 0 requests --------------------------------------
for (const s of ["scripts/smoke-prod.sh", "scripts/watch-upstream.sh"]) {
  writeFileSync(COUNT, "");
  const r = await run(s, { ...withShim, BLIPSCOPE_TOKEN_FILE: tokens("t1"), BLIP_KEY: BKEY, BLIP_DEVICE: BDEV });
  // by its REASON, not any refusal: a refusal for another cause (a file mode, say) would otherwise pass here
  const byReason = /BLIP_(KEY|DEVICE) is set/.test(r.out);
  row(`TI1 ${s.split("/")[1]}`, r.code === 2 && shimCount() === 0 && byReason, `exit ${r.code}; refused because BLIP_* is set: ${byReason}; requests counted ${shimCount()}`);
}
// ---- TI1b: test-device equal to records-device is refused, with 0 requests -----------------------------------------
writeFileSync(COUNT, "");
const r1b = await run("scripts/smoke-prod.sh", { ...withShim, BLIPSCOPE_TOKEN_FILE: tokens("t1b", RDEV) });
row("TI1b", r1b.code === 2 && shimCount() === 0 && /equals records-device/.test(r1b.out), `exit ${r1b.code}; requests counted ${shimCount()}`);

// ---- S-TIGUARD: the BLIP_* refusal removed; TI1 must fail (requests sent) -------------------------------------------
const TF = join(HERE, "token-file.ts"), orig = readFileSync(TF), before = createHash("sha256").update(orig).digest("hex");
const anchor = 'for (const v of ["BLIP_KEY", "BLIP_DEVICE"]) {';
if (orig.toString("utf8").split(anchor).length !== 2) row("S-TIGUARD", false, "anchor not found exactly once (BLIND)");
else {
  try {
    writeFileSync(TF, orig.toString("utf8").replace(anchor, "for (const v of [] as string[]) { // SABOTAGE S-TIGUARD"));
    writeFileSync(COUNT, "");
    const r = await run("scripts/watch-upstream.sh", { ...withShim, BLIPSCOPE_TOKEN_FILE: tokens("tsab"), BLIP_KEY: BKEY, BLIP_DEVICE: BDEV });
    row("S-TIGUARD", shimCount() > 0, `the refusal removed: requests counted ${shimCount()} (exit ${r.code}), so TI1 would fail`);
  } finally {
    writeFileSync(TF, orig);
    if (createHash("sha256").update(readFileSync(TF)).digest("hex") !== before) { console.log("S-TIGUARD: RESTORE FAILED"); process.exit(3); }
  }
}

// ---- TI2 + TI7: a real smoke run against the stub, slow answers, every command line sampled ------------------------
async function commandLines(): Promise<string[]> {
  if (process.platform === "win32") {
    return new Promise((done) => {
      const p = spawn("powershell", ["-NoProfile", "-NonInteractive", "-Command", "Get-CimInstance Win32_Process | ForEach-Object { $_.CommandLine }"]);
      let o = ""; p.stdout.on("data", (x) => { o += x; }); p.on("close", () => done(o.split(/\r?\n/)));
    });
  }
  const out: string[] = [];
  for (const pid of readdirSync("/proc").filter((d) => /^\d+$/.test(d))) {
    try { out.push(readFileSync(`/proc/${pid}/cmdline`, "utf8").replace(/\0/g, " ")); } catch { /* gone */ }
  }
  return out;
}
async function sampleWhile(p: Promise<unknown>, needles: string[]) {
  let done = false, samples = 0, curlSeen = 0, hit = false;
  p.finally(() => { done = true; });
  while (!done) {
    const lines = await commandLines(); samples++;
    if (lines.some((l) => /curl/i.test(l) && l.includes(BASE.replace("http://", "")))) curlSeen++;
    if (lines.some((l) => needles.some((n) => l.includes(n)))) hit = true;
    // A macrotask yield, on both platforms. On Linux commandLines() is all-synchronous, so without it this loop only
    // awaits resolved promises and the event loop never runs: the child's 'close' (which sets done), the stub's replies
    // and the kill timers all starve. That hung CI on b7f9f7a for 40+ minutes (docs/test-identity.md). S-TISTARVE
    // removes this line and must hang (sabotage-tistarve.mjs, a bounded wait).
    await new Promise((r) => setTimeout(r, 20));
  }
  return { samples, curlSeen, hit };
}
seen.requests = 0; seen.devices.clear(); seen.keys.clear(); delayMs = 2500;
const smoke = run("scripts/smoke-prod.sh", { BLIPSCOPE_TOKEN_FILE: tokens("t2") }, 420_000);
const s7 = await sampleWhile(smoke, [TKEY, TDEV]);
const r2 = await smoke;
row("TI2", seen.requests > 0 && [...seen.devices].every((d) => d === TDEV) && [...seen.keys].every((k) => k === TKEY) && seen.devices.size === 1,
  `requests ${seen.requests}; every X-Blip-Device is test-device: ${[...seen.devices].every((d) => d === TDEV)}; every X-Blip-Key is test-key: ${[...seen.keys].every((k) => k === TKEY)}; blip-key/blip-device ever sent: ${seen.keys.has(BKEY) || seen.devices.has(BDEV)} (smoke exit ${r2.code})`);
row("TI7", s7.curlSeen > 0 && !s7.hit, s7.curlSeen === 0 ? `BLIND: ${s7.samples} samples never saw a curl` : `the key or id on any process's command line: ${s7.hit} (${s7.samples} samples, a curl seen in ${s7.curlSeen})`);

// ---- TI7's control: the old way, the key as a curl argument -------------------------------------------------------
writeFileSync(join(T, "old.sh"), 'for i in 1 2 3; do curl -s -H "X-Blip-Key: $K" "$URL" > /dev/null; done\n');
const old = run(join(T, "old.sh"), { K: TKEY, URL: `${BASE}/v1/blips` });
const c7 = await sampleWhile(old, [TKEY]);
row("TI7 control", c7.hit, `the old way, the key on curl's command line: ${c7.hit} (${c7.samples} samples, a curl seen in ${c7.curlSeen})`);

server.close();
rmSync(T, { recursive: true, force: true });
console.log(off ? `SELFTEST: ${off} row(s) off` : "SELFTEST PASSED");
process.exit(off ? 1 : 0);
