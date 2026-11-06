// SCRATCH, read-only: the staging builder's bm_build lines in a window (#541, docs/basemap-build-reads.md). Prints the
// line's integer fields only: status, reads, bytes, readMs, maxReadMs, wallMs. Never a token, never a query.
//   tsx bb-lines.mts <fromISO> <toISO>
import { resolveToken } from "./wt-main/proxy/scripts/token-file.ts";
const { token } = resolveToken("observability-read" as never);
const [fromIso, toIso] = process.argv.slice(2);
const res = await fetch("https://api.cloudflare.com/client/v4/accounts/48822e896bb10c45aa6bfe139bcff3d1/workers/observability/telemetry/query", {
  method: "POST", headers: { Authorization: `Bearer ${token}`, "content-type": "application/json" },
  body: JSON.stringify({ queryId: "bb-lines", view: "events", limit: 500, timeframe: { from: Date.parse(fromIso!), to: Date.parse(toIso!) },
    parameters: { filters: [{ key: "$metadata.service", operation: "eq", type: "string", value: "blipscope-basemap-staging" }] } }),
});
const j = await res.json() as any;
if (!res.ok || j?.success === false) { console.log(`query failed: HTTP ${res.status}`); process.exit(2); }
const all = (j?.result?.events?.events ?? []) as any[];
const ev = all.filter((e) => e.source?.evt === "bm_build").sort((a, b) => a.timestamp - b.timestamp);
console.log(`builder events [${fromIso}, ${toIso}): ${all.length}; bm_build lines: ${ev.length}`);
for (const e of ev) {
  const s = e.source;
  console.log(`  ${new Date(e.timestamp).toISOString()} status ${s.status} reads ${s.reads} bytes ${s.bytes} readMs ${s.readMs} maxReadMs ${s.maxReadMs} wallMs ${s.wallMs}`);
}
