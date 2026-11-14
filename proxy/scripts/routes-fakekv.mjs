#!/usr/bin/env node
// routes-fakekv.mjs -- TESTS ONLY. A stand-in for `node scripts/wrangler-run.mjs kv ...` that keeps KV in a JSON file,
// so the weekly refresh's writer and verifier can be graded red-first with no network and no real namespace
// (docs/route-weekly-code.md, GR8-GR11 and RB1). Selected by ROUTES_KV_CLI; check_routes_workflow.py refuses any
// workflow that sets ROUTES_KV_CLI, so it can never stand in for real KV in CI's refresh job.
//
//   ROUTES_FAKEKV_STORE=<file.json> node routes-fakekv.mjs kv key get <key> --binding=X --env=E --remote
//   ... kv key list --prefix=<p> ...   kv bulk put <file> ...   kv bulk delete <file> --force ...   kv bulk get <file> ...
// The store is { "<env>": { "<key>": "<value>" } }. ROUTES_FAKEKV_DROP=<n> silently drops every n-th key of a bulk put
// (the failure the real API has shown twice), so a verifier can be shown to catch it. ROUTES_FAKEKV_GET_SUFFIX=newline
// appends a newline to every `kv key get` (N2, docs/route-weekly-code.md 2q): real output may differ from the bytes put by
// a byte like that, so J7w can show both sides of J7 hash bytes read by the same reader.
import { existsSync, readFileSync, writeFileSync } from "node:fs";

const STORE = process.env.ROUTES_FAKEKV_STORE;
if (!STORE) { console.error("routes-fakekv: ROUTES_FAKEKV_STORE is not set"); process.exit(2); }
const argv = process.argv.slice(2);
const flag = (name) => { const a = argv.find((x) => x.startsWith(`--${name}=`)); return a ? a.slice(name.length + 3) : undefined; };
const env = flag("env") ?? "top";
const pos = argv.filter((x) => !x.startsWith("--"));
const db = existsSync(STORE) ? JSON.parse(readFileSync(STORE, "utf8")) : {};
const ns = (db[env] ??= {});
const save = () => writeFileSync(STORE, JSON.stringify(db));
const unq = (s) => s.replace(/^'(.*)'$/, "$1").replace(/^"(.*)"$/, "$1");

const [kv, a, b, target] = pos;
if (kv !== "kv") { console.error(`routes-fakekv: unsupported: ${pos.join(" ")}`); process.exit(2); }
if (a === "key" && b === "get") {
  const k = unq(target ?? "");
  if (!(k in ns)) { console.error("Value not found"); process.exit(1); }
  process.stdout.write(ns[k] + (process.env.ROUTES_FAKEKV_GET_SUFFIX === "newline" ? "\n" : ""));
} else if (a === "key" && b === "list") {
  const p = flag("prefix") ?? "";
  process.stdout.write(JSON.stringify(Object.keys(ns).filter((k) => k.startsWith(p)).map((name) => ({ name }))));
} else if (a === "bulk" && b === "put") {
  const pairs = JSON.parse(readFileSync(unq(target), "utf8"));
  const drop = Number(process.env.ROUTES_FAKEKV_DROP ?? 0);
  pairs.forEach((p, i) => { if (!(drop > 0 && (i + 1) % drop === 0)) ns[p.key] = p.value; });
  save();
  console.log("Success!");
} else if (a === "bulk" && b === "delete") {
  for (const k of JSON.parse(readFileSync(unq(target), "utf8"))) delete ns[k];
  save();
  console.log("Success!");
} else if (a === "bulk" && b === "get") {
  // ROUTES_FAKEKV_BULKGET_OMIT=1 drops the first requested key from each answer (2q amendment 3, BK-R): a short answer
  // must be refused, never read as "not found".
  const out = {}, omit = process.env.ROUTES_FAKEKV_BULKGET_OMIT === "1";
  JSON.parse(readFileSync(unq(target), "utf8")).forEach((k, i) => { if (!(omit && i === 0)) out[k] = k in ns ? ns[k] : null; });
  console.log(JSON.stringify(out, null, 2));
  console.log("Success!");
} else {
  console.error(`routes-fakekv: unsupported: ${pos.join(" ")}`); process.exit(2);
}
