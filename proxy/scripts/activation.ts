/**
 * activation.ts -- Daniel's one-liner for the activation allowlist (docs/device-activation.md).
 *
 *   npx tsx scripts/activation.ts add    <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts reset  <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts status <16-hex id> [--staging]
 *   npx tsx scripts/activation.ts import <file>      [--staging]   MACs one per line, or esptool logs
 *
 * The id is the one the customer reads off the Connect screen or the config page.
 *
 * THE CREDENTIAL is the bench mint's PROVISION_TOKEN, read from the same file mint-test-identity.ts
 * and the bench read (~/.config/valar-flasher/provision-token, or provision-token-staging), so the
 * secret has one copy on disk. It travels in a request header, never on a command line.
 *
 * PRINTS NO ID: counts and states only, including for ids read from a file. A file line that is
 * not a MAC is named by its number. Exit 0 done, 1 refused or unreachable, 2 usage.
 */
import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { ACTIVATION_ID_RE, MAX_BATCH } from "../src/activation";
import { chunk, errorCode, parseImport, sumAdd, summarize } from "./activation-lib";

const BASES = { production: "https://scopes.valarsystems.com", staging: "https://scopes-staging.valarsystems.com" } as const;
const TOKEN_FILES = { production: "provision-token", staging: "provision-token-staging" } as const;

function usage(msg: string): never {
  console.error(`activation: ${msg}`);
  console.error("usage: activation.ts add|reset|status <16-hex id> [--staging]   |   activation.ts import <file> [--staging]");
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
if (op !== "add" && op !== "reset" && op !== "status" && op !== "import") usage("the operation must be add, reset, status or import");

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
