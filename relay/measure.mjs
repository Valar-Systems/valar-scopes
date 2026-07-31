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

// Which upstream a line hit. The relay serves adsb.lol at the root and adsb.fi
// under /fi (bench measurement only -- licence-blocked, see relay/fi-bench.sh),
// so the path prefix is an exact upstream discriminator with no log-format change.
function upstreamOf(uri) {
  return uri.startsWith("/fi/") ? "adsb.fi" : "adsb.lol";
}

function classify(uri) {
  const u = uri.startsWith("/fi/") ? uri.slice(3) : uri;
  if (u.startsWith("/v2/hex") || u.startsWith("/v2/icao")) return "hex";
  if (u.startsWith("/v2/lat") || u.startsWith("/v3/lat")) return "pos";
  if (u.startsWith("/api/0/routeset")) return "route";
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
  rows.push({ t, cache: m[2], status: m[3], ustatus: m[4], cls: classify(m[6]), up: upstreamOf(m[6]) });
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

function report(cls, up) {
  const r = win.filter((x) => x.cls === cls && (!up || x.up === up));
  if (!r.length) return `  ${cls}: (none)`;
  const fetches = r.filter((x) => isUpstream(x.ustatus));
  const u429 = fetches.filter((x) => x.ustatus === "429").length;
  const cacheDist = {};
  for (const x of r) cacheDist[x.cache] = (cacheDist[x.cache] || 0) + 1;
  const cd = Object.entries(cacheDist).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k}:${v}`).join(" ");
  return [
    `  ${cls.toUpperCase()}  (${r.length} requests)`,
    `    upstream fetches : ${fetches.length}  (${perHour(fetches.length, spanSec)}/h)   <- what the upstream actually sees`,
    `    429 on fetches   : ${u429}  (${pct(u429, fetches.length)})`,
    `    X-Cache          : ${cd}`,
  ].join("\n");
}

// Longest degraded run for positions: consecutive /v2/lat requests served STALE because
// the upstream refresh failed (ustatus=429/5xx). Measured as the wall-clock span from the
// first to the last such request in the run (a run breaks on any fresh 200 or a >5min gap).
function longestDegraded(up) {
  const pos = win.filter((x) => x.cls === "pos" && (!up || x.up === up));
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

console.log(`\n=== relay measurement: ${win.length} requests over ${(spanSec / 3600).toFixed(1)}h ` +
  `(${new Date(win[0].t).toISOString()} .. ${new Date(newest).toISOString()}) ===`);

// Split the report per upstream when both are present (the adsb.fi bench runs
// alongside live adsb.lol traffic on the same box), so the two are directly
// comparable on identical relay tuning. One upstream -> the original flat report.
const upstreams = [...new Set(win.map((x) => x.up))].sort();
for (const up of upstreams) {
  const deg = longestDegraded(up);
  if (upstreams.length > 1) {
    const n = win.filter((x) => x.up === up).length;
    const tag = up === "adsb.fi" ? "  [bench only -- licence-blocked, not a serving path]" : "";
    console.log(`\n--------- upstream: ${up}  (${n} requests) ---------${tag}`);
  } else {
    console.log("");
  }
  console.log(report("pos", up));
  console.log(report("hex", up));
  console.log(report("route", up));
  console.log(`\n  positions longest DEGRADED run (stale, upstream failing): ${deg.sec}s` +
    (deg.from ? ` starting ${deg.from}` : " (none)"));
}

console.log(`\n  PASS targets: hex 429 near 0 + upstream req/HOUR (not /s); positions 429 < a few %;`);
console.log(`  X-Cache mostly HIT; longest degraded run bounded (dead-reckoning covers < ~60s).`);
if (upstreams.includes("adsb.fi")) {
  console.log(`  NB adsb.fi's public limit is 1 req/s PER IP and 4xx/429s count toward it --`);
  console.log(`  check its upstream fetch rate stays far under 3600/h per relay.\n`);
} else {
  console.log("");
}
