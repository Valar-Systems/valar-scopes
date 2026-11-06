// SCRATCH, read-only: the staging proxy's INVOCATION events for /basemap in a window -- time, cpuTimeMs, wallTimeMs,
// outcome and response status. Paged by time slices so no window is truncated. Never a device id or a query string.
//   tsx d3-cpu.mts <fromISO> <toISO>
import { resolveToken } from "./wt-main/proxy/scripts/token-file.ts";
const { token } = resolveToken("observability-read" as never);
const [fromIso, toIso] = process.argv.slice(2);
const out: any[] = [];
const tally: Record<string, number> = {};
let seen = 0;
for (let t = Date.parse(fromIso!); t < Date.parse(toIso!); t += 60_000) {
  const res = await fetch("https://api.cloudflare.com/client/v4/accounts/48822e896bb10c45aa6bfe139bcff3d1/workers/observability/telemetry/query", {
    method: "POST", headers: { Authorization: `Bearer ${token}`, "content-type": "application/json" },
    body: JSON.stringify({ queryId: "d3-cpu", view: "events", limit: 1000, timeframe: { from: t, to: Math.min(t + 60_000, Date.parse(toIso!)) },
      parameters: { filters: [{ key: "$metadata.service", operation: "eq", type: "string", value: "blipscope-proxy-staging" }] } }),
  });
  const j = await res.json() as any;
  const ev = (j?.result?.events?.events ?? []) as any[];
  seen += ev.length;
  for (const e of ev) {
    const w0 = e.$workers ?? {};
    const path0 = w0.event?.request?.path ?? (w0.event?.request?.url ? new URL(w0.event.request.url).pathname : "-");
    tally[`${e.$metadata?.type ?? "?"} | cpu ${w0.cpuTimeMs !== undefined} | ${path0}`] = (tally[`${e.$metadata?.type ?? "?"} | cpu ${w0.cpuTimeMs !== undefined} | ${path0}`] ?? 0) + 1;
    const w = e.$workers ?? {};
    if (w.cpuTimeMs === undefined) continue;
    if (path0 !== "/api/v1/blipscope/basemap") continue;
    out.push({ t: e.timestamp, cpu: w.cpuTimeMs, wall: w.wallTimeMs, outcome: w.outcome, status: w.event?.response?.status, inm: !!w.event?.request?.headers?.["if-none-match"] });
  }
}
console.log(`staging proxy invocation events [${fromIso}, ${toIso}): ${seen} event(s) scanned; /basemap invocations ${out.length}`);
for (const [k, n] of Object.entries(tally)) console.log(`  kind: ${k}: ${n}`);
for (const o of out.sort((a, b) => a.t - b.t)) console.log(`  ${new Date(o.t).toISOString()} status ${o.status} outcome ${o.outcome} cpuTimeMs ${o.cpu} wallTimeMs ${o.wall}`);
