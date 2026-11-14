// routes-fk1.ts -- FK1, the one home of the rule (docs/route-weekly-code.md, section 2e; the reviewer, 2026-11-14).
//
// ROUTES_KV_CLI swaps real KV for a JSON fake, for tests. Where a REAL credential is in reach it would turn a run into a
// silent green -- "writes" to a file, verified against the same file -- so it is refused there, before any KV call, by
// the writer and the verify alike. The rule is tied to the credential, not to "running in CI": workers.yml holds none
// of these, so the fake-KV selftest runs there as a normal step, and the routes-refresh job can never run on the fake.
//
// Variables are judged by NAME only. A refusal names the condition and never prints a value.

const CREDENTIAL_VARS = ["ROUTES_KV_TOKEN", "CLOUDFLARE_API_TOKEN"] as const;

/** The name of the condition that forbids the fake KV here, or null when it is allowed. */
export function fk1Refusal(
  env: Record<string, string | undefined>,
  target: string | undefined,
  exists: (path: string) => boolean,
): string | null {
  if (env.GITHUB_WORKFLOW === "routes-refresh") return "the routes-refresh workflow (GITHUB_WORKFLOW)";
  if (/\/\.github\/workflows\/routes-refresh\.yml(@|$)/.test(env.GITHUB_WORKFLOW_REF ?? "")) {
    return "the routes-refresh workflow (GITHUB_WORKFLOW_REF)";
  }
  if (target === "production") return "--env production";
  for (const v of CREDENTIAL_VARS) if (env[v]) return `a real credential variable is set (${v})`;
  const tf = env.BLIPSCOPE_TOKEN_FILE;
  if (tf && exists(tf)) return "a real credential file is in reach (BLIPSCOPE_TOKEN_FILE names a file that exists)";
  return null;
}
