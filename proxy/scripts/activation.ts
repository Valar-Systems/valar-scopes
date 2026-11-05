/**
 * activation.ts -- Daniel's one-liner for the activation allowlist (docs/device-activation.md).
 *
 *   npx tsx scripts/activation.ts add    <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts reset  <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts status <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts import <file>      [--staging]   MACs one per line, or esptool logs
 *   npx tsx scripts/activation.ts seed   <file>      [--staging] [--state listed|activated]   BEFORE #548 deploys
 *   npx tsx scripts/activation.ts seed   --ledger    [--staging] --state activated              grandfather enr:dev:
 *   npx tsx scripts/activation.ts seed   --token-ids test-device,records-device --state activated
 *   npx tsx scripts/activation.ts sweep  --ledger | --token-ids test-device,records-device    AFTER #548 deploys
 *
 * The id is the one the customer reads off the Connect screen or the config page.
 *
 * THE CREDENTIAL is the bench mint's PROVISION_TOKEN, read from the same file mint-test-identity.ts
 * and the bench read (~/.config/valar-flasher/provision-token, or provision-token-staging), so the
 * secret has one copy on disk. It travels in a request header, never on a command line.
 *
 * PRINTS NO ID: counts and states only, including for ids read from a file. A file line that is
 * not a MAC is named by its number. Exit 0 done, 1 refused or unreachable, 2 usage.
 *
 * SEED is the one operation that does not go through the route, because it runs BEFORE the route
 * exists in production (docs/device-activation.md, "Seeding before the deploy"). It writes `listed`
 * rows straight into KV with `wrangler kv bulk put`, under the caller's own `wrangler login` session,
 * from the proxy directory whatever the caller's cwd. It never rewrites an existing row, and it
 * verifies, repairs and verifies again (a bulk put drops keys silently). wrangler's own output is
 * captured, never printed: a key listing is a list of ids. Exit 0 complete, 1 incomplete.
 */
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { homedir, tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ACTIVATION_ID_RE, actKey, MAX_BATCH } from "../src/activation";
import { deviceIdFromMac } from "../src/provision";
import {
  chunk, errorCode, failureText, parseImport, parseRecords, seedWith, sumAdd, summarize, summarizeSeed, summarizeSweep,
  sweepCounts,
  type SeedState, type SeedStore,
} from "./activation-lib";
import { bulkGet, kvTargetFromWranglerToml } from "./kv-rest";
import { readTokenFile, type TokenPurpose } from "./token-file";

const BASES = { production: "https://scopes.valarsystems.com", staging: "https://scopes-staging.valarsystems.com" } as const;
const TOKEN_FILES = { production: "provision-token", staging: "provision-token-staging" } as const;

function usage(msg: string): never {
  console.error(`activation: ${msg}`);
  console.error("usage: activation.ts add|reset|status <16-hex id> [--staging]   |   activation.ts import|seed <file> [--staging]");
  process.exit(2);
}

function refuse(msg: string): never {
  console.error(`activation: ${msg}`);
  process.exit(1);
}

const args = process.argv.slice(2);
const env = args.includes("--staging") ? "staging" : "production";
// --state and --token-ids take a value; --ledger and --staging are flags. Everything else is positional.
const valueOf = (flag: string): string | undefined => {
  const i = args.indexOf(flag);
  return i >= 0 ? args[i + 1] : undefined;
};
const seedState = valueOf("--state") ?? "listed";
const tokenIds = valueOf("--token-ids");
const fromLedger = args.includes("--ledger");
const rest = args.filter((a, i) => !["--staging", "--ledger", "--state", "--token-ids"].includes(a)
  && !["--state", "--token-ids"].includes(args[i - 1] ?? ""));
const [op, target] = rest;
const seedFromStore = (op === "seed" || op === "sweep") && (fromLedger || tokenIds !== undefined);
if (seedFromStore ? rest.length !== 1 : rest.length !== 2 || !target) usage("expected an operation and one argument");
if (!op) usage("expected an operation");
if (seedState !== "listed" && seedState !== "activated") usage("--state must be listed or activated");
if (fromLedger && tokenIds !== undefined) usage("seed takes one source: a file, --ledger, or --token-ids");
if (op !== "add" && op !== "reset" && op !== "status" && op !== "import" && op !== "seed" && op !== "sweep") {
  usage("the operation must be add, reset, status, import, seed or sweep");
}
if (op === "sweep" && !seedFromStore) usage("sweep takes --ledger or --token-ids");

// wrangler resolves wrangler.toml from its cwd, so it runs from the proxy directory however this was started.
const PROXY_DIR = join(dirname(fileURLToPath(import.meta.url)), "..");

/**
 * One wrangler call, output captured. On failure: the exit code, then stderr AND stdout's non-key lines through
 * failureText (ids masked, the known config warning dropped), so a failure names its cause and never an id.
 */
function wrangler(argv: string[]): string {
  const r = spawnSync(["npx", "wrangler", ...argv].join(" "), { cwd: PROXY_DIR, shell: true, encoding: "utf8" });
  if (r.status !== 0) {
    refuse(`wrangler ${argv.slice(0, 3).join(" ")} failed (exit ${r.status}):\n` +
      failureText(String(r.stdout ?? ""), String(r.stderr ?? "")));
  }
  return String(r.stdout ?? "");
}

/** The live KV as a SeedStore. Every listing is --remote: without it wrangler reads a local store and answers [] (exit 0). */
function wranglerStore(envName: string): SeedStore {
  const common = ["--binding=ENRICH_KV", `--env=${envName}`, "--remote"];
  return {
    async listedIds() {
      const out = wrangler(["kv", "key", "list", "--prefix=act:", ...common]);
      let keys: unknown;
      try {
        keys = JSON.parse(out.slice(out.indexOf("[")));
      } catch {
        return refuse("the act: listing was not JSON; nothing is believed");
      }
      if (!Array.isArray(keys)) return refuse("the act: listing was not a list");
      const ids = new Set<string>();
      for (const k of keys) {
        const id = String((k as { name?: unknown }).name ?? "").slice("act:".length);
        if (ACTIVATION_ID_RE.test(id)) ids.add(id);
      }
      return ids;
    },
    async putRows(ids, state) {
      const dir = mkdtempSync(join(tmpdir(), "act-seed-"));
      try {
        const now = new Date().toISOString();
        // An activated row carries no gen: the generation is keyed by DEVICE_KEY_SECRET, which only the Worker holds.
        const row = state === "activated"
          ? { v: 1, state, listedAt: now, activatedAt: now, resets: 0 }
          : { v: 1, state, listedAt: now, resets: 0 };
        for (const [i, batch] of chunk([...ids], 1000).entries()) {
          const path = join(dir, `seed-${i}.json`);
          writeFileSync(path, JSON.stringify(batch.map((id) => ({ key: actKey(id), value: JSON.stringify(row) }))));
          wrangler(["kv", "bulk", "put", JSON.stringify(path), ...common]);
        }
      } finally {
        rmSync(dir, { recursive: true, force: true });
      }
    },
  };
}

function token(): string {
  const f = join(homedir(), ".config", "valar-flasher", TOKEN_FILES[env]);
  if (!existsSync(f)) refuse(`no provision token for ${env} (looked for ${TOKEN_FILES[env]} under ~/.config/valar-flasher)`);
  const t = readFileSync(f, "utf8").trim();
  if (!t) refuse(`the provision token file for ${env} is empty`);
  return t;
}

async function post(body: Record<string, unknown>, tok: string): Promise<Record<string, unknown>> {
  let res: Response;
  try {
    res = await fetch(`${BASES[env]}/blipscope/activation`, {
      method: "POST",
      headers: { "Content-Type": "application/json", "X-Provision-Token": tok },
      body: JSON.stringify(body),
    });
  } catch (e) {
    return refuse(`the ${env} route could not be reached (${(e as Error).message.slice(0, 80)})`);
  }
  let parsed: unknown = null;
  try {
    parsed = await res.json();
  } catch {
    // reported below by status alone
  }
  if (!res.ok) refuse(`the ${env} route answered HTTP ${res.status} ${errorCode(parsed)}`);
  if (typeof parsed !== "object" || parsed === null) refuse(`the ${env} route's answer is not JSON`);
  return parsed as Record<string, unknown>;
}

async function main(): Promise<void> {
  if (seedFromStore) {
    let ids: string[];
    let from: string;
    if (fromLedger) {
      // Every enrolment-ledger id (enr:dev:), read with the same --remote listing; never printed.
      const out = wrangler(["kv", "key", "list", "--prefix=enr:dev:", "--binding=ENRICH_KV", `--env=${env}`, "--remote"]);
      let keys: unknown;
      try {
        keys = JSON.parse(out.slice(out.indexOf("[")));
      } catch {
        return refuse("the enr:dev: listing was not JSON; nothing was written");
      }
      if (!Array.isArray(keys)) return refuse("the enr:dev: listing was not a list; nothing was written");
      ids = keys.map((k) => String((k as { name?: unknown }).name ?? "").slice("enr:dev:".length))
        .filter((id) => ACTIVATION_ID_RE.test(id));
      from = "the enrolment ledger";
    } else {
      const purposes = (tokenIds ?? "").split(",").map((x) => x.trim()).filter(Boolean);
      const allowed = new Set(["test-device", "records-device"]);
      if (purposes.length === 0 || purposes.some((x) => !allowed.has(x))) usage("--token-ids takes test-device and/or records-device");
      ids = [];
      for (const purpose of purposes) {
        let id: string;
        try {
          id = readTokenFile(purpose as TokenPurpose).trim().toLowerCase();
        } catch {
          return refuse(`the token file has no usable ${purpose} line; nothing was written`);
        }
        if (!ACTIVATION_ID_RE.test(id)) return refuse(`the ${purpose} line is not a 16-hex id; nothing was written`);
        ids.push(id);
      }
      ids = [...new Set(ids)];
      from = `the token file (${purposes.join(", ")})`;
    }
    if (ids.length === 0) refuse(`no id came from ${from}; nothing was written`);
    if (op === "sweep") {
      // THE SWEEP (after #548 deploys): one `status` per id THROUGH THE WORKER, which writes the current generation
      // onto a grandfathered row; then the rows are read back with the kv-read token and the gen counted from what KV
      // holds, never inferred from the answers. Counts only.
      console.log(`sweep (${env}): ${ids.length} unique id(s) from ${from}`);
      const tok = token();
      const states: string[] = [];
      for (const id of ids) {
        const answer = await post({ op: "status", ids: [id] }, tok);
        states.push(typeof answer.state === "string" ? answer.state : "unknown");
      }
      const t = kvTargetFromWranglerToml(join(PROXY_DIR, "wrangler.toml"), env, readTokenFile("kv-read"));
      const rows = await bulkGet(t, ids.map((id) => actKey(id)));
      const gens = ids.map((id) => {
        try {
          return (JSON.parse(rows[actKey(id)] ?? "null") as { gen?: unknown } | null)?.gen;
        } catch {
          return undefined;
        }
      });
      const c = sweepCounts(states, gens);
      console.log(summarizeSweep(env, c));
      process.exit(c.stamped === c.swept ? 0 : 1);
    }
    console.log(`seed (${env}): ${ids.length} unique id(s) from ${from}, as ${seedState}`);
    const result = await seedWith(wranglerStore(env), ids, seedState as SeedState);
    console.log(summarizeSeed(env, result));
    process.exit(result.complete ? 0 : 1);
  }
  if (op === "seed") {
    if (!existsSync(target!)) usage("the records file does not exist");
    const parsed = parseRecords(readFileSync(target!, "utf8"));
    if (parsed.badLines.length > 0) {
      refuse(`${parsed.badLines.length} line(s) are not a record (line ${parsed.badLines.slice(0, 20).join(", ")}); nothing was written`);
    }
    const ids = parsed.mode === "macs" ? [...new Set(await Promise.all(parsed.macs.map(deviceIdFromMac)))] : parsed.ids;
    if (ids.length === 0) refuse("the file holds no id or MAC; nothing was written");
    console.log(`seed (${env}): ${ids.length} unique id(s) from a ${parsed.mode} file, as ${seedState}`);
    const result = await seedWith(wranglerStore(env), ids, seedState as SeedState);
    console.log(summarizeSeed(env, result));
    process.exit(result.complete ? 0 : 1);
  }
  if (op === "import") {
    if (!existsSync(target!)) usage("the import file does not exist");
    const parsed = parseImport(readFileSync(target!, "utf8"));
    if (parsed.badLines.length > 0) {
      const shown = parsed.badLines.slice(0, 20).join(", ");
      refuse(`${parsed.badLines.length} line(s) are not a MAC (line ${shown}${parsed.badLines.length > 20 ? ", ..." : ""}); nothing was sent`);
    }
    if (parsed.macs.length === 0) refuse("the file holds no MAC; nothing was sent");
    console.log(`import (${env}): ${parsed.macs.length} unique MAC(s) read from a ${parsed.mode}`);
    const tok = token();
    const answers: Record<string, unknown>[] = [];
    for (const batch of chunk(parsed.macs, MAX_BATCH)) answers.push(await post({ op: "add", macs: batch }, tok));
    const line = summarize("add", sumAdd(answers));
    if (!line) refuse("an answer was not the shape expected");
    console.log(line);
    return;
  }
  const id = target!.trim().toLowerCase();
  if (!ACTIVATION_ID_RE.test(id)) usage("the id must be exactly 16 hex characters");
  const answer = await post({ op, ids: [id] }, token());
  const line = summarize(op as "add" | "reset" | "status", answer);
  if (!line) refuse("the answer was not the shape expected");
  console.log(`${env}: ${line}`);
}

void main();
