#!/usr/bin/env node
// "USE MY LOCATION" END TO END, on the device's REAL settings page (review point 3, 2026-10-06).
//
// check-locate-page.mjs proves the HELPER never sends the position anywhere. This proves
// what the SETTINGS PAGE does with every way the helper can end: it saves exactly once on
// success, and on every other ending it saves NOTHING, never produces 0,0, and shows a
// plain sentence plus the gps-coordinates.org paste fallback.
//
// THE INPUT IS THE FIRMWARE'S OWN PAGE. The page is fetched from a real device (--device)
// and served to headless Chrome under http://blipscope-test.local/, so the script under
// test is byte-for-byte what the board serves -- not a copy typed into a test. The helper
// is built from src/locatepage.ts. Every POST the page makes is INTERCEPTED and answered
// here: the device's stored location is never touched by this check, and nothing is ever
// printed from the boxes but "unchanged: yes/no".
//
// Cases (the frozen table in the PR): S success, D denied, T timeout, U unavailable,
// B popup blocked, C closed without choosing, Z helper sends 0,0, G helper sends garbage.
//
// SABOTAGE (--sabotage <name>) rewrites the device's page in flight, so the check can be
// shown red against the real page:
//   no-fallback  failures no longer reveal the paste link       -> every failure case fails
//   save-zero    the 0,0 guard is removed                        -> Z saves 0,0 and fails
// A sabotage whose anchor is not found exactly once is BLIND, never a silent pass.
//
// RUN:  node scripts/check-locate-flow.mjs --device http://blipscope-xxxxxx.local [--sabotage <name>]
// EXIT: 0 every case as predicted   1 a case differed   2 blind (page/rig did not work)

import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { build } from "esbuild";

const SENTINEL = { latitude: 11.1111111, longitude: -22.2222222, accuracy: 15 };   // FAKE -- not a place
const PAGE = "http://blipscope-test.local";
const HELPER_ORIGIN = "https://scopes.valarsystems.com";
const CHROME = [
  process.env.CHROME,
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
  "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
].filter(Boolean).find((p) => existsSync(p));
const devAt = process.argv.indexOf("--device");
const DEVICE = devAt > 0 ? (process.argv[devAt + 1] || "").replace(/\/+$/, "") : "";
if (!DEVICE) { console.log("usage: node scripts/check-locate-flow.mjs --device http://<device>"); process.exit(2); }
const SABOTAGES = {
  "no-fallback": ["bpSay(t,false);bpFb.style.display='block'}", "bpSay(t,false)}"],
  "save-zero": ["||(Math.abs(la)<0.00005&&Math.abs(lo)<0.00005)", ""],
};
const sabAt = process.argv.indexOf("--sabotage");
const SABOTAGE = sabAt > 0 ? SABOTAGES[process.argv[sabAt + 1]] : null;
if (sabAt > 0 && !SABOTAGE) { console.log(`unknown sabotage; one of: ${Object.keys(SABOTAGES).join(", ")}`); process.exit(2); }
let sabotageApplied = false;
if (!CHROME) { console.log("BLIND: no Chrome/Chromium found (set CHROME=...)"); process.exit(2); }

const out = await build({ entryPoints: ["src/locatepage.ts"], bundle: true, format: "esm", platform: "neutral", write: false, logLevel: "silent" });
const mod = await import("data:text/javascript;base64," + Buffer.from(out.outputFiles[0].text).toString("base64"));
const b64 = (s) => Buffer.from(s).toString("base64");
const sha = async (s) => Buffer.from(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s))).toString("base64");

async function helperResponse(replace) {
  const real = await mod.locateResponse();
  let html = await real.text();
  const headers = {};
  real.headers.forEach((v, k) => { headers[k] = v; });
  delete headers["content-length"];
  if (replace) {
    if (html.split(replace[0]).length !== 2) throw new Error(`replace anchor not found once: ${replace[0]}`);
    html = html.replace(replace[0], replace[1]);
    const script = html.match(/<script>([\s\S]*)<\/script>/)[1];
    const style = html.match(/<style>([\s\S]*)<\/style>/)[1];
    headers["content-security-policy"] = `default-src 'none'; script-src 'sha256-${await sha(script)}'; ` +
      `style-src 'sha256-${await sha(style)}'; form-action 'none'; base-uri 'none'; frame-ancestors 'none'`;
  }
  return { html, headers };
}

// The helper is stubbed ONLY for the timeout case, and the stub honours the timeout the
// helper ASKED for -- so the elapsed time measures the helper's own request, and the
// requested value is recorded for the report.
const TIMEOUT_STUB = `(function(){Object.defineProperty(navigator,'geolocation',{configurable:true,value:{
getCurrentPosition:function(ok,err,o){window.__geoOpts=o||null;setTimeout(function(){err({code:3,message:'Timeout expired'})},(o&&o.timeout)||0)},
watchPosition:function(){return 0},clearWatch:function(){}}})})();`;

const CASES = [
  { key: "S", name: "success", geo: SENTINEL, want: { msg: "Saved your location", fallback: false, posts: 1, boxes: "sentinel" } },
  { key: "D", name: "permission denied", deny: true, want: { msg: mod.LOCATE_COPY.denied, fallback: true, posts: 0, boxes: "unchanged" } },
  { key: "T", name: "timeout", stub: TIMEOUT_STUB, want: { msg: mod.LOCATE_COPY.timeout, fallback: true, posts: 0, boxes: "unchanged", waitMs: 16000 } },
  { key: "U", name: "position unavailable", geo: {}, want: { msg: mod.LOCATE_COPY.unavailable, fallback: true, posts: 0, boxes: "unchanged" } },
  { key: "B", name: "popup blocked", noGesture: true, want: { msg: "blocked the location window", fallback: true, posts: 0, boxes: "unchanged" } },
  { key: "C", name: "closed without choosing", geo: SENTINEL, closePopup: true, want: { msg: "closed before it shared", fallback: true, posts: 0, boxes: "unchanged" } },
  { key: "Z", name: "helper sends 0,0", geo: { latitude: 0, longitude: 0, accuracy: 10 }, want: { msg: "not usable", fallback: true, posts: 0, boxes: "unchanged" } },
  { key: "G", name: "helper sends garbage", geo: SENTINEL, replace: ["lat:formatCoord(c.latitude)", "lat:undefined"],
    want: { msg: "not usable", fallback: true, posts: 0, boxes: "unchanged" } },
];

async function runCase(c) {
  const profile = mkdtempSync(join(tmpdir(), "locflow-"));
  // NO --disable-popup-blocking: case B needs Chrome's real blocker. Every other case
  // clicks with a user gesture, which the blocker allows -- as a tap on a phone does.
  const chrome = spawn(CHROME, ["--headless=new", "--remote-debugging-port=0", `--user-data-dir=${profile}`,
    "--no-first-run", "--no-default-browser-check", "--disable-extensions",
    "--disable-background-networking", "--disable-component-update", "about:blank"], { stdio: "ignore" });
  try {
    let port = null;
    for (let i = 0; i < 100 && !port; i++) {
      const f = join(profile, "DevToolsActivePort");
      if (existsSync(f)) port = readFileSync(f, "utf8").split("\n")[0].trim();
      else await new Promise((r) => setTimeout(r, 100));
    }
    if (!port) return { blind: "Chrome never opened its debugging port" };
    const ver = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
    const ws = new WebSocket(ver.webSocketDebuggerUrl);
    await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
    let id = 0;
    const pending = new Map();
    const handlers = [];
    ws.onmessage = (m) => {
      const msg = JSON.parse(m.data);
      if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); return; }
      for (const h of handlers) h(msg);
    };
    const send = (method, params = {}, sessionId) => new Promise((r) => {
      const mid = ++id; pending.set(mid, r);
      ws.send(JSON.stringify({ id: mid, method, params, ...(sessionId ? { sessionId } : {}) }));
    });
    const evalIn = async (sid, expression, userGesture = false) =>
      (await send("Runtime.evaluate", { expression, returnByValue: true, userGesture }, sid)).result?.result?.value;

    const sessions = {}, openers = {};
    const posts = [], others = [];
    let pageServed = false;
    const helper = await helperResponse(c.replace);

    handlers.push(async (msg) => {
      const sid = msg.sessionId;
      if (msg.method === "Target.attachedToTarget") {
        const s = msg.params.sessionId;
        sessions[msg.params.targetInfo.targetId] = s;
        openers[msg.params.targetInfo.targetId] = msg.params.targetInfo.openerId || null;
        await send("Fetch.enable", { patterns: [{ urlPattern: "*" }] }, s);
        await send("Page.enable", {}, s);
        if (c.geo) await send("Emulation.setGeolocationOverride", c.geo, s);
        if (c.stub && msg.params.targetInfo.openerId)
          await send("Page.addScriptToEvaluateOnNewDocument", { source: c.stub }, s);
        await send("Runtime.runIfWaitingForDebugger", {}, s);
      } else if (msg.method === "Fetch.requestPaused") {
        const r = msg.params.request, rid = msg.params.requestId;
        const u = new URL(r.url);
        if (u.origin === PAGE && r.method === "GET") {
          // The device's own page, proxied byte for byte.
          try {
            const resp = await fetch(DEVICE + u.pathname + u.search);
            let buf = Buffer.from(await resp.arrayBuffer());
            if (SABOTAGE && u.pathname === "/") {
              const html = buf.toString("utf8");
              if (html.split(SABOTAGE[0]).length !== 2) throw new Error("sabotage anchor not found exactly once");
              buf = Buffer.from(html.replace(SABOTAGE[0], SABOTAGE[1]), "utf8");
              sabotageApplied = true;
            }
            const hdrs = [];
            resp.headers.forEach((v, k) => { if (!/^(content-length|content-encoding|transfer-encoding|connection)$/i.test(k)) hdrs.push({ name: k, value: v }); });
            if (u.pathname === "/") pageServed = resp.status === 200;
            await send("Fetch.fulfillRequest", { requestId: rid, responseCode: resp.status, responseHeaders: hdrs, body: buf.toString("base64") }, sid);
          } catch {
            await send("Fetch.failRequest", { requestId: rid, errorReason: "ConnectionFailed" }, sid);
          }
        } else if (u.origin === PAGE && r.method === "POST" && u.pathname === "/location") {
          // Answered HERE -- the device's stored location is never written by this check.
          const body = r.postData || "";
          posts.push(body);
          const q = new URLSearchParams(body);
          await send("Fetch.fulfillRequest", { requestId: rid, responseCode: 200,
            responseHeaders: [{ name: "Content-Type", value: "application/json" }],
            body: b64(JSON.stringify({ ok: true, lat: q.get("lat"), lon: q.get("lon") })) }, sid);
        } else if (r.url.startsWith(`${HELPER_ORIGIN}/blipscope/locate?`)) {
          await send("Fetch.fulfillRequest", { requestId: rid, responseCode: 200,
            responseHeaders: Object.entries(helper.headers).map(([name, value]) => ({ name, value })), body: b64(helper.html) }, sid);
        } else {
          if (u.origin === PAGE) others.push(`${r.method} ${u.pathname}`);
          await send("Fetch.failRequest", { requestId: rid, errorReason: "BlockedByClient" }, sid);
        }
      }
    });

    if (c.deny) await send("Browser.setPermission", { permission: { name: "geolocation" }, setting: "denied", origin: HELPER_ORIGIN });
    else await send("Browser.grantPermissions", { permissions: ["geolocation"], origin: HELPER_ORIGIN });
    await send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: true, flatten: true });
    const { result: { targetId: pageId } } = await send("Target.createTarget", { url: "about:blank" });
    for (let i = 0; i < 50 && !sessions[pageId]; i++) await new Promise((r) => setTimeout(r, 50));
    const pageSid = sessions[pageId];
    if (!pageSid) return { blind: "the page tab never attached" };
    await send("Page.navigate", { url: `${PAGE}/#location` }, pageSid);

    let ready = false;
    for (let i = 0; i < 100 && !ready; i++) {
      ready = await evalIn(pageSid, "!!document.getElementById('bplocate')") === true;
      if (!ready) await new Promise((r) => setTimeout(r, 100));
    }
    if (!ready) return { blind: pageServed ? "the page has no Use-my-location button" : "the device page did not load" };
    const BOXES = "JSON.stringify([document.querySelector('[name=latitude]').value,document.querySelector('[name=longitude]').value])";
    const before = await evalIn(pageSid, BOXES);

    await evalIn(pageSid, "document.getElementById('bplocate').click()", !c.noGesture);
    let popupSid = null, popupId = null;
    for (let i = 0; i < 30 && !popupSid && !c.noGesture; i++) {
      popupId = Object.keys(openers).find((t) => openers[t] === pageId);
      popupSid = popupId ? sessions[popupId] : null;
      if (!popupSid) await new Promise((r) => setTimeout(r, 100));
    }
    if (c.noGesture) {
      await new Promise((r) => setTimeout(r, 800));
      popupId = Object.keys(openers).find((t) => openers[t] === pageId);
      if (popupId) return { blind: "the popup opened without a gesture -- the blocker is off, case B cannot be judged" };
    } else if (!popupSid) return { blind: "the helper popup never opened" };

    let shareAt = Date.now(), geoTimeout = null;
    if (c.closePopup) {
      for (let i = 0; i < 50; i++) {                    // the helper page loaded, nothing chosen
        if (await evalIn(popupSid, "!!document.getElementById('go')") === true) break;
        await new Promise((r) => setTimeout(r, 100));
      }
      shareAt = Date.now();
      await send("Target.closeTarget", { targetId: popupId });
    } else if (popupSid) {
      let clicked = false;
      for (let i = 0; i < 50 && !clicked; i++) {
        clicked = await evalIn(popupSid, "(function(){var b=document.getElementById('go');if(!b||b.disabled)return false;b.click();return true;})()") === true;
        if (!clicked) await new Promise((r) => setTimeout(r, 100));
      }
      if (!clicked) return { blind: "could not press Share in the helper" };
      shareAt = Date.now();
    }

    // Wait for the page to settle on an outcome (not one of its transient lines).
    const MSG = "(function(){var m=document.getElementById('bpmsg');return m?m.textContent:''})()";
    let msg = "", settledAt = 0;
    const deadline = Date.now() + (c.want.waitMs || 6000);
    while (Date.now() < deadline) {
      msg = await evalIn(pageSid, MSG) || "";
      if (msg && !/^Waiting for the location window|^Saving your location|^Using /.test(msg)) { settledAt = Date.now(); break; }
      if (c.stub && popupSid && geoTimeout === null) geoTimeout = await evalIn(popupSid, "window.__geoOpts&&window.__geoOpts.timeout");
      await new Promise((r) => setTimeout(r, 100));
    }
    await new Promise((r) => setTimeout(r, 700));     // anything late (a second POST, a fill)
    const fb = await evalIn(pageSid, "(function(){var f=document.getElementById('bplocfb');if(!f)return null;var a=f.querySelector('a');" +
                                       "return {shown:getComputedStyle(f).display!=='none'&&!!f.offsetParent,href:a?a.href:''}})()");
    const after = await evalIn(pageSid, BOXES);
    ws.close();
    return { msg, settledMs: settledAt ? settledAt - shareAt : null, fb, before, after, posts, others, geoTimeout };
  } finally {
    chrome.kill();
    await new Promise((r) => setTimeout(r, 300));
    try { rmSync(profile, { recursive: true, force: true }); } catch {}
  }
}

// Never print a coordinate: the page's own lines can echo the device's stored location
// ("Using <lat>, <lon>"), so any number with 2+ decimals is redacted before it is shown.
const redact = (t) => String(t).replace(/-?\d+\.\d{2,}/g, "<n>");

let worst = 0;
for (const c of CASES) {
  const r = await runCase(c);
  if (SABOTAGE && !sabotageApplied) { console.log(`${c.key}: BLIND -- the sabotage never applied`); worst = Math.max(worst, 2); continue; }
  if (r.blind) { console.log(`${c.key} ${c.name}: BLIND -- ${r.blind}`); worst = Math.max(worst, 2); continue; }
  const problems = [];
  if (!r.msg.includes(c.want.msg)) problems.push(`message "${redact(r.msg).slice(0, 90)}"`);
  const fbShown = !!(r.fb && r.fb.shown);
  if (fbShown !== c.want.fallback) problems.push(`fallback ${fbShown ? "shown" : "hidden"}`);
  if (c.want.fallback && !(r.fb && /gps-coordinates\.org/.test(r.fb.href))) problems.push("fallback link missing");
  if (r.posts.length !== c.want.posts) problems.push(`${r.posts.length} save POST(s)`);
  if (r.others.length) problems.push(`other device requests: ${r.others.join(", ")}`);
  const unchanged = r.before === r.after;
  if (c.want.boxes === "unchanged" && !unchanged) problems.push("boxes CHANGED");
  if (c.want.boxes === "sentinel" && r.after !== JSON.stringify(["11.1111", "-22.2222"])) problems.push("boxes do not show the saved 4-dp value");
  if (c.want.posts === 1 && r.posts[0] !== "lat=11.1111&lon=-22.2222") problems.push(`save body "${redact(r.posts[0])}"`);
  const zero = JSON.parse(r.after).some((v) => /^-?0(\.0*)?$/.test(String(v).trim()));
  if (zero) problems.push("a box holds 0");
  if (c.key === "T" && r.geoTimeout !== mod.LOCATE_TIMEOUT_MS) problems.push(`helper asked for timeout ${r.geoTimeout}`);
  if (c.key === "T" && !(r.settledMs >= mod.LOCATE_TIMEOUT_MS)) problems.push(`settled after ${r.settledMs} ms`);
  const extra = c.key === "T" ? ` (helper asked ${r.geoTimeout} ms; page settled ${r.settledMs} ms after Share)`
              : c.key === "C" ? ` (page noticed ${r.settledMs} ms after the close)` : "";
  console.log(`${c.key} ${c.name.padEnd(24)} saves ${r.posts.length}  fallback ${fbShown ? "shown " : "hidden"}  ` +
              `boxes ${c.want.boxes === "sentinel" ? "= saved value" : unchanged ? "unchanged" : "CHANGED"}  ` +
              `${problems.length ? "FAIL: " + problems.join("; ") : "ok"}${extra}`);
  console.log(`    page said: ${redact(r.msg)}`);
  if (problems.length) worst = Math.max(worst, 1);
}
process.exit(worst);
