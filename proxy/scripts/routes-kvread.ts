// routes-kvread.ts -- the one `kv bulk get` reader the route tools share (docs/route-weekly-code.md, section 2q): the
// verify's values check and the writer's backup read the same way, so the two can never disagree about a value.
import { execSync } from "node:child_process";
import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { q, sh } from "./shquote";

/** `kv bulk get` through the helper (or ROUTES_KV_CLI in tests), 100 keys a call: the JSON object between the first "{"
 *  and the last "}" (wrangler prints "Success!" after it). The same parse as #700's D2, which read 36,786 production
 *  values with it. A key the answer does not mention is ABSENT from the map: only an answer's own `null` means "not
 *  found" (2q amendment 3), so a caller can tell a short answer from a missing key. */
export function bulkGet(keys: string[], env: string, tmp: string, kvCli: string): Map<string, string | null> {
  const out = new Map<string, string | null>();
  for (let i = 0; i < keys.length; i += 100) {
    const f = join(tmp, `bulk-get-${i / 100}.json`);
    writeFileSync(f, JSON.stringify(keys.slice(i, i + 100)));
    const stdout = execSync(["node", kvCli, "kv", "bulk", "get", q(sh(f)), "--binding=ENRICH_KV", `--env=${env}`, "--remote"].join(" "),
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 64 * 1024 * 1024 });
    const start = stdout.indexOf("{"), end = stdout.lastIndexOf("}");
    if (start < 0 || end < start) throw new Error(`bulk get chunk ${i / 100}: no JSON object in the output`);
    for (const [k, v] of Object.entries(JSON.parse(stdout.slice(start, end + 1)) as Record<string, string | null>)) out.set(k, v);
  }
  return out;
}
