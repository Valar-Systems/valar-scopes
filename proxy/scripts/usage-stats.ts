/**
 * usage-stats.ts -- feature adoption, as a table rather than raw SQL.
 *
 *   CLOUDFLARE_API_TOKEN=... npm run stats [-- --days 7 --env production]
 *
 * Prints two views of the same rows, because they answer different questions and
 * confusing them is easy:
 *
 *   FLEET      "cards were opened 340 times this week"
 *   ADOPTION   "11 of 14 reporting devices opened a card at all"
 *
 * The second is the one that decides what to build. A feature can have a large
 * total from three enthusiasts and be untouched by everyone else, and a totals
 * column cannot tell you which -- so devices-using sits next to every total.
 *
 * =============================================================================
 * WHAT "REPORTING DEVICES" MEANS, AND WHY IT IS NOT "DEVICES"
 *
 * The denominator here is devices that SENT a usage report in the window, not
 * devices that exist. A unit in a drawer sends nothing, so it is absent from
 * both columns rather than counted as a non-user -- which would be the more
 * flattering error and the wrong one.
 *
 * That is also why uptime is in the output. "Unused on a device that is on 12 h
 * a day" and "unused on a device in a drawer" are different facts, and the
 * ratio of report count to elapsed hours is what separates them.
 *
 * =============================================================================
 * COUNTS ARE A FLOOR, NOT A TOTAL
 *
 * The device commits a delta when it SENDS, so a report lost in flight takes its
 * events with it (include/UsageReport.h explains why that direction was chosen).
 * Devices on poor links therefore under-report, and the bias is not uniform. The
 * header says so on every run: these numbers are for comparing adoption, not for
 * quoting as exact activity.
 */
import { execSync } from "node:child_process";

const ACCOUNT = "48822e896bb10c45aa6bfe139bcff3d1"; // same as wrangler.toml account_id

const DATASET: Record<string, string> = {
  production: "blipscope_proxy",
  staging: "blipscope_proxy_staging",
};

interface Args {
  days: number;
  env: string;
}

function parseArgs(argv: string[]): Args {
  const a: Args = { days: 7, env: "production" };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--days") a.days = Number(argv[++i]) || 7;
    else if (argv[i] === "--env") a.env = argv[++i] ?? "production";
  }
  return a;
}

/**
 * The eight doubles, in the order recordUsage writes them. Keep in step with
 * include/UsageReport.h's Format() -- the firmware is the other side of this
 * contract, and a column read from the wrong index is wrong in a way that looks
 * entirely plausible (screen counts are all the same order of magnitude).
 */
const FEATURES = [
  { col: "double1", label: "detail card opened", key: "cardOpens" },
  { col: "double2", label: "screen: radar", key: "radar" },
  { col: "double3", label: "screen: list", key: "list" },
  { col: "double4", label: "screen: stats", key: "stats" },
  { col: "double5", label: "screen: follow", key: "follow" },
  { col: "double6", label: "logbook claim", key: "claims" },
] as const;

async function runSql<T>(sql: string, token: string): Promise<T[]> {
  const res = await fetch(
    `https://api.cloudflare.com/client/v4/accounts/${ACCOUNT}/analytics_engine/sql`,
    { method: "POST", headers: { Authorization: `Bearer ${token}` }, body: sql },
  );
  if (!res.ok) {
    // Whole body, not a slice of it: a filtered error message is how an auth
    // failure gets read as an empty dataset. See CLAUDE.md on filtering the
    // output of a command you are running to detect failure.
    throw new Error(`Analytics Engine returned ${res.status}: ${await res.text()}`);
  }
  const body = (await res.json()) as { data?: T[] };
  return body.data ?? [];
}

function token(): string {
  const t = process.env.CLOUDFLARE_API_TOKEN;
  if (t) return t;
  // Windows keeps these at the user level, where a running shell may not see
  // them -- see the memory note. Try the registry before giving up.
  try {
    const out = execSync(
      'powershell -NoProfile -Command "[Environment]::GetEnvironmentVariable(\'CLOUDFLARE_API_TOKEN\',\'User\')"',
      { encoding: "utf8" },
    ).trim();
    if (out) return out;
  } catch {
    /* fall through to the explicit failure below */
  }
  throw new Error("CLOUDFLARE_API_TOKEN is not set (env or user-level).");
}

function pad(s: string, n: number): string {
  return s.length >= n ? s : s + " ".repeat(n - s.length);
}
function padL(s: string, n: number): string {
  return s.length >= n ? s : " ".repeat(n - s.length) + s;
}

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));
  const ds = DATASET[args.env] ?? DATASET.production;
  const t = token();
  const where = `WHERE index1 = 'usage' AND timestamp > NOW() - INTERVAL '${args.days}' DAY`;

  // One row per feature: total events, and how many DISTINCT devices contributed
  // any. uniqExact over a filtered blob4 is what makes "devices using" mean
  // "devices that did it at least once" rather than "devices that reported".
  const selects = FEATURES.map(
    (f) =>
      `SUM(${f.col} * _sample_interval) AS ${f.key}_total, ` +
      `uniqExact(IF(${f.col} > 0, blob4, NULL)) AS ${f.key}_devices`,
  ).join(", ");

  const rows = await runSql<Record<string, string>>(
    `SELECT ${selects}, uniqExact(blob4) AS devices, COUNT() AS reports, ` +
      `MAX(double8) AS max_uptime_h, SUM(double7 * _sample_interval) AS follow_on_reports ` +
      `FROM ${ds} ${where}`,
    t,
  );

  const r = rows[0];
  if (!r) {
    console.log("No usage data in the window. That is not the same as no usage:");
    console.log("check that the fleet is running an image that reports (fw >= the");
    console.log("release that added X-Blip-Usage) before concluding anything.");
    return;
  }

  const devices = Number(r.devices ?? 0);
  const reports = Number(r.reports ?? 0);

  console.log("");
  console.log(`Feature adoption -- last ${args.days} day(s), ${args.env}`);
  console.log(`${devices} reporting device(s), ${reports} report(s).`);
  console.log("Counts are a FLOOR: a report lost in flight takes its events with it.");
  console.log("");
  console.log(`${pad("feature", 22)}${padL("events", 10)}${padL("devices", 10)}  adoption`);
  console.log("-".repeat(22 + 10 + 10 + 12));

  for (const f of FEATURES) {
    const total = Number(r[`${f.key}_total`] ?? 0);
    const using = Number(r[`${f.key}_devices`] ?? 0);
    const pct = devices > 0 ? Math.round((using / devices) * 100) : 0;
    console.log(
      `${pad(f.label, 22)}${padL(total.toLocaleString(), 10)}${padL(`${using}/${devices}`, 10)}` +
        `  ${padL(String(pct), 3)}%`,
    );
  }

  console.log("");
  // follow_on_reports counts REPORTS with the flag set, not devices -- a device
  // that reported 24 times with Follow on contributes 24. Stated rather than
  // presented as a device count, which is what it looks like at a glance.
  const followReports = Number(r.follow_on_reports ?? 0);
  console.log(
    `Follow configured on ${followReports} of ${reports} reports ` +
      `(${reports > 0 ? Math.round((followReports / reports) * 100) : 0}% of reporting time).`,
  );
  console.log(`Longest uptime seen: ${Number(r.max_uptime_h ?? 0)} h.`);
  console.log("");

  // Per-device, which was the ask: "this unit opens cards daily, that one never
  // has". Capped, because the fleet is small now and will not always be.
  const perDevice = await runSql<Record<string, string>>(
    `SELECT blob4 AS dev, blob3 AS fw, ` +
      FEATURES.map((f) => `SUM(${f.col} * _sample_interval) AS ${f.key}`).join(", ") +
      `, COUNT() AS reports, MAX(double8) AS uptime_h ` +
      `FROM ${ds} ${where} AND blob4 != '' ` +
      `GROUP BY dev, fw ORDER BY cardOpens DESC LIMIT 60`,
    t,
  );

  if (perDevice.length > 0) {
    console.log(
      `${pad("device", 20)}${pad("fw", 5)}${padL("cards", 7)}${padL("claims", 8)}` +
        `${padL("reports", 9)}${padL("uptime", 8)}`,
    );
    console.log("-".repeat(20 + 5 + 7 + 8 + 9 + 8));
    for (const d of perDevice) {
      console.log(
        pad(String(d.dev ?? "").slice(0, 18), 20) +
          pad(String(d.fw ?? "?"), 5) +
          padL(String(Number(d.cardOpens ?? 0)), 7) +
          padL(String(Number(d.claims ?? 0)), 8) +
          padL(String(Number(d.reports ?? 0)), 9) +
          padL(`${Number(d.uptime_h ?? 0)}h`, 8),
      );
    }
    console.log("");
  }
}

main().catch((e) => {
  console.error(String(e));
  process.exit(1);
});
