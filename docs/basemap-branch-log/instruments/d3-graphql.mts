// SCRATCH, read-only: blipscope-basemap-staging invocations per minute, from GraphQL workersInvocationsAdaptive.
//   tsx d3-graphql.mts <fromISO> <toISO>      prints each minute's requests, status, cpu and wall (us); never a token
import { resolveToken } from "./wt-main/proxy/scripts/token-file.ts";

const [from, to] = process.argv.slice(2);
const { token } = resolveToken("analytics-read");
const query = `query($a:String!,$f:Time!,$t:Time!){viewer{accounts(filter:{accountTag:$a}){
  workersInvocationsAdaptive(limit:200, filter:{scriptName:"blipscope-basemap-staging", datetime_geq:$f, datetime_leq:$t}, orderBy:[datetimeMinute_ASC]){
    sum{requests errors} quantiles{cpuTimeP50 cpuTimeP99 wallTimeP50 wallTimeP99} dimensions{datetimeMinute status}}}}}`;
const res = await fetch("https://api.cloudflare.com/client/v4/graphql", {
  method: "POST", headers: { Authorization: `Bearer ${token}`, "content-type": "application/json" },
  body: JSON.stringify({ query, variables: { a: "48822e896bb10c45aa6bfe139bcff3d1", f: from, t: to } }),
});
const j = await res.json() as { data?: any; errors?: Array<{ message: string }> };
const rows = j.data?.viewer?.accounts?.[0]?.workersInvocationsAdaptive;
if (!rows) { console.log(`BLIND: HTTP ${res.status}; errors ${JSON.stringify((j.errors ?? []).map((e) => e.message))}`); process.exit(2); }
const total = rows.reduce((s: number, r: any) => s + r.sum.requests, 0);
console.log(`builder [${from}, ${to}]: HTTP ${res.status}; ${rows.length} row(s); ${total} invocation(s)`);
for (const r of rows) {
  console.log(`  ${r.dimensions.datetimeMinute} status ${r.dimensions.status}: requests ${r.sum.requests}, errors ${r.sum.errors}; ` +
    `cpu p50 ${r.quantiles.cpuTimeP50} us, p99 ${r.quantiles.cpuTimeP99} us; wall p50 ${r.quantiles.wallTimeP50} us, p99 ${r.quantiles.wallTimeP99} us`);
}
