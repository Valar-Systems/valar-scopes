/**
 * activation.ts -- Daniel's one-liner for the activation allowlist (docs/device-activation.md).
 *
 *   npx tsx scripts/activation.ts add    <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts reset  <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts status <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts import <file>      [--staging]   MACs one per line, or esptool logs
 *   npx tsx scripts/activation.ts seed   <file>      [--staging]   BEFORE #548 deploys: bench records into KV
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
  chunk, errorCode, parseImport, parseRecords, seedWith, sumAdd, summarize, summarizeSeed, type SeedStore,
} from "./activation-lib";

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
const rest = args.filter((a) => a !== "--staging");
const [op, target] = rest;
if (rest.length !== 2 || !op || !target) usage("expected an operation and one argument");
if (op !== "add" && op !== "reset" && op !== "status" && op !== "import" && op !== "seed") {
  usage("the operation must be add, reset, status, import or seed");
}

// wrangler resolves wrangler.toml from its cwd, so it runs from the proxy directory however this was started.
const PROXY_DIR = join(dirname(fileURLToPath(import.meta.url)), "..");

/** One wrangler call, output captured. On failure: the exit code and stderr with every 16-hex run masked. */
function wrangler(argv: string[]): string {
  const r = spawnSync(["npx", "wrangler", ...argv].join(" "), { cwd: PROXY_DIR, shell: true, encoding: "utf8" });
  if (r.status !== 0) {
    const err = String(r.stderr ?? "").replace(/[0-9a-f]{16}/gi, "<id>").slice(-600);
    refuse(`wrangler ${argv.slice(0, 3).join(" ")} failed (exit ${r.status}):\n${err}`);
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
    async putListed(ids) {
      const dir = mkdtempSync(join(tmpdir(), "act-seed-"));
      try {
        const listedAt = new Date().toISOString();
        for (const [i, batch] of chunk([...ids], 1000).entries()) {
          const path = join(dir, `seed-${i}.json`);
          writeFileSync(path, JSON.stringify(batch.map((id) => ({
            key: actKey(id), value: JSON.stringify({ v: 1, state: "listed", listedAt, resets: 0 }),
          }))));
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
  if (op === "seed") {
    if (!existsSync(target!)) usage("the records file does not exist");
    const parsed = parseRecords(readFileSync(target!, "utf8"));
    if (parsed.badLines.length > 0) {
      refuse(`${parsed.badLines.length} line(s) are not a record (line ${parsed.badLines.slice(0, 20).join(", ")}); nothing was written`);
    }
    const ids = parsed.mode === "macs" ? [...new Set(await Promise.all(parsed.macs.map(deviceIdFromMac)))] : parsed.ids;
    if (ids.length === 0) refuse("the file holds no id or MAC; nothing was written");
    console.log(`seed (${env}): ${ids.length} unique id(s) from a ${parsed.mode} file`);
    const result = await seedWith(wranglerStore(env), ids);
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
