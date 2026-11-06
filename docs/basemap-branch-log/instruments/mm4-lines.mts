// SCRATCH, read-only: staging's /basemap request lines in a window, with the branch label (`bm`, #534) beside the
// status, colo and ms. Prints those fields only; never a device id or a query string.
//   tsx mm4-lines.mts <fromISO> <toISO>
import { resolveToken } from "./wt-main/proxy/scripts/token-file.ts";
const { token } = resolveToken("observability-read" as never);
const [fromIso, toIso] = process.argv.slice(2);
const res = await fetch("https://api.cloudflare.com/client/v4/accounts/48822e896bb10c45aa6bfe139bcff3d1/workers/observability/telemetry/query", {
  method: "POST", headers: { Authorization: `Bearer ${token}`, "content-type": "application/json" },
  body: JSON.stringify({ queryId: "mm4-lines", view: "events", limit: 500, timeframe: { from: Date.parse(fromIso!), to: Date.parse(toIso!) },
    parameters: { filters: [{ key: "$metadata.service", operation: "eq", type: "string", value: "blipscope-proxy-staging" },
      { key: "$workers.event.request.path", operation: "eq", type: "string", value: "/api/v1/blipscope/basemap" }] } }),
});
const j = await res.json() as any;
if (!res.ok || j?.success === false) { console.log(`query failed: HTTP ${res.status}`); process.exit(2); }
const ev = ((j?.result?.events?.events ?? []) as any[]).filter((e) => e.source?.evt === "req").sort((a, b) => a.timestamp - b.timestamp);
console.log(`/basemap request lines [${fromIso}, ${toIso}): ${ev.length}; with bm: ${ev.filter((e) => e.source?.bm !== undefined).length}`);
for (const e of ev) console.log(`  ${new Date(e.timestamp).toISOString()} status ${e.source?.status} bm ${e.source?.bm ?? "<absent>"} colo ${e.source?.colo} ms ${e.source?.ms}`);
