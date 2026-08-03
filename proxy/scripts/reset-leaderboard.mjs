#!/usr/bin/env node
/**
 * reset-leaderboard.mjs -- wipe every leaderboard key so scoring restarts under v2.
 *
 * WHY A RESET IS PART OF THE CHANGE, not a cleanup after it. v1 scores were
 * earned passively: every aircraft that flew past an antenna counted. v2 scores
 * only what an owner tapped. The two numbers are not comparable, and leaving v1
 * totals on the board would put whoever had the busiest sky permanently on top
 * of a leaderboard that no longer claims to measure that. There is no migration
 * that could be correct -- nobody's past taps were recorded, because taps did not
 * used to mean anything.
 *
 * THE DEVICE SIDE NEEDS NOTHING. A v3 logbook record carries no claim field, so
 * it parses as claimDay 0 = unclaimed. Every device resets itself on the first
 * boot of the new firmware, keeping its full SEEN history intact. This script
 * only clears the cloud's copy.
 *
 * ORDER MATTERS. Run this AFTER the new Worker is deployed, not before:
 *
 *   1. Deploy the v2 Worker.        Old firmware still submits, and is stored
 *                                   as `legacy` -- accepted, never ranked.
 *   2. PROVE it is bound:
 *        curl -s https://scopes.valarsystems.com/leaderboard.json \
 *          | grep -q '"scoring":"claims-v2"'
 *      A machine-readable marker, deliberately NOT a sentence from the board
 *      page: that page is edited freely as a design artefact, and a deploy gate
 *      that breaks when somebody improves a subtitle is a gate that gets deleted.
 *      The marker is present even on an empty board, which is what step 2 sees.
 *   3. Run this script.             Clears v1 rows and First! flags.
 *   4. Devices update over OTA.     Each starts submitting claims and appears on
 *                                   the board as it is used.
 *
 * Running it before step 1 just means the surviving v1 Worker rebuilds a v1
 * board from the next submission, and you get to do it again.
 *
 * DISPLAY NAMES ARE KEPT BY DEFAULT. lb:name:* is what makes a display name
 * first-come-first-served. Clearing it is defensible for a "true" reset, but it
 * frees every name someone has already chosen -- so a name a customer is using
 * becomes claimable by a stranger, which is a worse outcome than a stale key and
 * is not undoable once taken. Scores are what became meaningless in v2; names did
 * not. Releasing them is therefore opt-in:
 *
 *   node scripts/reset-leaderboard.mjs --dry-run          # list what would go
 *   node scripts/reset-leaderboard.mjs --env production   # scores only, names kept
 *   node scripts/reset-leaderboard.mjs --release-names    # also free every name
 *
 * --keep-names is accepted and is a no-op, so a runbook that names it explicitly
 * still reads correctly.
 */
import { execFileSync } from "node:child_process";

const args = process.argv.slice(2);
const dryRun = args.includes("--dry-run");
const releaseNames = args.includes("--release-names");
const keepNames = !releaseNames;
const envIdx = args.indexOf("--env");
const cfEnv = envIdx >= 0 ? args[envIdx + 1] : "production";

const PREFIXES = ["lb:dev:", "lb:firsttype:", "lb:board"];
if (releaseNames) PREFIXES.push("lb:name:");

const wrangler = (...a) =>
  execFileSync("npx", ["wrangler", ...a], { encoding: "utf8", stdio: ["ignore", "pipe", "inherit"] });

// Resolve the binding to a namespace id via the config, so the id is never
// pasted into a destructive command by hand.
const listKeys = (prefix) => {
  const out = wrangler("kv", "key", "list", "--binding", "ENRICH_KV", "--env", cfEnv, "--prefix", prefix);
  try {
    return JSON.parse(out).map((k) => k.name);
  } catch {
    console.error("could not parse the key list; raw output follows:\n" + out.slice(0, 400));
    process.exit(1);
  }
};

let total = 0;
const doomed = [];
for (const prefix of PREFIXES) {
  const keys = listKeys(prefix);
  console.log(`${prefix.padEnd(18)} ${keys.length} key(s)`);
  doomed.push(...keys);
  total += keys.length;
}

if (total === 0) {
  console.log("\nnothing to delete -- the board is already clear.");
  process.exit(0);
}

if (dryRun) {
  console.log(`\n--dry-run: ${total} key(s) would be deleted. Sample:`);
  for (const k of doomed.slice(0, 20)) console.log("   " + k);
  if (doomed.length > 20) console.log(`   ... and ${doomed.length - 20} more`);
  process.exit(0);
}

// One bulk delete rather than N calls: a partial reset that stops halfway would
// leave a board scored from whichever rows happened to survive.
const payload = JSON.stringify(doomed);
console.log(`\ndeleting ${total} key(s) from env=${cfEnv} ...`);
execFileSync("npx", ["wrangler", "kv", "bulk", "delete", "--binding", "ENRICH_KV", "--env", cfEnv, "--force", "/dev/stdin"], {
  input: payload,
  encoding: "utf8",
  stdio: ["pipe", "inherit", "inherit"],
});
console.log(`done. ${keepNames ? "Display names were KEPT (pass --release-names to free them)." : "Display names were RELEASED and are claimable by anyone."}`);
