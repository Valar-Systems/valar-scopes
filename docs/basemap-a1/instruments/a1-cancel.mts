// SCRATCH, read-only: the slot's "0 cancellation lines" total (docs/basemap-a1.md §6). Every staging-proxy event in the
// window (paged), searched whole for the runtime's waitUntil cancellation text. THE ANCHOR: the same events must hold
// the window's /basemap request lines (`evt:"req"` on the basemap path), else the read is BLIND -- an empty window is
// not "0 cancellations". Prints counts only: never an event body, a device id or a query.
//   tsx a1-cancel.mts <fromISO> <toISO> <expected basemap request lines>
import { resolveToken } from "./wt-main/proxy/scripts/token-file.ts";
const { token } = resolveToken("observability-read" as never);
const [fromIso, toIso, want] = process.argv.slice(2);
const body = (offset?: string) => JSON.stringify({ queryId: "a1-cancel", view: "events", limit: 500, ...(offset ? { offset } : {}),
  timeframe: { from: Date.parse(fromIso!), to: Date.parse(toIso!) },
  parameters: { filters: [{ key: "$metadata.service", operation: "eq", type: "string", value: "blipscope-proxy-staging" }] } });
const all: any[] = [];
let offset: string | undefined, failed = false;
for (let page = 0; page < 20; page++) {
  const res = await fetch("https://api.cloudflare.com/client/v4/accounts/48822e896bb10c45aa6bfe139bcff3d1/workers/observability/telemetry/query", {
    method: "POST", headers: { Authorization: `Bearer ${token}`, "content-type": "application/json" }, body: body(offset) });
  const j = await res.json() as any;
  if (!res.ok || j?.success === false) { console.log(`BLIND: query failed, HTTP ${res.status}`); failed = true; break; }
  const ev = (j?.result?.events?.events ?? []) as any[];
  all.push(...ev);
  if (ev.length < 500) break;
  offset = ev[ev.length - 1]?.$metadata?.id;
  if (!offset) break;
}
const text = (e: any) => JSON.stringify(e);
const cancel = all.filter((e) => /waitUntil\(\) tasks did not complete|have been cancelled/i.test(text(e))).length;
const reqLines = all.filter((e) => e.source?.evt === "req" && e.$workers?.event?.request?.path === "/api/v1/blipscope/basemap").length;
const anchored = reqLines === Number(want);
if (!failed) console.log(`events ${all.length}; basemap request lines ${reqLines} (want ${want}): ${anchored ? "anchor ok" : "ANCHOR OFF"}; cancellation lines ${cancel}`);
// No process.exit (ruled ~04:32Z, Q-S4): it fired a libuv assertion on Windows while fetch's sockets were closing.
process.exitCode = !failed && anchored ? 0 : 2;
