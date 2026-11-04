// docs/test-identity.md, item 3: runs a bash script ONCE as the TEST identity. The single refusal point for
// smoke-prod.sh and watch-upstream.sh (test-identity.sh calls this): resolveTestIdentity refuses BLIP_KEY/BLIP_DEVICE
// in the environment, a test-device equal to records-device, and a missing or misshapen test field, all with exit 2
// and before the script makes any request. The pair travels only in the child's environment (TEST_IDENTITY_KEY,
// TEST_IDENTITY_DEVICE); BLIP_KEY and BLIP_DEVICE are removed from it. Prints NAMES only, never a value.
//
//   node scripts/with-test-identity.mts <script.sh> [args...]
//
// The target is staging when BASE names scopes-staging (test-key-staging), else production (test-key).
import { spawnSync } from "node:child_process";
import { TokenFileError, resolveTestIdentity } from "./token-file.ts";

const [script, ...args] = process.argv.slice(2);
if (!script) { console.error("with-test-identity: usage: <script.sh> [args...]"); process.exit(2); }
if (process.env.TEST_IDENTITY_REEXEC) { console.error("with-test-identity: refusing a second re-run"); process.exit(2); }
const target = /scopes-staging\./.test(process.env.BASE ?? "") ? "staging" : "production";
let c: { key: string; device: string; source: string };
try {
  c = resolveTestIdentity(target);
} catch (e) {
  if (!(e instanceof TokenFileError)) throw e;
  console.error(`REFUSED (no request sent): ${e.message}`); // names fields, a path and a reason, never a value
  process.exit(2);
}
const env: Record<string, string | undefined> = { ...process.env, TEST_IDENTITY_REEXEC: "1", TEST_IDENTITY_KEY: c.key, TEST_IDENTITY_DEVICE: c.device };
delete env.BLIP_KEY;
delete env.BLIP_DEVICE;
console.error(`with-test-identity: the test identity for ${target}, from ${c.source}`);
const r = spawnSync("bash", [script, ...args], { env, stdio: "inherit" });
process.exit(r.status ?? 1);
