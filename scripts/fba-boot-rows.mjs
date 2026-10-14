// fba-boot-rows.mjs -- one device's boot rows at the Worker, for the fresh-boot acceptance.
//
//   node --experimental-strip-types scripts/fba-boot-rows.mjs <device-id> <from-epoch> <to-epoch>
//
// Prints one line per boot row whose time falls in [from, to]: "<epoch> <reason>", oldest first.
// The rows are the ones the device's X-Blip-Boot header writes (proxy/src/metrics.ts recordBoot:
// once per boot, on the first authenticated check-in), read through the DASHBOARD'S OWN query,
// deviceBoots() in dashboard/src/analytics.ts -- so this cannot drift from what the dashboard shows.
//
// WHY THE BOOT COUNT LIVES HERE AND NOT ON SERIAL (ruling, 2026-10-08): twice that day the serial
// capture's reopen after the step-5 power cut reset the board it was counting boots on. The Worker
// sees every boot that checks in and cannot cause one.
//
// TOKEN (Account Analytics: Read), never printed -- only whether one was found:
//   CF_API_TOKEN when non-empty, else the analytics-read line of BLIPSCOPE_TOKEN_FILE
//   (proxy/scripts/token-file.ts, the reader every by-hand script shares).
// The device id is never printed either; the caller passes it and gets times and reasons back.
//
// Exit 0: rows printed (possibly none). 2: bad arguments. 3: no token / the query failed --
// UNTRUSTWORTHY, nothing was measured, and the caller must not read "no rows" as "no boots".
import { deviceBoots } from "../dashboard/src/analytics.ts";
import { resolveToken } from "../proxy/scripts/token-file.ts";

const [dev, fromS, toS] = process.argv.slice(2);
const from = Number(fromS), to = Number(toS);
if (!/^[0-9a-f]{16}$/.test(dev ?? "") || !Number.isFinite(from) || !Number.isFinite(to) || to < from) {
  console.error("usage: fba-boot-rows.mjs <16-hex device id> <from-epoch> <to-epoch>");
  process.exit(2);
}

let token = "";
try {
  token = resolveToken("analytics-read", "CF_API_TOKEN").token;
} catch (e) {
  console.error(`UNTRUSTWORTHY: no analytics token -- ${e instanceof Error ? e.message : e}`);
  process.exit(3);
}
console.error(`token: ${token ? "present" : "absent"}`);

const env = { CF_ACCOUNT_ID: "48822e896bb10c45aa6bfe139bcff3d1", CF_API_TOKEN: token, AE_DATASET: "blipscope_proxy" };
let rows;
try {
  rows = await deviceBoots(env, dev);
} catch (e) {
  console.error(`UNTRUSTWORTHY: the boot query failed -- ${String(e instanceof Error ? e.message : e).slice(0, 200)}`);
  process.exit(3);
}
// AE returns "YYYY-MM-DD HH:MM:SS" in UTC with no zone marker.
const epoch = (at) => Math.floor(Date.parse(String(at).replace(" ", "T") + "Z") / 1000);
for (const r of rows.map((r) => ({ t: epoch(r.at), reason: r.reason })).filter((r) => r.t >= from && r.t <= to).sort((a, b) => a.t - b.t)) {
  console.log(`${r.t} ${r.reason}`);
}
