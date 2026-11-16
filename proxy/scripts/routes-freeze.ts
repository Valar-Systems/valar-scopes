// routes-freeze.ts -- W4 (docs/route-weekly-code.md): the freeze, judged at the job's ACTUAL start. A scheduled time is
// not a start time (refresh-data's 06:17Z runs started 7-10 hours late), so "now" is read here, never the cron's time.
// The rule is G.freeze, the one home (FZ1-FZ4); this maps its verdict to what the workflow branches on:
//   exit 0 run, 4 skip (frozen, with a stated line), 1 fail (a malformed freeze never quietly runs or quietly skips).
import * as G from "./routes-guards";

const v = G.freeze(new Date().toISOString(), process.env.ROUTES_FREEZE_UNTIL);
console.log(`freeze: ${v.action === "skip" ? "SKIP" : v.action === "fail" ? "FAIL" : "run"} -- ${v.reason}`);
process.exit(v.action === "run" ? 0 : v.action === "skip" ? 4 : 1);
