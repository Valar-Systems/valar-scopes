// routes-token-check.ts -- TK1 (docs/route-weekly-code.md, sections 2b and 2c): the owner's "90-day expiry; remind me
// 7 days before it ends", mechanised rather than remembered. Every weekly run asks Cloudflare's token-verify endpoint,
// authenticated by ROUTES_KV_TOKEN itself, for the token's own `expires_on`, and prints the DATE only. Inside 14 days
// it warns; weekly runs then warn once inside 14 days and again inside 7. A token with no expiry breaks the 90-day
// condition and warns the same way. A failed call, or a token that is not active, is a run failure, never a silent pass.
//
//   ROUTES_KV_TOKEN=... npx tsx scripts/routes-token-check.ts     exit 0 ok, 3 warn, 1 fail (the workflow maps 3 to W9)
//
// The token is read from the environment, sent only in the Authorization header, and never printed, in a verdict or
// in an error (the call's error text is not echoed, because a failed request's message can carry its headers).

const DAY_MS = 86_400_000;
export const WARN_DAYS = 14;
const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1";
const USER_VERIFY = "https://api.cloudflare.com/client/v4/user/tokens/verify";
const ACCOUNT_VERIFY = `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}/tokens/verify`;

export interface Verdict { action: "ok" | "warn" | "fail"; line: string }

/** The decision, pure: a verify answer (or anything else) and the time it was read. */
export function tokenVerdict(now: Date, answer: unknown): Verdict {
  const a = answer as { success?: unknown; result?: { status?: unknown; expires_on?: unknown } } | null;
  if (!a || a.success !== true || !a.result) return { action: "fail", line: "token check: the verify call failed" };
  if (a.result.status !== "active") return { action: "fail", line: "token check: ROUTES_KV_TOKEN is not active" };
  const exp = a.result.expires_on;
  if (typeof exp !== "string" || !Number.isFinite(Date.parse(exp))) {
    return { action: "warn", line: "token check: ROUTES_KV_TOKEN has no expiry, which breaks the 90-day condition" };
  }
  const days = Math.floor((Date.parse(exp) - now.getTime()) / DAY_MS);
  const date = new Date(Date.parse(exp)).toISOString().slice(0, 10);
  if (days <= WARN_DAYS) return { action: "warn", line: `token check: ROUTES_KV_TOKEN expires in ${days} days (${date})` };
  return { action: "ok", line: `token check: expiry ${date}, ${days} days out` };
}

async function verify(url: string, token: string): Promise<unknown> {
  try {
    const r = await fetch(url, { headers: { Authorization: `Bearer ${token}` } });
    return await r.json();
  } catch {
    return null; // the reason is not printed: see the header
  }
}

async function main() {
  const token = process.env.ROUTES_KV_TOKEN;
  if (!token) { console.log("token check: ROUTES_KV_TOKEN is not set"); process.exit(1); }
  // A user token verifies at /user/tokens/verify; an account-owned token answers there with an error and verifies at
  // the account's own endpoint. Which kind the owner creates is the owner's choice, so both are asked, in that order.
  let answer = await verify(USER_VERIFY, token);
  if (tokenVerdict(new Date(), answer).action === "fail") answer = await verify(ACCOUNT_VERIFY, token);
  const v = tokenVerdict(new Date(), answer);
  console.log(v.line);
  process.exit(v.action === "ok" ? 0 : v.action === "warn" ? 3 : 1);
}

if (process.argv[1] && /routes-token-check\.ts$/.test(process.argv[1].replace(/\\/g, "/"))) void main();
