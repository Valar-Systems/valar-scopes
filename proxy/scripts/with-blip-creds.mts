// docs/blip-creds-token-file.md: runs a bash script ONCE with BLIP_KEY and BLIP_DEVICE in its environment, each from
// its variable while set, else from the token file through token-file.ts (resolveBlipCreds: the one reader, with its
// refusals). Nothing is printed but NAMES and the child's own output; the values travel only in the child's
// environment. BLIPSCOPE_CREDS_REEXEC marks the child, so a second re-run is refused.
//
//   node scripts/with-blip-creds.mts <script.sh> [args...]   (blip-creds.sh calls this)
//   node scripts/with-blip-creds.mts --selftest               T4, T5, T6 with a fake token file (EXAMPLE values)
import { spawnSync } from "node:child_process";
import { chmodSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { TOKEN_FILE_ENV, TokenFileError, resolveBlipCreds } from "./token-file.ts";

function run(script: string, args: string[]): number {
  if (process.env.BLIPSCOPE_CREDS_REEXEC) {
    console.error("with-blip-creds: refusing a second re-run (BLIPSCOPE_CREDS_REEXEC is set)");
    return 2;
  }
  const env: Record<string, string | undefined> = { ...process.env, BLIPSCOPE_CREDS_REEXEC: "1" };
  try {
    const c = resolveBlipCreds();
    env.BLIP_KEY = c.key;
    env.BLIP_DEVICE = c.device;
    console.error(`with-blip-creds: BLIP_KEY/BLIP_DEVICE from ${c.source}`);
  } catch (e) {
    if (!(e instanceof TokenFileError)) throw e;
    console.error(`with-blip-creds: ${e.message}`);   // names a path, a key and a line, never a value
  }
  const r = spawnSync("bash", [script, ...args], { env, stdio: "inherit" });
  return r.status ?? 1;
}

function selftest(): number {
  const here = dirname(fileURLToPath(import.meta.url));
  const helper = join(here, "blip-creds.sh").replace(/\\/g, "/");
  const td = mkdtempSync(join(tmpdir(), "with-blip-creds-"));
  const FAKE_KEY = "EXAMPLE-key-not-a-real-one-0001";   // EXAMPLE -- not a real key
  const FAKE_DEV = "0000000000000000";                  // EXAMPLE -- not a real device
  const good = join(td, "tokens");
  writeFileSync(good, `# EXAMPLE values only\nblip-key=${FAKE_KEY}\nblip-device=${FAKE_DEV}\n`);
  const lacking = join(td, "tokens-without-blip");
  writeFileSync(lacking, "# EXAMPLE: no blip lines\nkv-read=EXAMPLE-kv\n");
  try { chmodSync(good, 0o600); chmodSync(lacking, 0o600); } catch { /* Windows: token-file.ts warns instead */ }
  // The re-run counter: CREDS_DEPTH is exported before the helper, so it survives exec into the wrapper and counts
  // every start of the child. depth 1 = no re-run, 2 = one; past 3 the child stops itself, so a runaway is a red row
  // and not a hang.
  const child = join(td, "child.sh");
  writeFileSync(child, `export CREDS_DEPTH=$(( \${CREDS_DEPTH:-0} + 1 ))\n` +
    `if [ "$CREDS_DEPTH" -gt 3 ]; then echo "ran: runaway, depth $CREDS_DEPTH"; exit 9; fi\n` +
    `. "${helper}"; blip_creds_or_reexec "$0" "$@"\n` +
    `echo "ran: key \${#BLIP_KEY} dev \${#BLIP_DEVICE} reexec \${BLIPSCOPE_CREDS_REEXEC:-0} depth $CREDS_DEPTH"\n`);
  const base: Record<string, string | undefined> = { ...process.env };
  delete base.BLIP_KEY; delete base.BLIP_DEVICE; delete base.BLIPSCOPE_CREDS_REEXEC; delete base[TOKEN_FILE_ENV];
  delete base.CREDS_DEPTH;
  const go = (env: Record<string, string | undefined>) => {
    const r = spawnSync("bash", [child.replace(/\\/g, "/")], { env, encoding: "utf8" });
    const all = (r.stdout ?? "") + (r.stderr ?? "");
    return { all, runs: (r.stdout ?? "").split("\n").filter((l) => l.startsWith("ran:")) };
  };
  let bad = 0;
  const check = (ok: boolean, what: string) => { console.log(`  ${ok ? "ok " : "BAD"}  ${what}`); if (!ok) bad++; };
  // T4: the file supplies both; the child sees them at the right lengths; no byte of output holds either value.
  const t4 = go({ ...base, [TOKEN_FILE_ENV]: good });
  check(t4.runs.length === 1 && t4.runs[0] === `ran: key ${FAKE_KEY.length} dev ${FAKE_DEV.length} reexec 1 depth 2`,
        `T4: one re-run, the child holds both values at their lengths (${t4.runs.join(" | ") || "no run"})`);
  check(!t4.all.includes(FAKE_KEY) && !t4.all.includes(FAKE_DEV),
        "T4: neither value appears in any byte the wrapper or the child wrote");
  // T5: the variables set: no re-run at all, the variables as they are.
  const t5 = go({ ...base, BLIP_KEY: "EXAMPLE-var-key", BLIP_DEVICE: FAKE_DEV, [TOKEN_FILE_ENV]: good });
  check(t5.runs.length === 1 && t5.runs[0] === `ran: key 15 dev ${FAKE_DEV.length} reexec 0 depth 1`,
        `T5: the variables set, no re-run (${t5.runs.join(" | ") || "no run"})`);
  // T6: unset, and the file lacks them: exactly one re-run, which then finds them still unset and does not re-run.
  const t6 = go({ ...base, [TOKEN_FILE_ENV]: lacking });
  check(t6.runs.length === 1 && t6.runs[0] === "ran: key 0 dev 0 reexec 1 depth 2" && t6.all.includes('no "blip-key" line'),
        `T6: unset twice, exactly one re-run and the file's reason named (${t6.runs.join(" | ") || "no run"})`);
  rmSync(td, { recursive: true, force: true });
  console.log(bad ? `SELFTEST FAILED (${bad})` : "SELFTEST PASSED");
  return bad ? 1 : 0;
}

const argv = process.argv.slice(2);
if (argv[0] === "--selftest") process.exit(selftest());
if (!argv[0]) { console.error("usage: with-blip-creds.mts <script.sh> [args...] | --selftest"); process.exit(2); }
process.exit(run(argv[0], argv.slice(1)));
