// routes-env-check.ts -- W11, SC2 (docs/route-weekly-code.md, section 2.2): the routes-refresh job's first check. The
// owner's conditions say the environment's deployments are limited to main, and that the KV token and the ntfy topic
// exist. This reads the environment from the GitHub API and the secrets as BOOLEANS (the workflow passes each secret
// into this step's environment; only whether it is non-empty is ever looked at, never printed).
//
//   GITHUB_REPOSITORY=o/r npx --no-install tsx scripts/routes-env-check.ts     exit 0 start, 1 refuse
// It also appends `readback=on|off` to $GITHUB_OUTPUT: without the identity secrets the read-back is NOT RUN (RB1).

export interface EnvVerdict { start: boolean; lines: string[]; readback: boolean }
type Environment = { deployment_branch_policy?: { protected_branches?: boolean; custom_branch_policies?: boolean } | null } | null;
type Policies = { branch_policies?: { name?: string; type?: string }[] } | null;

const IDENTITY = ["ROUTES_TEST_DEVICE", "ROUTES_TEST_KEY", "ROUTES_TEST_KEY_STAGING"] as const;

/** The decision, pure: the environment's API answer, its branch policies, and which secret NAMES are set. */
export function envVerdict(environment: unknown, policies: unknown, present: Record<string, boolean>): EnvVerdict {
  const lines: string[] = [];
  const e = environment as Environment;
  let mainOnly = false;
  if (!e) lines.push("environment: not set");
  else {
    const p = e.deployment_branch_policy;
    const names = ((policies as Policies)?.branch_policies ?? []).map((b) => `${b.type ?? "branch"}:${b.name ?? ""}`);
    mainOnly = !!p && p.protected_branches === false && p.custom_branch_policies === true
      && names.length === 1 && names[0] === "branch:main";
    lines.push("environment: set");
  }
  lines.push(`main-only: ${mainOnly}`);
  const kv = present.ROUTES_KV_TOKEN === true, ntfy = present.ROUTES_NTFY_TOPIC === true;
  lines.push(`kv token: ${kv ? "set" : "not set"}`);
  lines.push(`ntfy: ${ntfy ? "set" : "not set"}`);
  const readback = IDENTITY.every((n) => present[n] === true);
  lines.push(`read-back identity: ${readback ? "set" : "not set"}`);
  return { start: !!e && mainOnly && kv && ntfy, lines, readback };
}

async function main() {
  const { execFileSync } = await import("node:child_process");
  const { appendFileSync } = await import("node:fs");
  const repo = process.env.GITHUB_REPOSITORY;
  if (!repo) { console.log("environment check: GITHUB_REPOSITORY is not set"); process.exit(1); }
  const api = (path: string): unknown => {
    try { return JSON.parse(execFileSync("gh", ["api", path], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] })); }
    catch { return null; } // a 404 (no environment) and a failed read both read as "not set": the job does not start
  };
  const environment = api(`repos/${repo}/environments/routes-refresh`);
  const policies = environment ? api(`repos/${repo}/environments/routes-refresh/deployment-branch-policies`) : null;
  const names = ["ROUTES_KV_TOKEN", "ROUTES_NTFY_TOPIC", ...IDENTITY];
  const present = Object.fromEntries(names.map((n) => [n, !!process.env[n]]));
  const v = envVerdict(environment, policies, present);
  for (const l of v.lines) console.log(l);
  if (process.env.GITHUB_OUTPUT) appendFileSync(process.env.GITHUB_OUTPUT, `readback=${v.readback ? "on" : "off"}\n`);
  console.log(v.start ? "environment check: start" : "environment check: REFUSING to start (secrets-not-set)");
  process.exit(v.start ? 0 : 1);
}

if (process.argv[1] && /routes-env-check\.ts$/.test(process.argv[1].replace(/\\/g, "/"))) void main();
