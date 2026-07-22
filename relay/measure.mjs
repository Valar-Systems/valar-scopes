#!/usr/bin/env node
// Relay log analyzer for the post-fix soak. Reads nginx relay-log lines on stdin
// (format: `<iso8601> cache=<status> status=<code> ustatus=<code> rt=<s> uri="<u>"`)
// and reports, SPLIT by workload (positions /v2/lat vs enrichment /v2/hex):
//   - upstream request rate (fetches that actually hit adsb.lol, i.e. ustatus != -)
//   - 429 rate on those upstream fetches
//   - X-Cache flip distribution (HIT/MISS/STALE/EXPIRED/UPDATING)
//   - longest DEGRADED run for positions (consecutive stale-because-upstream-failed)
// These are the honest numbers for the adsb.lol sponsorship/paid-tier email.
//
// Usage (per box, or concatenate both):
//   ssh root@<ip> "cat /var/log/nginx/relay.log" | node relay/measure.mjs
//   ssh root@<ip> "cat /var/log/nginx/relay.log" | node relay/measure.mjs --hours 24
//
// --hours N  limits to the last N hours (by the newest line's timestamp); default: all.

const args = process.argv.slice(2);
const hoursIdx = args.indexOf("--hours");
const windowH = hoursIdx >= 0 ? Number(args[hoursIdx + 1]) : null;

const LINE = /^(\S+) cache=(\S+) status=(\d+) ustatus=(\S+) rt=(\S+) .*uri="([^"]*)"/;

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString("utf8");
}

function classify(uri) {
  if (uri.startsWith("/v2/hex")) return "hex";
  if (uri.startsWith("/v2/lat")) return "pos";
  if (uri.startsWith("/api/0/routeset")) return "route";
  return "other";
}
const isUpstream = (u) => u !== "-" && u !== ""; // an actual fetch reached adsb.lol
const pct = (n, d) => (d ? ((100 * n) / d).toFixed(1) + "%" : "n/a");
const perHour = (n, secs) => (secs > 0 ? (n / (secs / 3600)).toFixed(1) : "n/a");

const rows = [];
for (const line of (await readStdin()).split("\n")) {
  const m = LINE.exec(line);
  if (!m) continue;
  const t = Date.parse(m[1]);
  if (Number.isNaN(t)) continue;
  rows.push({ t, cache: m[2], status: m[3], ustatus: m[4], cls: classify(m[6]) });
}
if (!rows.length) {
  console.log("no parseable relay-log lines on stdin");
  process.exit(0);
}
rows.sort((a, b) => a.t - b.t);
const newest = rows[rows.length - 1].t;
const cutoff = windowH ? newest - windowH * 3600e3 : rows[0].t;
const win = rows.filter((r) => r.t >= cutoff);
const spanSec = (newest - win[0].t) / 1000 || 1;

function report(cls) {
  const r = win.filter((x) => x.cls === cls);
  if (!r.length) return `  ${cls}: (none)`;
  const up = r.filter((x) => isUpstream(x.ustatus));
  const u429 = up.filter((x) => x.ustatus === "429").length;
  const cacheDist = {};
  for (const x of r) cacheDist[x.cache] = (cacheDist[x.cache] || 0) + 1;
  const cd = Object.entries(cacheDist).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}:${v}`).join(" ");
  return [
    `  ${cls.toUpperCase()}  (${r.length} requests)`,
    `    upstream fetches : ${up.length}  (${perHour(up.length, spanSec)}/h)   <- what adsb.lol actually sees`,
    `    429 on fetches   : ${u429}  (${pct(u429, up.length)})`,
    `    X-Cache          : ${cd}`,
  ].join("\n");
}

// Longest degraded run for positions: consecutive /v2/lat requests served STALE because
// the upstream refresh failed (ustatus=429/5xx). Measured as the wall-clock span from the
// first to the last such request in the run (a run breaks on any fresh 200 or a >5min gap).
function longestDegraded() {
  const pos = win.filter((x) => x.cls === "pos");
  let runStart = null, runEnd = null, prev = null, best = 0, bestFrom = null;
  const failed = (x) => x.cache === "STALE" && (x.ustatus === "429" || /^5\d\d$/.test(x.ustatus));
  for (const x of pos) {
    const bad = failed(x);
    const gap = prev ? x.t - prev.t : 0;
    if (bad && runStart !== null && gap <= 300e3) {
      runEnd = x.t;
    } else if (bad) {
      runStart = x.t; runEnd = x.t;
    } else {
      if (runStart !== null && runEnd - runStart > best) { best = runEnd - runStart; bestFrom = runStart; }
      runStart = null;
    }
    prev = x;
  }
  if (runStart !== null && runEnd - runStart > best) { best = runEnd - runStart; bestFrom = runStart; }
  return { sec: Math.round(best / 1000), from: bestFrom ? new Date(bestFrom).toISOString() : null };
}

const deg = longestDegraded();
console.log(`\n=== relay measurement: ${win.length} requests over ${(spanSec / 3600).toFixed(1)}h ` +
  `(${new Date(win[0].t).toISOString()} .. ${new Date(newest).toISOString()}) ===\n`);
console.log(report("pos"));
console.log(report("hex"));
console.log(report("route"));
console.log(`\n  positions longest DEGRADED run (stale, upstream failing): ${deg.sec}s` +
  (deg.from ? ` starting ${deg.from}` : " (none)"));
console.log(`\n  PASS targets: hex 429 near 0 + upstream req/HOUR (not /s); positions 429 < a few %;`);
console.log(`  X-Cache mostly HIT; longest degraded run bounded (dead-reckoning covers < ~60s).\n`);
