import { verifyAccess } from "./access";
import {
  BLIPS_ROUTES,
  PHOTO_ROUTES,
  RETENTION_HOURS,
  clampHours,
  deviceBoots,
  deviceFirmware,
  deviceOta,
  deviceSummary,
  enrichGaps,
  enrolledButSilent,
  firmwareSpread,
  firstRequestTimes,
  firstSeenTimes,
  fleetRows,
  fleetTotals,
  isDeviceId,
  latestBoots,
  otaNotOk,
  otaOutcomes,
  readLedger,
  seenDevices,
  upstreams,
  usageRows,
} from "./analytics";
import { readDrift } from "./drift";
import { FAKE_DEVICE_IDS } from "./fakeids.generated";
import { computeFunnel, sortUpstreams } from "./funnel";
import { OTA_TRIAGE_HOURS, computeTriage } from "./triage";
import { readRevoked, setRevoked } from "./revoke";
import {
  errorPage,
  firmwareBody,
  flashErr,
  flashOk,
  fleetBody,
  gapsBody,
  otaBody,
  deviceBody,
  funnelBody,
  page,
  triageBody,
  upstreamsBody,
  usageBody,
} from "./render";
import type { DeviceRow, Env } from "./types";

// Blipscope Cloud fleet dashboard.
//
// WHY THIS IS A SEPARATE WORKER from the one the devices talk to.
// The device Worker serves every board every few seconds and has, by design, no
// admin surface at all -- that was the whole shape of the revocation feature.
// Putting a fleet console on the same script would undo it: a bug in a rendering
// path, a dependency, or a query would sit in the same isolate as the thing
// keeping 50 screens alive, and its routes would exist on the hostname devices
// hit. Separate script, separate hostname, separate deploy. The device Worker is
// unchanged by anything in this directory except the telemetry it already
// writes.
//
// Everything here is read-only except one POST (/revoke), which writes the same
// aggregate KV entry the documented wrangler procedure writes.

const HTML = { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" };

// Render the post-redirect banner from a fixed code plus a device id, so nothing
// an attacker can put in a URL reaches the page as markup. The id is re-checked
// against the device-id shape here even though setRevoked already refused
// anything else -- this render path is reachable by editing the URL directly.
export function flashFor(code: string | null, dev: string): string {
  if (!code) return "";
  const id = /^[0-9a-f]{8,32}$/.test(dev.toLowerCase()) ? dev.toLowerCase() : "that device";
  switch (code) {
    case "revoked":
      return flashOk(`${id} is now REVOKED. Takes effect fleet-wide within ~60s.`);
    case "restored":
      return flashOk(`${id} is active again. Takes effect fleet-wide within ~60s.`);
    case "nochange":
      return flashOk(`${id} was already in that state -- nothing changed.`);
    case "bad":
      return flashErr(`That was not a device id (expected 8-32 hex characters). Nothing was changed.`);
    default:
      return "";
  }
}

// A dashboard is not a place to be clever about caching: it is read when
// something is wrong, and a stale answer then is worse than a slow one.
function html(body: string, status = 200): Response {
  return new Response(body, { status, headers: HTML });
}

async function withRevocation(rows: DeviceRow[], env: Env): Promise<DeviceRow[]> {
  const denied = await readRevoked(env);
  // Devices that are revoked but no longer sending anything still need a row --
  // otherwise "restore" is unreachable from the UI for exactly the devices that
  // most need it.
  const seen = new Set(rows.map((r) => r.dev));
  const extra: DeviceRow[] = [...denied]
    .filter((d) => !seen.has(d))
    .map((d) => ({
      dev: d, model: "?", fw: "?", requests: 0, errors: 0, cards: 0,
      enriches: 0, staleServed: 0, lastSeen: "", revoked: true,
    }));
  return [...rows.map((r) => ({ ...r, revoked: denied.has(r.dev) })), ...extra];
}

// Device display names, when the owner opted into the leaderboard. Best-effort:
// a KV hiccup costs a column, never the page.
async function withNames(rows: DeviceRow[], env: Env): Promise<DeviceRow[]> {
  try {
    const got = await Promise.all(
      rows.slice(0, 100).map((r) =>
        env.ENRICH_KV.get<{ name?: string }>(`lb:dev:${r.dev}`, "json").catch(() => null),
      ),
    );
    return rows.map((r, i) => (got[i]?.name ? { ...r, name: got[i]?.name } : r));
  } catch {
    return rows;
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    // Access first, before anything reads storage or spends an API call. Every
    // failure mode -- no token, bad signature, wrong audience, unconfigured
    // Worker -- lands here as a flat 403.
    const who = await verifyAccess(request, env);
    if (!who) {
      return new Response("forbidden", { status: 403, headers: { "Cache-Control": "no-store" } });
    }

    const hours = clampHours(url.searchParams.get("hours"));

    try {
      if (request.method === "POST" && url.pathname === "/revoke") {
        const form = await request.formData();
        const dev = String(form.get("dev") ?? "");
        const to = String(form.get("to") ?? "") === "1";
        const back = clampHours(String(form.get("hours") ?? "24"));
        const result = await setRevoked(env, dev, to);
        // Log who did what. This is the only mutating path in the product and
        // the audit trail should not depend on anyone remembering to look.
        console.log(
          JSON.stringify({ evt: "revoke", by: who.email, dev, to, ok: result.ok }),
        );
        // PRG so a refresh cannot replay the write. The redirect carries a CODE,
        // never markup: round-tripping HTML through a query parameter and
        // rendering it back would be reflected XSS on the one surface in this
        // product that can revoke a customer's device.
        const code = !result.ok ? "bad" : result.changed ? (to ? "revoked" : "restored") : "nochange";
        return new Response(null, {
          status: 303,
          headers: {
            Location: `/?hours=${back}&m=${code}&dev=${encodeURIComponent(dev.slice(0, 32))}`,
            "Cache-Control": "no-store",
          },
        });
      }

      if (url.pathname === "/") {
        const [rawRows, totals, triage] = await Promise.all([
          fleetRows(env, hours),
          fleetTotals(env, hours),
          // The triage list has its OWN stated windows (7 d, 30 d, retention),
          // independent of the table's. A triage failure is shown as one, in
          // place, rather than taking the fleet table down with it.
          (async () => {
            const [ledger, seen7d, seenRetention, boots, otaBad, drift] = await Promise.all([
              readLedger(env), seenDevices(env, 168), seenDevices(env, RETENTION_HOURS),
              latestBoots(env, 720), otaNotOk(env, OTA_TRIAGE_HOURS), readDrift(env),
            ]);
            return triageBody(computeTriage({ ledger, seen7d, seenRetention, latestBoots: boots, otaNotOk: otaBad, drift }));
          })().catch((err: unknown) => `<section class="err"><h2>Triage</h2>could not be computed: ${String(err instanceof Error ? err.message : err).replace(/[<>&"']/g, "")}</section>`),
        ]);
        const rows = await withNames(await withRevocation(rawRows, env), env);
        const flash = flashFor(
          url.searchParams.get("m"),
          (url.searchParams.get("dev") ?? "").slice(0, 32),
        );
        return html(
          page({
            title: "Fleet",
            email: who.email,
            hours,
            active: "/",
            body: fleetBody(rows, totals, hours, flash, triage),
          }),
        );
      }

      if (url.pathname === "/firmware") {
        return html(
          page({
            title: "Firmware",
            email: who.email,
            hours,
            active: "/firmware",
            body: firmwareBody(await firmwareSpread(env, hours)),
          }),
        );
      }

      if (url.pathname === "/ota") {
        return html(
          page({
            title: "OTA",
            email: who.email,
            hours,
            active: "/ota",
            body: otaBody(await otaOutcomes(env, hours)),
          }),
        );
      }

      if (url.pathname === "/gaps") {
        return html(
          page({
            title: "Enrichment gaps",
            email: who.email,
            hours,
            active: "/gaps",
            body: gapsBody(await enrichGaps(env, hours)),
          }),
        );
      }

      // One device. Every id anywhere in the dashboard links here. A malformed id,
      // or one with no ledger row and nothing reported in the retention window, is
      // a 404 -- never an empty page that looks like a device with no data.
      const devMatch = url.pathname.startsWith("/device/") ? [url.pathname, url.pathname.slice("/device/".length)] : null;
      if (devMatch) {
        const dev = decodeURIComponent(devMatch[1] ?? "").toLowerCase();
        if (!isDeviceId(dev)) return html(errorPage("No such device."), 404);
        const h = url.searchParams.has("hours") ? hours : 720;
        const [summary, firmware, boots, ota, usage, ledger, denied, lb] = await Promise.all([
          deviceSummary(env, dev, h), deviceFirmware(env, dev), deviceBoots(env, dev), deviceOta(env, dev),
          usageRows(env, h, dev), env.ENRICH_KV.get<{ firstAt?: string; lastAt?: string; enrollments?: number }>(`enr:dev:${dev}`, "json"),
          readRevoked(env), env.ENRICH_KV.get<{ name?: string }>(`lb:dev:${dev}`, "json").catch(() => null),
        ]);
        if (!summary && !ledger && boots.length === 0 && ota.length === 0 && usage.length === 0) {
          return html(errorPage("No such device: not enrolled, and nothing reported in 90 days."), 404);
        }
        return html(page({
          title: "Device", email: who.email, hours: h, active: "/",
          body: deviceBody({
            dev, name: lb?.name, hours: h, summary, revoked: denied.has(dev), firmware, boots, ota, usage: usage[0] ?? null,
            ledger: ledger ? { dev, firstEnrolled: ledger.firstAt ?? "", lastEnrolled: ledger.lastAt ?? "", enrollments: ledger.enrollments ?? 0 } : null,
          }),
        }));
      }

      // Setup funnel: enrolment -> first /blips -> first card (first photo fetch).
      if (url.pathname === "/funnel") {
        const [ledger, fb, fc, seen] = await Promise.all([readLedger(env), firstRequestTimes(env, BLIPS_ROUTES), firstRequestTimes(env, PHOTO_ROUTES), firstSeenTimes(env)]);
        const f = computeFunnel(ledger, fb, fc, Date.now() - RETENTION_HOURS * 3600000, seen, FAKE_DEVICE_IDS);
        return html(page({ title: "Setup funnel", email: who.email, hours, active: "/funnel", body: funnelBody(f) }));
      }

      // Fleet vs upstream: one line per upstream, worst first.
      if (url.pathname === "/upstreams") {
        const rows = sortUpstreams(await upstreams(env, hours));
        return html(page({ title: "Upstream health", email: who.email, hours, active: "/upstreams", body: upstreamsBody(rows, hours) }));
      }

      // Per-device usage counters (the hourly usage index) and the enrolled-but-
      // silent list. Defaults to 7 days, not the fleet page's 24 h: an hourly
      // report needs a longer window to say anything.
      if (url.pathname === "/usage" || url.pathname === "/usage.json") {
        const h = url.searchParams.has("hours") ? hours : 168;
        const [rows, silent] = await Promise.all([
          usageRows(env, h),
          seenDevices(env, h)
            .then((seen) => enrolledButSilent(env, seen))
            // The silent list can fail on its own (its cap, a KV list error)
            // without taking the usage table down with it -- and it says why.
            .catch((err: unknown) => ({ error: String(err instanceof Error ? err.message : err) })),
        ]);
        if (url.pathname === "/usage.json") {
          return new Response(JSON.stringify({ hours: h, rows, silent }, null, 2), {
            headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
          });
        }
        return html(
          page({ title: "Usage", email: who.email, hours: h, active: "/usage", body: usageBody(rows, silent, h) }),
        );
      }

      // A JSON mirror of the fleet table, for when you want to pipe it somewhere
      // rather than look at it.
      if (url.pathname === "/fleet.json") {
        const rows = await withRevocation(await fleetRows(env, hours), env);
        return new Response(JSON.stringify({ hours, rows }, null, 2), {
          headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
        });
      }

      return html(errorPage("No such page."), 404);
    } catch (err) {
      // The message can carry an upstream API body, so it is shown to an
      // authenticated operator only -- which, at this point in the handler, is
      // guaranteed.
      console.log(JSON.stringify({ evt: "error", by: who.email, err: String(err) }));
      return html(errorPage(String(err)), 500);
    }
  },
} satisfies ExportedHandler<Env>;
