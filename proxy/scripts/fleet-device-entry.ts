/**
 * fleet-device-entry.ts -- write ONE device's entry into cfg:fleet's `devices` layer (docs/bend-site-feed.md,
 * "The feeder's night cadence"; ruled 2026-11-05).
 *
 *   npx tsx scripts/fleet-device-entry.ts --field pollNightMs --value 15000 [--staging] [--dry-run]
 *
 * THE DEVICE is the token file's `feeder-device` line (BLIPSCOPE_TOKEN_FILE; token-file.ts). It is never printed,
 * never on a command line, and never in the repo; it goes into KV only.
 *
 * THE VALUE is refused unless it is a whole number from 1000 to 4294967295 -- the range the firmware applies
 * unchanged (fleet-device-entry-lib.ts) -- before anything is read or written.
 *
 * THE WRITE goes through wrangler under the caller's `wrangler login` session, from the proxy directory whatever the
 * cwd: cfg:fleet is read (--remote: without it wrangler reads a local store and answers nothing, exit 0), merged,
 * written from a temporary file (--path, deleted after), and read back. The read-back is ONE boolean: KV now holds
 * exactly the merge of what it held. Exit 0 verified, 1 not verified or wrangler failed, 2 usage or refused value.
 */
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { checkEntryValue, mergeDeviceEntry, readBackMatches } from "./fleet-device-entry-lib";
import { readTokenFile } from "./token-file";

const PROXY_DIR = join(dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const opt = (name: string): string | undefined => {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
};
const env = argv.includes("--staging") ? "staging" : "production";
const dryRun = argv.includes("--dry-run");

function stop(code: number, msg: string): never {
  console.error(`fleet-device-entry: ${msg}`);
  process.exit(code);
}

const check = checkEntryValue(opt("--field") ?? "", opt("--value") ?? "");
if (!check.ok) stop(2, `refused: ${check.reason}; nothing was read or written`);

const ID16 = /^[0-9a-f]{16}$/;
let id: string;
try {
  id = readTokenFile("feeder-device").trim().toLowerCase();
} catch {
  stop(2, "the token file has no usable feeder-device line");
}
if (!ID16.test(id!)) stop(2, "the feeder-device line is not a 16-hex device id");

function wrangler(args: string[]): string {
  const r = spawnSync(["npx", "wrangler", ...args].join(" "), { cwd: PROXY_DIR, shell: true, encoding: "utf8" });
  if (r.status !== 0) {
    stop(1, `wrangler ${args.slice(0, 3).join(" ")} failed (exit ${r.status}):\n` +
      String(r.stderr ?? "").replace(/[0-9a-f]{16}/gi, "<id>").slice(-600));
  }
  return String(r.stdout ?? "");
}

const common = ["--binding=ENRICH_KV", `--env=${env}`, "--remote"];
function readFleet(): unknown {
  const out = wrangler(["kv", "key", "get", "cfg:fleet", ...common, "--text"]).trim();
  if (out === "" || out === "Value not found") return {};
  try {
    return JSON.parse(out);
  } catch {
    return stop(1, "cfg:fleet is not JSON; nothing was written");
  }
}

const before = readFleet();
const merged = mergeDeviceEntry(before, id!, check.field, check.value);
const devicesBefore = Object.keys(((before as { devices?: object })?.devices ?? {}) as object).length;
const devicesAfter = Object.keys(merged.devices as object).length;
console.log(`cfg:fleet (${env}): devices entries ${devicesBefore} -> ${devicesAfter}; setting ${check.field} = ${check.value} on the feeder`);
if (dryRun) {
  console.log("dry run: nothing written");
  process.exit(0);
}

const dir = mkdtempSync(join(tmpdir(), "fleet-entry-"));
try {
  const path = join(dir, "cfg-fleet.json");
  writeFileSync(path, JSON.stringify(merged));
  wrangler(["kv", "key", "put", "cfg:fleet", `--path=${JSON.stringify(path)}`, ...common]);
} finally {
  rmSync(dir, { recursive: true, force: true });
}
const verified = readBackMatches(before, readFleet(), id!, check.field, check.value);
console.log(`read-back verified: ${verified}`);
process.exit(verified ? 0 : 1);
