/**
 * mint-test-identity.ts (docs/test-identity.md, items 1, 2, 5 and 8): mints the TEST identity the production checks
 * run as, through the bench provision route (POST /blipscope/provision), and writes it STRAIGHT into the token file.
 * Prints booleans, labels and lengths only: never the MAC, the id or a key.
 *
 *   npx tsx scripts/mint-test-identity.ts --env production      writes test-mac, test-device, test-key
 *   npx tsx scripts/mint-test-identity.ts --env staging         writes test-key-staging (the same id, from test-mac)
 *   npx tsx scripts/mint-test-identity.ts --add-records-device  writes records-device from blip-device, in place
 *
 *   --base <url> and --provision-token-file <path> point it at a stub (mint-test-identity.selftest.mts).
 *
 * The id: a fresh random MAC, locally administered and unicast, which no board has (every board's MAC is universally
 * administered), derived by the Worker's OWN deviceIdFromMac and checked against the fake-id list before any request.
 * The route derives it again; the two must agree. The provision token is sent as a fetch header, never on a command
 * line. Nothing it writes is overwritten: a field already in the file refuses the run.
 *
 * Mints only. An authenticated request as the new id writes it into the dashboard's request points, so the checks
 * that make one run after the dashboard's exclusion is deployed (docs/test-identity.md, TD1, then TM2's /v1/config).
 */
import { randomBytes } from "node:crypto";
import { appendFileSync, existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { FAKE_DEVICE_IDS } from "../src/fakeids.generated";
import { deviceIdFromMac } from "../src/provision";
import { TokenFileError, nodeIo, parseTokenFile, readTokenFileText, tokenFileKeys } from "./token-file";

const BASES = { production: "https://scopes.valarsystems.com", staging: "https://scopes-staging.valarsystems.com" } as const;
const TOKEN_FILES = { production: "provision-token", staging: "provision-token-staging" } as const;

const argv = process.argv.slice(2);
const opt = (n: string) => { const i = argv.indexOf(n); return i >= 0 ? argv[i + 1] : undefined; };
const say = (s: string) => console.log(`mint-test-identity: ${s}`);
const refuse = (s: string): never => { console.log(`mint-test-identity: REFUSED -- ${s}`); process.exit(2); };

/** A fresh random MAC: locally administered (bit 1 of the first octet set), unicast (bit 0 clear). */
function freshMac(): string {
  const b = randomBytes(6);
  b[0] = (b[0]! & 0xfc) | 0x02;
  return [...b].map((x) => x.toString(16).padStart(2, "0")).join(":");
}

/** One POST to the provision route. The token travels in a header of this process, never in an argument. */
async function provision(base: string, token: string, mac: string): Promise<{ deviceId: string; key: string }> {
  let res: Response;
  try {
    res = await fetch(`${base.replace(/\/+$/, "")}/blipscope/provision`, {
      method: "POST", headers: { "content-type": "application/json", "X-Provision-Token": token }, body: JSON.stringify({ mac }),
    });
  } catch (e) { return refuse(`the provision route could not be reached (${(e as Error).message.slice(0, 80)})`); }
  const text = await res.text();
  if (res.status !== 200) {
    const code = /"error":"([a-z_]+)"/.exec(text)?.[1] ?? "unknown"; // a code, never a value
    return refuse(`the provision route answered HTTP ${res.status} ${code}`);
  }
  const body = JSON.parse(text) as { deviceId?: unknown; key?: unknown };
  if (typeof body.deviceId !== "string" || typeof body.key !== "string") return refuse("the provision route's answer has no deviceId or key");
  return { deviceId: body.deviceId, key: body.key };
}

function providerToken(env: "production" | "staging"): string {
  const f = opt("--provision-token-file") ?? join(homedir(), ".config", "valar-flasher", TOKEN_FILES[env]);
  if (!existsSync(f)) return refuse(`no provision token for ${env} at ${f}`);
  const t = readFileSync(f, "utf8").trim();
  if (!t) return refuse(`the provision token file for ${env} is empty`);
  return t;
}

/** Appends lines to the token file in ONE write, starting on a fresh line. */
function append(path: string, text: string, lines: Array<[string, string]>) {
  const lead = text.length && !text.endsWith("\n") ? "\n" : "";
  appendFileSync(path, lead + lines.map(([k, v]) => `${k}=${v}\n`).join(""));
}

async function main() {
  let file: { text: string; path: string };
  try { file = readTokenFileText("place to write the test identity", nodeIo()); } catch (e) {
    return refuse(e instanceof TokenFileError ? e.message : String(e));
  }
  const keys = tokenFileKeys(file.text);
  const field = (k: string) => parseTokenFile(file.text, k as Parameters<typeof parseTokenFile>[1], file.path);

  if (argv.includes("--add-records-device")) {
    if (keys.has("records-device")) return refuse("records-device is already in the token file");
    if (!keys.has("blip-device")) return refuse("there is no blip-device line to take records-device from");
    append(file.path, file.text, [["records-device", field("blip-device")]]);
    return say("wrote records-device, from blip-device (value not shown)");
  }

  const env = opt("--env");
  if (env !== "production" && env !== "staging") return refuse("usage: --env production|staging, or --add-records-device");
  const base = opt("--base") ?? BASES[env];

  if (env === "production") {
    const clash = ["test-mac", "test-device", "test-key"].filter((k) => keys.has(k));
    if (clash.length) return refuse(`already in the token file: ${clash.join(", ")}`);
    const mac = freshMac();
    const id = await deviceIdFromMac(mac);
    if (FAKE_DEVICE_IDS.has(id)) return refuse("the fresh MAC derives to an id on the fake-id list; run again");
    const got = await provision(base, providerToken(env), mac);
    const idMatches = got.deviceId === id, keyShape = /^[0-9a-f]{64}$/.test(got.key);
    say(`production: the route's id equals the Worker's own derivation: ${idMatches ? "yes" : "no"}; key is 64 hex: ${keyShape ? "yes" : "no"}`);
    if (!idMatches || !keyShape) return refuse("nothing written");
    append(file.path, file.text, [["test-mac", mac], ["test-device", id], ["test-key", got.key]]);
    return say("wrote test-mac, test-device and test-key (values not shown)");
  }

  // staging: the same MAC, so the same id, with staging's own key
  if (keys.has("test-key-staging")) return refuse("test-key-staging is already in the token file");
  if (!keys.has("test-mac") || !keys.has("test-device")) return refuse("mint production first: no test-mac or test-device");
  const mac = field("test-mac"), id = field("test-device");
  if ((await deviceIdFromMac(mac)) !== id) return refuse("test-mac does not derive to test-device");
  const got = await provision(base, providerToken(env), mac);
  const idMatches = got.deviceId === id, keyShape = /^[0-9a-f]{64}$/.test(got.key);
  const differs = !keys.has("test-key") || field("test-key") !== got.key;
  say(`staging: the route's id equals test-device: ${idMatches ? "yes" : "no"}; key is 64 hex: ${keyShape ? "yes" : "no"}; differs from test-key: ${differs ? "yes" : "no"}`);
  if (!idMatches || !keyShape || !differs) return refuse("nothing written");
  append(file.path, file.text, [["test-key-staging", got.key]]);
  return say("wrote test-key-staging (value not shown)");
}

await main();
