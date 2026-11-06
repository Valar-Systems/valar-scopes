// The account's R2 operations this month, by bucket and class (docs/basemap-route.md, the Class A rule): the
// figure stated before any ingest or repair run. READ-ONLY; prints counts only, never the token.
//
//   BLIPSCOPE_TOKEN_FILE=~/.config/blipscope/tokens npx tsx scripts/r2-class-a.ts
//
// The classes are Cloudflare's R2 pricing page's lists, verbatim (read 2026-10-29; the page dated 2026-10-26), with its
// free operations. An action type on none of the three lists is printed as UNCLASSIFIED, never guessed into a class.
import { readTokenFile } from "./token-file";

const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"; // same as wrangler.toml account_id
const CLASS_A = new Set(["ListBuckets", "PutBucket", "ListObjects", "PutObject", "CopyObject", "CompleteMultipartUpload", "CreateMultipartUpload",
  "LifecycleStorageTierTransition", "ListMultipartUploads", "UploadPart", "UploadPartCopy", "ListParts", "PutBucketEncryption", "PutBucketCors",
  "PutBucketLifecycleConfiguration"]);
const CLASS_B = new Set(["HeadBucket", "HeadObject", "GetObject", "UsageSummary", "GetBucketEncryption", "GetBucketLocation", "GetBucketCors",
  "GetBucketLifecycleConfiguration"]);
const FREE_OPS = new Set(["DeleteObject", "DeleteBucket", "AbortMultipartUpload"]);
const FREE = { a: 1_000_000, b: 10_000_000 };

const now = new Date();
const start = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), 1));
const query = `query($a: string!, $s: Time!, $e: Time!) { viewer { accounts(filter: {accountTag: $a}) {
  r2OperationsAdaptiveGroups(limit: 10000, filter: {datetime_geq: $s, datetime_leq: $e}) { sum { requests } dimensions { actionType bucketName } } } } }`;
const res = await fetch("https://api.cloudflare.com/client/v4/graphql", {
  method: "POST",
  headers: { Authorization: `Bearer ${readTokenFile("analytics-read")}`, "Content-Type": "application/json" },
  body: JSON.stringify({ query, variables: { a: ACCOUNT, s: start.toISOString(), e: now.toISOString() } }),
});
const body = (await res.json()) as { data?: { viewer: { accounts: Array<{ r2OperationsAdaptiveGroups: Array<{ sum: { requests: number }; dimensions: { actionType: string; bucketName: string } }> }> } }; errors?: unknown };
if (!res.ok || body.errors || !body.data) { console.error(`analytics query failed: HTTP ${res.status} ${JSON.stringify(body.errors ?? "").slice(0, 400)}`); process.exit(2); }
const rows = body.data.viewer.accounts[0]?.r2OperationsAdaptiveGroups ?? [];
// CONTROL: a month-to-date with no rows at all would be a blind query, not a quiet account
if (!rows.length) { console.error("no R2 operations returned at all: the query cannot be trusted (BLIND)"); process.exit(2); }
const per = new Map<string, { a: number; b: number }>();
let a = 0, b = 0;
const unknown = new Map<string, number>();
for (const r of rows) {
  const k = r.dimensions.bucketName || "(account)";
  const t = per.get(k) ?? { a: 0, b: 0 };
  if (CLASS_A.has(r.dimensions.actionType)) { t.a += r.sum.requests; a += r.sum.requests; }
  else if (CLASS_B.has(r.dimensions.actionType)) { t.b += r.sum.requests; b += r.sum.requests; }
  else if (!FREE_OPS.has(r.dimensions.actionType)) unknown.set(r.dimensions.actionType, (unknown.get(r.dimensions.actionType) ?? 0) + r.sum.requests);
  per.set(k, t);
}
const days = (now.getTime() - start.getTime()) / 86_400_000;
const monthDays = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth() + 1, 0)).getUTCDate();
console.log(`month to date (${start.toISOString().slice(0, 10)} .. ${now.toISOString().slice(0, 16)}Z, ${days.toFixed(2)} of ${monthDays} days)`);
for (const [k, t] of [...per].sort()) console.log(`  ${k.padEnd(28)} class A ${String(t.a).padStart(9)}  class B ${String(t.b).padStart(10)}`);
console.log(`  ${"TOTAL".padEnd(28)} class A ${String(a).padStart(9)}  class B ${String(b).padStart(10)}`);
console.log(`projected over the month at this rate: class A ${Math.round(a / days * monthDays)} (free ${FREE.a}), class B ${Math.round(b / days * monthDays)} (free ${FREE.b})`);
for (const [k, n] of unknown) console.log(`  UNCLASSIFIED action ${k}: ${n}`);

// storage: each bucket's latest payload size this month (the free tier is 10 GB-month, averaged over daily peaks)
const sq = `query($a: string!, $s: Time!, $e: Time!) { viewer { accounts(filter: {accountTag: $a}) {
  r2StorageAdaptiveGroups(limit: 1000, filter: {datetime_geq: $s, datetime_leq: $e}, orderBy: [datetime_DESC]) { max { payloadSize objectCount } dimensions { bucketName datetime } } } } }`;
const sr = await fetch("https://api.cloudflare.com/client/v4/graphql", {
  method: "POST", headers: { Authorization: `Bearer ${readTokenFile("analytics-read")}`, "Content-Type": "application/json" },
  body: JSON.stringify({ query: sq, variables: { a: ACCOUNT, s: new Date(now.getTime() - 2 * 86_400_000).toISOString(), e: now.toISOString() } }),
});
const sb = (await sr.json()) as { data?: { viewer: { accounts: Array<{ r2StorageAdaptiveGroups: Array<{ max: { payloadSize: number; objectCount: number }; dimensions: { bucketName: string } }> }> } }; errors?: unknown };
if (!sr.ok || sb.errors || !sb.data) { console.error(`storage query failed: HTTP ${sr.status} ${JSON.stringify(sb.errors ?? "").slice(0, 400)}`); process.exit(2); }
const latest = new Map<string, { payloadSize: number; objectCount: number }>();
for (const g of sb.data.viewer.accounts[0]?.r2StorageAdaptiveGroups ?? []) if (!latest.has(g.dimensions.bucketName)) latest.set(g.dimensions.bucketName, g.max);
let gb = 0;
for (const [k, v] of [...latest].sort()) { gb += v.payloadSize / 1e9; console.log(`  storage ${k.padEnd(26)} ${(v.payloadSize / 1e9).toFixed(3)} GB, ${v.objectCount} objects`); }
console.log(`  storage TOTAL ${gb.toFixed(3)} GB (free 10 GB-month)`);
