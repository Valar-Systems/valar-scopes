import type { DeviceRow } from "./types";
import type { FleetTotals, FwRow, OtaRow } from "./analytics";

// The page. Server-rendered, no client framework, no external assets -- this is
// an ops tool that must work when something else is broken.

const CSS = `
:root{--bg:#0b1120;--panel:#111827;--line:#1f3d2b;--ink:#22c55e;--dim:#15803d;
      --text:#d1fae5;--warn:#fbbf24;--bad:#f87171;--mute:#6b7280}
*{box-sizing:border-box}
body{margin:0;padding:1.5rem;background:var(--bg);color:var(--text);
     font:14px/1.5 ui-monospace,Menlo,Consolas,monospace}
.wrap{max-width:76rem;margin:0 auto;display:flex;flex-direction:column;gap:1.5rem}
h1{font-size:1.1rem;margin:0;color:var(--ink);letter-spacing:.06em;text-transform:uppercase}
h2{font-size:.85rem;margin:0 0 .6rem;color:var(--dim);letter-spacing:.08em;text-transform:uppercase}
header{display:flex;flex-wrap:wrap;gap:1rem;align-items:baseline;justify-content:space-between;
       border-bottom:1px solid var(--line);padding-bottom:.75rem}
.who{color:var(--mute);font-size:.8rem}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(9rem,1fr));gap:.75rem}
.card{background:var(--panel);border:1px solid var(--line);padding:.75rem 1rem}
.card .n{font-size:1.6rem;color:var(--ink);font-variant-numeric:tabular-nums;line-height:1.1}
.card .l{font-size:.72rem;color:var(--mute);text-transform:uppercase;letter-spacing:.06em}
.card .s{font-size:.72rem;color:var(--dim)}
section{background:var(--panel);border:1px solid var(--line);padding:1rem}
.scroll{overflow-x:auto}
table{border-collapse:collapse;width:100%;font-size:.82rem}
th{text-align:left;color:var(--mute);font-weight:400;text-transform:uppercase;
   font-size:.7rem;letter-spacing:.06em;padding:.3rem .6rem;border-bottom:1px solid var(--line);white-space:nowrap}
td{padding:.35rem .6rem;border-bottom:1px solid rgba(31,61,43,.5);white-space:nowrap}
td.n{text-align:right;font-variant-numeric:tabular-nums}
tr:last-child td{border-bottom:none}
tr.rev td{opacity:.55}
.pill{display:inline-block;padding:.05rem .45rem;border:1px solid currentColor;
      font-size:.7rem;letter-spacing:.04em;text-transform:uppercase}
.ok{color:var(--ink)}.warn{color:var(--warn)}.bad{color:var(--bad)}.mute{color:var(--mute)}
button{font:inherit;font-size:.75rem;padding:.15rem .6rem;cursor:pointer;
       background:transparent;border:1px solid currentColor}
button.revoke{color:var(--bad)}button.restore{color:var(--ink)}
.note{color:var(--mute);font-size:.78rem;margin:.6rem 0 0;max-width:56rem}
.note b{color:var(--dim);font-weight:400}
nav{display:flex;gap:.75rem;font-size:.78rem}
nav a{color:var(--dim);text-decoration:none;border-bottom:1px solid transparent}
nav a:hover,nav a[aria-current]{color:var(--ink);border-bottom-color:var(--ink)}
.err{border-color:var(--bad);color:var(--bad)}
`;

const esc = (s: unknown): string =>
  String(s ?? "").replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c] as string,
  );

const n = (v: number): string => v.toLocaleString("en-US");

function ago(iso: string): string {
  const t = Date.parse(iso.endsWith("Z") || iso.includes("+") ? iso : `${iso}Z`);
  if (!Number.isFinite(t)) return "?";
  const s = Math.max(0, Math.floor((Date.now() - t) / 1000));
  if (s < 90) return `${s}s ago`;
  if (s < 5400) return `${Math.round(s / 60)}m ago`;
  if (s < 172800) return `${Math.round(s / 3600)}h ago`;
  return `${Math.round(s / 86400)}d ago`;
}

// A device that stopped talking is the single most useful thing on the page, so
// staleness is encoded in FORM (a pill) as well as text.
function seenPill(iso: string): string {
  const t = Date.parse(iso.endsWith("Z") || iso.includes("+") ? iso : `${iso}Z`);
  const mins = Number.isFinite(t) ? (Date.now() - t) / 60000 : 1e9;
  const cls = mins < 15 ? "ok" : mins < 120 ? "warn" : "bad";
  return `<span class="pill ${cls}">${esc(ago(iso))}</span>`;
}

export function page(opts: {
  title: string;
  email: string;
  hours: number;
  body: string;
  active: string;
}): string {
  const tabs = [
    ["/", "Fleet"],
    ["/firmware", "Firmware"],
    ["/ota", "OTA"],
    ["/gaps", "Enrichment gaps"],
  ];
  return `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(opts.title)}</title><style>${CSS}</style></head><body><div class="wrap">
<header>
  <div>
    <h1>Blipscope Cloud &mdash; ${esc(opts.title)}</h1>
    <nav>${tabs
      .map(
        ([href, label]) =>
          `<a href="${href}?hours=${opts.hours}"${opts.active === href ? ' aria-current="page"' : ""}>${esc(label)}</a>`,
      )
      .join("")}</nav>
  </div>
  <div class="who">${esc(opts.email)} &middot; last ${opts.hours}h &middot;
    ${[6, 24, 72, 168].map((h) => `<a href="${esc(opts.active)}?hours=${h}" style="color:var(--dim)">${h}h</a>`).join(" ")}
  </div>
</header>
${opts.body}
</div></body></html>`;
}

export function errorPage(message: string): string {
  return page({
    title: "Error",
    email: "",
    hours: 24,
    active: "/",
    body: `<section class="err"><h2>Something failed</h2><pre>${esc(message)}</pre></section>`,
  });
}

export function fleetBody(
  rows: DeviceRow[],
  totals: FleetTotals,
  hours: number,
  flash: string,
): string {
  const errPct = totals.requests ? (totals.errors / totals.requests) * 100 : 0;
  const revoked = rows.filter((r) => r.revoked).length;

  const cards = `
  <div class="cards">
    <div class="card"><div class="n">${n(totals.devices)}</div><div class="l">Devices seen</div>
      <div class="s">${revoked ? `${revoked} revoked` : "none revoked"}</div></div>
    <div class="card"><div class="n">${n(totals.requests)}</div><div class="l">Requests</div>
      <div class="s">${n(Math.round(totals.requests / Math.max(1, hours)))}/h</div></div>
    <div class="card"><div class="n ${errPct > 2 ? "bad" : ""}">${errPct.toFixed(2)}%</div><div class="l">Error rate</div>
      <div class="s">${n(totals.errors)} responses &ge;400</div></div>
    <div class="card"><div class="n">${n(totals.cards)}</div><div class="l">Cards opened</div>
      <div class="s">taps, fleet-wide</div></div>
    <div class="card"><div class="n">${n(totals.unattributed)}</div><div class="l">Unattributed</div>
      <div class="s">shared-key requests</div></div>
  </div>`;

  const body = rows.length
    ? rows
        .map((r) => {
          const errPctRow = r.requests ? (r.errors / r.requests) * 100 : 0;
          return `<tr class="${r.revoked ? "rev" : ""}">
        <td><code>${esc(r.dev)}</code>${r.name ? ` <span class="mute">${esc(r.name)}</span>` : ""}</td>
        <td>${esc(r.model)}</td>
        <td class="n">${esc(r.fw)}</td>
        <td>${seenPill(r.lastSeen)}</td>
        <td class="n">${n(r.requests)}</td>
        <td class="n ${errPctRow > 2 ? "bad" : ""}">${errPctRow.toFixed(1)}%</td>
        <td class="n ${r.cards ? "ok" : "mute"}">${n(r.cards)}</td>
        <td class="n">${n(r.enriches)}</td>
        <td class="n ${r.staleServed ? "warn" : "mute"}">${n(r.staleServed)}</td>
        <td>${
          r.revoked
            ? `<span class="pill bad">revoked</span>`
            : `<span class="pill ok">active</span>`
        }</td>
        <td><form method="POST" action="/revoke" style="margin:0">
              <input type="hidden" name="dev" value="${esc(r.dev)}">
              <input type="hidden" name="to" value="${r.revoked ? "0" : "1"}">
              <input type="hidden" name="hours" value="${hours}">
              <button class="${r.revoked ? "restore" : "revoke"}"
                ${r.revoked ? "" : `onclick="return confirm('Revoke ${esc(r.dev)}? It will start getting 401s within ~60s and its screen will empty out.')"`}>
                ${r.revoked ? "Restore" : "Revoke"}</button>
            </form></td>
      </tr>`;
        })
        .join("")
    : `<tr><td colspan="11" class="mute">No device-attributed requests in this window.
         If the fleet is live, the Worker predates the per-device metrics dimension &mdash; redeploy it.</td></tr>`;

  return `
  ${flash}
  ${cards}
  <section>
    <h2>Devices</h2>
    <div class="scroll"><table>
      <thead><tr>
        <th>Device</th><th>Model</th><th>FW</th><th>Last seen</th>
        <th class="n">Requests</th><th class="n">Errors</th><th class="n">Cards</th>
        <th class="n">Enrich</th><th class="n">Stale</th><th>State</th><th></th>
      </tr></thead>
      <tbody>${body}</tbody>
    </table></div>
    <p class="note">
      <b>Requests</b> measures uptime, not attention &mdash; a device polls on a timer whether or not
      anyone is in the room. <b>Cards</b> is the honest interaction number: the firmware fetches a
      photo exactly once per aircraft when a detail card is opened, and that only happens on a tap.
      <b>Enrich</b> is background work the device does on its own. <b>Stale</b> counts responses served
      from cache past their freshness window &mdash; normal in small numbers, a signal about upstream
      health in large ones. Counts are weight-corrected for the 1:10 cache-hit sampling.
    </p>
  </section>`;
}

export function firmwareBody(rows: FwRow[]): string {
  const byModel = new Map<string, FwRow[]>();
  for (const r of rows) {
    if (!byModel.has(r.model)) byModel.set(r.model, []);
    (byModel.get(r.model) as FwRow[]).push(r);
  }
  const blocks = [...byModel.entries()]
    .map(([model, list]) => {
      const total = list.reduce((a, b) => a + b.devices, 0);
      const bars = list
        .sort((a, b) => Number(b.fw) - Number(a.fw))
        .map((r) => {
          const pct = total ? (r.devices / total) * 100 : 0;
          return `<tr><td>v${esc(r.fw)}</td><td class="n">${n(r.devices)}</td>
            <td style="width:60%"><span style="display:inline-block;height:.7rem;width:${pct.toFixed(1)}%;
              background:var(--ink);min-width:1px"></span>
              <span class="mute"> ${pct.toFixed(0)}%</span></td></tr>`;
        })
        .join("");
      return `<h2>${esc(model)} &mdash; ${n(total)} devices</h2>
        <div class="scroll"><table><tbody>${bars}</tbody></table></div>`;
    })
    .join("");
  return `<section>${blocks || '<p class="mute">No firmware data in this window.</p>'}
    <p class="note">This is the rollout view: whether an OTA actually landed, without plugging
      anything in. A model stuck on an old version usually means its release asset is missing
      &mdash; a device only ever downloads its own slug's binary.</p></section>`;
}

export function otaBody(rows: OtaRow[]): string {
  const cls = (r: string) => (r === "ok" ? "ok" : r.startsWith("fail") ? "bad" : "warn");
  const body = rows.length
    ? rows
        .map(
          (r) => `<tr>
      <td>${seenPill(r.when)}</td>
      <td><code>${esc(r.dev)}</code></td>
      <td>${esc(r.model)}</td>
      <td>v${r.fwFrom} &rarr; v${r.fwTo}</td>
      <td><span class="pill ${cls(r.result)}">${esc(r.result)}</span></td>
    </tr>`,
        )
        .join("")
    : `<tr><td colspan="5" class="mute">No OTA attempts reported in this window.</td></tr>`;
  return `<section><h2>OTA attempts</h2>
    <div class="scroll"><table>
      <thead><tr><th>When</th><th>Device</th><th>Model</th><th>Version</th><th>Result</th></tr></thead>
      <tbody>${body}</tbody></table></div>
    <p class="note">Reported by the device on its first check-in after an update attempt. A
      <b>fail-*</b> row names the exact unit to look at &mdash; devices update from a fragmented
      heap, which the bench can't reproduce honestly.</p></section>`;
}

export function gapsBody(rows: { gap: string; type: string; lookups: number }[]): string {
  const label: Record<string, string> = {
    type: "no ICAO type resolved",
    name: "no friendly name (add TYPE_NAMES or a tn: key)",
    photo: "no stock photo (run suggest/harvest/ingest)",
  };
  const body = rows.length
    ? rows
        .map(
          (r) => `<tr><td><span class="pill ${r.gap === "photo" ? "warn" : "bad"}">${esc(r.gap)}</span></td>
      <td><code>${esc(r.type)}</code></td><td class="n">${n(r.lookups)}</td>
      <td class="mute">${esc(label[r.gap] ?? "")}</td></tr>`,
        )
        .join("")
    : `<tr><td colspan="4" class="mute">No enrichment gaps in this window.</td></tr>`;
  return `<section><h2>What the fleet looked up and we couldn't answer</h2>
    <div class="scroll"><table>
      <thead><tr><th>Gap</th><th>Type</th><th class="n">Lookups</th><th>Fix</th></tr></thead>
      <tbody>${body}</tbody></table></div>
    <p class="note">Ranked by real fleet demand rather than guesswork. <b>name</b> gaps are the
      cheapest to close and need no deploy &mdash; a <code>tn:&lt;CODE&gt;</code> KV key is enough.</p>
  </section>`;
}

export function flashOk(msg: string): string {
  return `<section style="border-color:var(--ink)"><span class="pill ok">done</span> ${esc(msg)}</section>`;
}
export function flashErr(msg: string): string {
  return `<section class="err"><span class="pill bad">refused</span> ${esc(msg)}</section>`;
}
