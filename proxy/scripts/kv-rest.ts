/**
 * kv-rest.ts -- read-only access to a KV namespace through Cloudflare's REST
 * API, for the photo ingest's verifier.
 *
 * READ-ONLY ON PURPOSE. Writes stay on wrangler behind the allowlist
 * (publish-guard.ts); this client has no put. The verifier uses the bulk read
 * (100 keys a call) rather than one wrangler call per key, so checking all 948
 * pointers takes seconds instead of twenty minutes.
 */
import { readFileSync } from "node:fs";

export interface KvTarget {
  accountId: string;
  namespaceId: string;
  token: string;
}

// The account and the ENRICH_KV namespace for one wrangler env, read from the
// same wrangler.toml the writes go through -- so the verifier cannot check a
// different namespace from the one wrangler wrote to.
export function kvTargetFromWranglerToml(tomlPath: string, env: string, token: string): KvTarget {
  const toml = readFileSync(tomlPath, "utf8");
  const account = toml.match(/^account_id\s*=\s*"([0-9a-f]{32})"/m)?.[1];
  const header = `[[env.${env}.kv_namespaces]]`;
  const at = toml.indexOf(header);
  if (!account) throw new Error(`${tomlPath}: no account_id`);
  if (at < 0) throw new Error(`${tomlPath}: no ${header}`);
  // The block runs to the next [ header. Within it, the ENRICH_KV binding's id.
  const rest = toml.slice(at + header.length);
  const block = rest.slice(0, rest.search(/^\[/m) >= 0 ? rest.search(/^\[/m) : rest.length);
  if (!/binding\s*=\s*"ENRICH_KV"/.test(block)) throw new Error(`${header} is not the ENRICH_KV binding`);
  const id = block.match(/^id\s*=\s*"([0-9a-f]{32})"/m)?.[1];
  if (!id) throw new Error(`${header}: no namespace id`);
  return { accountId: account, namespaceId: id, token };
}

// The token for a READ. PHOTO_KV_READ_TOKEN is the KV-read-only token the
// render-drift job runs with -- that job sets no write token at all. Anywhere
// else (a publish, a local run) the ordinary CLOUDFLARE_API_TOKEN reads too.
// `||`, not `??`: an empty variable must fall through, not blind the read.
export function readToken(): string {
  return process.env.PHOTO_KV_READ_TOKEN || process.env.CLOUDFLARE_API_TOKEN || "";
}

const base = (t: KvTarget) =>
  `https://api.cloudflare.com/client/v4/accounts/${t.accountId}/storage/kv/namespaces/${t.namespaceId}`;

/** key -> value, or null when the key does not exist. Throws on any API failure. */
export async function bulkGet(t: KvTarget, keys: string[]): Promise<Record<string, string | null>> {
  const out: Record<string, string | null> = {};
  for (let i = 0; i < keys.length; i += 100) {
    const r = await fetch(`${base(t)}/bulk/get`, {
      method: "POST",
      headers: { Authorization: `Bearer ${t.token}`, "Content-Type": "application/json" },
      body: JSON.stringify({ keys: keys.slice(i, i + 100) }),
    });
    const d = (await r.json().catch(() => ({}))) as { success?: boolean; result?: { values?: Record<string, string | null> }; errors?: unknown };
    if (!r.ok || !d.success || !d.result?.values) {
      throw new Error(`KV bulk read failed: HTTP ${r.status} ${JSON.stringify(d.errors ?? "")}`);
    }
    Object.assign(out, d.result.values);
  }
  return out;
}

/** One key's value, or null when the key does not exist. Throws on any other failure. */
export async function getValue(t: KvTarget, key: string): Promise<string | null> {
  const r = await fetch(`${base(t)}/values/${encodeURIComponent(key)}`, { headers: { Authorization: `Bearer ${t.token}` } });
  if (r.status === 404) return null;
  const text = await r.text();
  if (!r.ok) throw new Error(`KV read of ${key} failed: HTTP ${r.status} ${text.slice(0, 200)}`);
  return text;
}

/** Every key name under a prefix. Throws on any API failure. */
export async function listKeys(t: KvTarget, prefix: string): Promise<Set<string>> {
  const names = new Set<string>();
  let cursor = "";
  do {
    const r = await fetch(
      `${base(t)}/keys?prefix=${encodeURIComponent(prefix)}&limit=1000${cursor ? `&cursor=${encodeURIComponent(cursor)}` : ""}`,
      { headers: { Authorization: `Bearer ${t.token}` } },
    );
    const d = (await r.json().catch(() => ({}))) as { success?: boolean; result?: { name: string }[]; result_info?: { cursor?: string }; errors?: unknown };
    if (!r.ok || !d.success || !d.result) throw new Error(`KV key list failed: HTTP ${r.status} ${JSON.stringify(d.errors ?? "")}`);
    for (const k of d.result) names.add(k.name);
    cursor = d.result_info?.cursor ?? "";
  } while (cursor);
  return names;
}

export const ABSENT_CONTROL_KEY = "pptr:t:VERIFY-CONTROL-ABSENT";

// The two anchor controls. Any exception counts as "cannot see" -- a dead token
// must come back UNTRUSTWORTHY, never as a list of missing keys.
export async function readControls(t: KvTarget, presentKey: string) {
  try {
    const v = await bulkGet(t, [presentKey, ABSENT_CONTROL_KEY]);
    return {
      knownPresentReadsBack: typeof v[presentKey] === "string" && (v[presentKey] as string).length > 0,
      knownAbsentReadsNull: v[ABSENT_CONTROL_KEY] === null,
      error: "",
    };
  } catch (err) {
    return { knownPresentReadsBack: false, knownAbsentReadsNull: false, error: String(err instanceof Error ? err.message : err) };
  }
}
