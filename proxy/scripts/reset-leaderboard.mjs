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
import { createRequire } from "node:module";
import { writeFileSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const args = process.argv.slice(2);
const dryRun = args.includes("--dry-run");
const releaseNames = args.includes("--release-names");
const keepNames = !releaseNames;
const envIdx = args.indexOf("--env");
const cfEnv = envIdx >= 0 ? args[envIdx + 1] : "production";

const PREFIXES = ["lb:dev:", "lb:firsttype:", "lb:board"];
if (releaseNames) PREFIXES.push("lb:name:");

// Run wrangler's JS entry point under THIS node, rather than shelling out to the
// `npx` wrapper. Two Windows failures made that necessary, both of which would
// have landed on deploy day at the exact moment before an irreversible delete:
//
//   execFileSync("npx", ...)      -> ENOENT. No shell, so the npx.cmd shim is
//                                    invisible; reads like wrangler is missing.
//   execFileSync("npx.cmd", ...)  -> EINVAL. Node 18.20+/20.12+/21.7+ refuse to
//                                    spawn .cmd/.bat without shell:true (the
//                                    BatBadBut hardening, CVE-2024-27980).
//
// The obvious escape is shell:true. Deliberately NOT taken: this script passes
// KV KEY NAMES into a bulk delete, and putting those through a shell parser to
// work around a path-resolution problem is the wrong trade on the one command in
// the product that cannot be undone. Resolving the module and running it with
// process.execPath removes the shell from the picture entirely, and is
// platform-independent rather than a Windows special case.
const require_ = createRequire(import.meta.url);
const WRANGLER_JS = require_.resolve("wrangler/bin/wrangler.js");
const wrangler = (...a) =>
  execFileSync(process.execPath, [WRANGLER_JS, ...a], {
    encoding: "utf8",
    stdio: ["ignore", "pipe", "inherit"],
  });

// --remote IS LOAD-BEARING. Wrangler v4 defaults every `kv` subcommand to LOCAL
// (miniflare) storage, so without it this listed an empty local namespace,
// printed "nothing to delete -- the board is already clear", and exited 0 -- on a
// production board holding 68 keys. That is the worst shape a bug in a
// destructive script can take: it does nothing, says so cheerfully, and the
// operator believes the reset happened. Real run, real numbers: lb:dev: 1,
// lb:firsttype: 66, lb:board 1.
//
// Resolve the binding via the config rather than passing a namespace id, so the
// id is never pasted into a destructive command by hand.
const listKeys = (prefix) => {
  const out = wrangler("kv", "key", "list", "--binding", "ENRICH_KV", "--env", cfEnv, "--remote", "--prefix", prefix);
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
console.log(`\ndeleting ${total} key(s) from env=${cfEnv} ...`);
// A REAL FILE, not /dev/stdin. That path does not exist on Windows, so the
// delete would have failed here -- after the listing had succeeded and printed a
// confident count, at the one step that cannot be retried by halves. Both this
// and the npx resolution above were latent until someone ran the script on the
// machine it will actually be run from.
const tmp = join(tmpdir(), `lb-reset-${process.pid}.json`);
writeFileSync(tmp, JSON.stringify(doomed), "utf8");
try {
  // --remote here too, and for the same reason: a delete that quietly targets
  // local storage would leave production untouched behind a "done." line.
  wrangler("kv", "bulk", "delete", "--binding", "ENRICH_KV", "--env", cfEnv, "--remote", "--force", tmp);
} finally {
  try { unlinkSync(tmp); } catch { /* best effort; it is a temp file */ }
}
console.log(`done. ${keepNames ? "Display names were KEPT (pass --release-names to free them)." : "Display names were RELEASED and are claimable by anyone."}`);
