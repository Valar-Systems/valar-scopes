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

// src= is OPTIONAL in this pattern on purpose. It is present in the shipping
// log_format, but a stricter regex would silently DROP any line lacking it --
// turning a format change into quietly smaller totals rather than an error, in
// the one tool whose entire job is reporting totals accurately.
const LINE = /^(\S+) cache=(\S+) status=(\d+) ustatus=(\S+) rt=(\S+) (?:src=(\S+) )?uri="([^"]*)"/;

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString("utf8");
}

// Which upstream a line hit. The relay serves adsb.lol at the root and adsb.fi
// under /fi, so the path prefix is an exact upstream discriminator with no
// log-format change. BOTH are serving paths: since 2026-07-31 adsb.fi is the
// chain PRIMARY and adsb.lol the licensed fallback (see upstreams/chain.ts).
function upstreamOf(uri) {
  return uri.startsWith("/fi/") ? "adsb.fi" : "adsb.lol";
}

// WHO made the request, which is NOT the same question as which upstream it hit.
//
// fi-bench.sh polls the relay from the box itself (`--resolve localhost:443:
// 127.0.0.1`), so its traffic is indistinguishable from the Worker's by URI --
// same prefix, same tile, same cadence -- and it consumes the SAME per-IP budget
// upstream. Before this split the report added the two together and called the
// total "what the upstream actually sees", which is true but useless for the one
// thing the number is used for: telling an upstream operator what OUR PRODUCT
// costs them. A synthetic poller inflating that figure would have been invisible.
//
// remote_addr separates them exactly: loopback = on-box bench, anything else =
// the Worker's Cloudflare egress.
const isOnBox = (src) => src === "127.0.0.1" || src === "::1" || src === "-";

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
  rows.push({
    t, cache: m[2], status: m[3], ustatus: m[4],
    cls: classify(m[7]), up: upstreamOf(m[7]), onBox: isOnBox(m[6]),
  });
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
  const bench = fetches.filter((x) => x.onBox).length;
  const lines = [
    `  ${cls.toUpperCase()}  (${r.length} requests)`,
    `    upstream fetches : ${fetches.length}  (${perHour(fetches.length, spanSec)}/h)   <- what the upstream actually sees`,
  ];
  // Only printed when there IS on-box traffic, so a clean run stays terse -- but
  // when it is not clean the breakdown is unmissable, because the whole-figure
  // line above is the one that gets quoted to an upstream operator.
  if (bench) {
    lines.push(
      `      of which BENCH : ${bench}  (${perHour(bench, spanSec)}/h)  <- fi-bench.sh on this box, NOT the product`,
      `      of which WORKER: ${fetches.length - bench}  (${perHour(fetches.length - bench, spanSec)}/h)  <- quote THIS one`,
    );
  }
  lines.push(
    `    429 on fetches   : ${u429}  (${pct(u429, fetches.length)})`,
    `    X-Cache          : ${cd}`,
  );
  return lines.join("\n");
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

// Said once, loudly, at the top -- not only inside whichever workload happens to
// carry it. fi-bench.sh predates adsb.fi becoming the chain primary; with the
// real thing now serving through the same path, leaving the poller running just
// spends the per-IP budget twice and taints the figure we report upstream.
const benchTotal = win.filter((x) => x.onBox).length;
if (benchTotal) {
  console.log(`\n  !! ${benchTotal} of these requests came from ON THIS BOX (fi-bench.sh), not from the`);
  console.log(`     Worker. adsb.fi is the chain PRIMARY now, so the bench poller is redundant load on`);
  console.log(`     the same 1 req/s per-IP budget:  systemctl disable --now blipscope-fi-bench`);
}

// Split the report per upstream when both are present, so the two are directly
// comparable on identical relay tuning. One upstream -> the original flat report.
const upstreams = [...new Set(win.map((x) => x.up))].sort();
for (const up of upstreams) {
  const deg = longestDegraded(up);
  if (upstreams.length > 1) {
    const n = win.filter((x) => x.up === up).length;
    // Both are serving paths. adsb.fi leads on written commercial permission
    // (2026-08-05, conditional on the rate limit and nothing else) plus ~19x the
    // rate headroom; adsb.lol stays as the ODbL-licensed fallback and is the
    // only source carrying routes. See ROADMAP "adsb.fi -- PRIMARY" for why this
    // said the opposite twice, and upstreams/chain.ts for the order itself.
    const tag = up === "adsb.fi"
      ? "  [chain PRIMARY -- positions + hex]"
      : "  [licensed fallback + the only route source]";
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
