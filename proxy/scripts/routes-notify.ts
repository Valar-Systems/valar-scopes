// routes-notify.ts -- W9 (docs/route-weekly-code.md): the readers of a failed or refused routes-refresh run, so a
// failure is never an instrument firing into a void. Condition 4 of the plan and the owner's ntfy yes (2026-11-13):
//   - ONE GitHub issue labelled `route-refresh`, opened, or commented on if one is open (reconcile-fleet.py counts them);
//   - ntfy to ROUTES_NTFY_TOPIC, on failure only. The topic is a secret and is never printed.
// Both carry a class from a CLOSED set and counts only: no id, no key, no position (SC4 grades every class).
//
//   npx tsx scripts/routes-notify.ts --class verify-fail --env production --count mismatched=1 [--dry-run]

export const CLASSES = [
  "refused-g1", "refused-g2", "refused-g3", "refused-g4", "refused-g5", "refused-g6",
  "verify-fail", "seal-refused", "source-unreachable", "secrets-not-set", "frozen-malformed", "token-expiring",
  "meta-not-key-level", "run-failed",
] as const;
export type NoticeClass = (typeof CLASSES)[number];

const TEXT: Record<NoticeClass, string> = {
  "refused-g1": "The new source is not whole (G1). Nothing was written or deleted, and the copy was not re-sealed.",
  "refused-g2": "An anchor callsign is missing from the new source (G2). Deletes were refused; the run did not seal.",
  "refused-g3": "A surviving shard lost too many keys (G3). Deletes were refused; the run did not seal.",
  "refused-g4": "Too many shards, or too many keys, were removed (G4). Deletes were refused; the run did not seal.",
  "refused-g5": "The deletes are over the scheduled cap (G5). Deletes were refused; the run did not seal.",
  "refused-g6": "A delete was outside the mirror's shape or provenance (G6). Deletes were refused; the run did not seal.",
  "verify-fail": "The verify after the write did not pass (values, absence or coverage). No receipt, so no seal.",
  "seal-refused": "The seal refused its receipt. meta:routes is unchanged.",
  "source-unreachable": "The route source could not be fetched. Nothing was written.",
  "secrets-not-set": "The routes-refresh environment is missing a required secret, or is not limited to main.",
  "frozen-malformed": "ROUTES_FREEZE_UNTIL is set but malformed. The run did not start.",
  "token-expiring": "ROUTES_KV_TOKEN is close to its expiry, or has none. Rotate it (the owner's 90-day condition).",
  "meta-not-key-level": "meta:routes was not written by a key-level seal, so this is a first run: that one is attended, never CI's.",
  "run-failed": "The run failed for a reason no other class names. Nothing was sealed; read the run's log.",
};

/** The notice, pure. Only integer counts are rendered; anything else is dropped, never coerced. */
export function renderNotice(cls: string, env: string, counts: Record<string, number>): { title: string; body: string } {
  if (!(CLASSES as readonly string[]).includes(cls)) throw new Error(`routes-notify: "${cls}" is not in the closed set`);
  if (env !== "staging" && env !== "production") throw new Error("routes-notify: env must be staging or production");
  const parts = Object.entries(counts).filter(([k, n]) => /^[a-z]{1,16}$/.test(k) && Number.isInteger(n) && n >= 0).map(([k, n]) => `${k} ${n}`);
  return {
    title: `Route refresh, ${env}: ${cls}`,
    body: `${TEXT[cls as NoticeClass]}${parts.length > 0 ? `\nCounts: ${parts.join(", ")}.` : ""}`,
  };
}

async function main() {
  const argv = process.argv.slice(2);
  const get = (f: string) => { const i = argv.indexOf(f); return i >= 0 ? argv[i + 1] : undefined; };
  const counts: Record<string, number> = {};
  argv.forEach((a, i) => { if (a === "--count") { const [k, v] = (argv[i + 1] ?? "").split("="); if (k) counts[k] = Number(v); } });
  const n = renderNotice(get("--class") ?? "", get("--env") ?? "", counts);
  if (argv.includes("--dry-run")) { console.log(n.title); console.log(n.body); return; }

  // The GitHub issue: the run link goes HERE only (a run id is not for the phone notice).
  const { execFileSync } = await import("node:child_process");
  const run = process.env.GITHUB_RUN_ID ? `\n\nRun: ${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}` : "";
  let issueOk = false;
  try {
    execFileSync("gh", ["label", "create", "route-refresh", "--force", "--color", "B60205", "--description", "a routes-refresh run failed or refused"], { stdio: "ignore" });
    const open = JSON.parse(execFileSync("gh", ["issue", "list", "--label", "route-refresh", "--state", "open", "--json", "number", "--limit", "1"], { encoding: "utf8" })) as { number: number }[];
    if (open[0]) execFileSync("gh", ["issue", "comment", String(open[0].number), "--body", `${n.title}\n\n${n.body}${run}`], { stdio: "ignore" });
    else execFileSync("gh", ["issue", "create", "--label", "route-refresh", "--title", n.title, "--body", `${n.body}${run}`], { stdio: "ignore" });
    issueOk = true;
  } catch { /* stated below; the reason is not echoed */ }
  console.log(`issue: ${issueOk ? "opened or updated" : "FAILED"}`);

  // ntfy, on failure only (this script runs only on failure). The topic is never printed.
  const topic = process.env.ROUTES_NTFY_TOPIC;
  let ntfy = "not sent (ROUTES_NTFY_TOPIC is not set)";
  if (topic) {
    try {
      const r = await fetch(`https://ntfy.sh/${encodeURIComponent(topic)}`, { method: "POST", headers: { Title: n.title }, body: n.body });
      ntfy = `HTTP ${r.status}`;
    } catch { ntfy = "FAILED (no response)"; }
  }
  console.log(`ntfy: ${ntfy}`);
  if (!issueOk || !/^HTTP 2\d\d$/.test(ntfy)) process.exit(1);
}

if (process.argv[1] && /routes-notify\.ts$/.test(process.argv[1].replace(/\\/g, "/"))) void main();
