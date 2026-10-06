#!/usr/bin/env node
// NO REQUEST CARRIES THE POSITION -- the wire test for /blipscope/locate (docs/RELEASE-v15.md §5).
//
// The vitest file executes the page's script against a stub; that proves what the
// SCRIPT calls. This proves what the BROWSER sends. A CSP header can be relaxed in
// one edit, and a leak can be written in ways a stub never anticipated; the wire is
// where every one of them has to show up.
//
// Hermetic: headless Chrome over CDP, Fetch interception on every target (the popup
// included). This script fulfils exactly two URLs itself -- the opener at
// http://blipscope-test.local/ and the helper at https://scopes.valarsystems.com/
// blipscope/locate, built from src/locatepage.ts with its real headers -- and records
// then FAILS every other request. Chrome's own geolocation is overridden to a SENTINEL
// that is obviously not a place (11.1111111, -22.2222222 -- FAKE), and permission is
// granted to the helper's origin, so the page's REAL getCurrentPosition runs.
//
// THE ANCHOR IS NOT OPTIONAL. "No request carried the position" is equally true of a
// page that never received one. So the opener must receive the sentinel by postMessage,
// or the result is BLIND (exit 2), never a pass.
//
// RUN:  node scripts/check-locate-page.mjs [--selftest]
// EXIT: 0 no request carries the position   1 one does   2 blind / rig broken

import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { build } from "esbuild";

const SENTINEL = { latitude: 11.1111111, longitude: -22.2222222, accuracy: 15 };   // FAKE -- not a place
const OPENER = "http://blipscope-test.local/";
const HELPER_ORIGIN = "https://scopes.valarsystems.com";
const CHROME = [
  process.env.CHROME,
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
  "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
].filter(Boolean).find((p) => existsSync(p));

// ---- the page under test, from the real module ---------------------------------
async function loadModule() {
  const out = await build({ entryPoints: ["src/locatepage.ts"], bundle: true, format: "esm", platform: "neutral", write: false, logLevel: "silent" });
  return import("data:text/javascript;base64," + Buffer.from(out.outputFiles[0].text).toString("base64"));
}

const b64 = (s) => Buffer.from(s).toString("base64");
const sha = async (s) => Buffer.from(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s))).toString("base64");

/** The helper response for one variant: the real page, optionally planted, CSP on or off.
 *  A planted script is RE-HASHED when the CSP is on -- otherwise the CSP would block the whole
 *  script and the plant would "pass" by never running. */
async function helperResponse(mod, { plant = null, csp = true } = {}) {
  const real = await mod.locateResponse();
  let html = await real.text();
  const headers = {};
  real.headers.forEach((v, k) => { headers[k] = v; });
  if (plant) {
    const marker = "say(COPY.sent,false);";
    if (!html.includes(marker)) throw new Error("plant anchor missing");
    html = html.replace(marker, marker + plant);
  }
  delete headers["content-length"];
  if (csp) {
    const script = html.match(/<script>([\s\S]*)<\/script>/)[1];
    const style = html.match(/<style>([\s\S]*)<\/style>/)[1];
    headers["content-security-policy"] = `default-src 'none'; script-src 'sha256-${await sha(script)}'; ` +
      `style-src 'sha256-${await sha(style)}'; form-action 'none'; base-uri 'none'; frame-ancestors 'none'`;
    if (!plant && headers["content-security-policy"] !== real.headers.get("content-security-policy"))
      throw new Error("re-derived CSP differs from the served one -- the rig would test a different policy");
  } else delete headers["content-security-policy"];
  return { html, headers };
}

const OPENER_HTML = `<!doctype html><html><body><script>
window.__got=[];addEventListener("message",function(e){window.__got.push({origin:e.origin,data:e.data});});
window.__w=window.open("${HELPER_ORIGIN}/blipscope/locate?o="+encodeURIComponent(location.origin));
</script></body></html>`;

// ---- needles ---------------------------------------------------------------------
function needles() {
  const out = new Set();
  for (const v of [SENTINEL.latitude, SENTINEL.longitude]) {
    for (const s of [String(v), v.toFixed(4), String(Math.round(v * 1e6))]) {
      out.add(s); out.add(encodeURIComponent(s)); out.add(b64(s));
    }
  }
  return [...out];
}
const NEEDLES = needles();
function carries(text) {
  const hay = [text];
  try { hay.push(decodeURIComponent(text)); } catch {}
  for (const tok of text.match(/[A-Za-z0-9+/_-]{8,}={0,2}/g) || []) {
    try { hay.push(Buffer.from(tok.replace(/-/g, "+").replace(/_/g, "/"), "base64").toString("latin1")); } catch {}
  }
  return NEEDLES.filter((n) => hay.some((h) => h.includes(n)));
}

// ---- one browser run ---------------------------------------------------------------
async function runVariant(mod, variant) {
  const profile = mkdtempSync(join(tmpdir(), "locwire-"));
  const chrome = spawn(CHROME, ["--headless=new", "--remote-debugging-port=0", `--user-data-dir=${profile}`,
    "--no-first-run", "--no-default-browser-check", "--disable-popup-blocking", "--disable-extensions",
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

    const requests = [];           // every request any page made
    const frames = [];             // WebSocket frames
    const sessions = {};           // targetId -> sessionId
    const openers = {};            // targetId -> the targetId that opened it
    const helper = await helperResponse(mod, variant);

    handlers.push(async (msg) => {
      const sid = msg.sessionId;
      if (msg.method === "Target.attachedToTarget") {
        const s = msg.params.sessionId;
        sessions[msg.params.targetInfo.targetId] = s;
        openers[msg.params.targetInfo.targetId] = msg.params.targetInfo.openerId || null;
        await send("Fetch.enable", { patterns: [{ urlPattern: "*" }] }, s);
        await send("Network.enable", {}, s);
        await send("Page.enable", {}, s);
        if (!variant.noGeo) await send("Emulation.setGeolocationOverride", SENTINEL, s);
        await send("Runtime.runIfWaitingForDebugger", {}, s);
      } else if (msg.method === "Fetch.requestPaused") {
        const r = msg.params.request;
        const body = (r.postData || "") + (r.postDataEntries || []).map((e) => e.bytes ? Buffer.from(e.bytes, "base64").toString("latin1") : "").join("");
        requests.push({ url: r.url, method: r.method, headers: JSON.stringify(r.headers), body, type: msg.params.resourceType });
        if (r.url === OPENER) {
          await send("Fetch.fulfillRequest", { requestId: msg.params.requestId, responseCode: 200,
            responseHeaders: [{ name: "Content-Type", value: "text/html; charset=utf-8" }], body: b64(OPENER_HTML) }, sid);
        } else if (r.url.startsWith(`${HELPER_ORIGIN}/blipscope/locate?`)) {
          await send("Fetch.fulfillRequest", { requestId: msg.params.requestId, responseCode: 200,
            responseHeaders: Object.entries(helper.headers).map(([name, value]) => ({ name, value })), body: b64(helper.html) }, sid);
        } else {
          await send("Fetch.failRequest", { requestId: msg.params.requestId, errorReason: "BlockedByClient" }, sid);
        }
      } else if (msg.method === "Network.webSocketCreated") {
        frames.push("ws-open " + msg.params.url);
      } else if (msg.method === "Network.webSocketFrameSent") {
        frames.push(msg.params.response?.payloadData || "");
      }
    });

    await send("Browser.grantPermissions", { permissions: ["geolocation"], origin: HELPER_ORIGIN });
    await send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: true, flatten: true });
    const { result: { targetId: openerId } } = await send("Target.createTarget", { url: "about:blank" });
    for (let i = 0; i < 50 && !sessions[openerId]; i++) await new Promise((r) => setTimeout(r, 50));
    const openerSid = sessions[openerId];
    if (!openerSid) return { blind: "the opener tab never attached" };
    await send("Page.navigate", { url: OPENER }, openerSid);

    // the popup: wait for its session and its button, then click it
    let popupSid = null;
    for (let i = 0; i < 100 && !popupSid; i++) {
      // THE popup is the target the opener opened -- not "any other tab": Chrome's own
      // startup tab attaches too, and the first version of this clicked around in it.
      const popupId = Object.keys(openers).find((t) => openers[t] === openerId);
      popupSid = popupId ? sessions[popupId] : null;
      if (!popupSid) await new Promise((r) => setTimeout(r, 100));
    }
    if (!popupSid) return { blind: "the helper popup never opened", requests, frames };
    let clicked = false;
    for (let i = 0; i < 50 && !clicked; i++) {
      const r = await send("Runtime.evaluate", { expression: "(function(){var b=document.getElementById('go');if(!b||b.disabled)return false;b.click();return true;})()", returnByValue: true }, popupSid);
      clicked = r.result?.result?.value === true;
      if (!clicked) await new Promise((r) => setTimeout(r, 100));
    }
    await new Promise((r) => setTimeout(r, 2500));       // the fix, the message, any leak
    const pm = await send("Runtime.evaluate", { expression: "(document.getElementById('msg')||{}).textContent||''", returnByValue: true }, popupSid);
    const popupMsg = pm.result?.result?.value || "(popup gone)";
    const got = await send("Runtime.evaluate", { expression: "JSON.stringify(window.__got)", returnByValue: true }, openerSid);
    const messages = JSON.parse(got.result?.result?.value || "[]");
    ws.close();
    return { requests, frames, messages, clicked, popupMsg };
  } finally {
    chrome.kill();
    await new Promise((r) => setTimeout(r, 300));
    try { rmSync(profile, { recursive: true, force: true }); } catch {}
  }
}

function grade(name, run, quiet = false) {
  if (run.blind) { if (!quiet) console.log(`${name}: BLIND -- ${run.blind}`); return 2; }
  const carrying = run.requests.filter((r) => carries(r.url + "\n" + r.headers + "\n" + r.body).length)
    .concat(run.frames.filter((f) => carries(f).length).map((f) => ({ url: "ws: " + f.slice(0, 60) })));
  const anchor = run.messages.find((m) => m.origin === HELPER_ORIGIN && m.data?.type === "blipscope-location" &&
                                         m.data.lat === SENTINEL.latitude.toFixed(4) && m.data.lon === SENTINEL.longitude.toFixed(4));
  if (!quiet) {
    console.log(`${name}: requests ${run.requests.length}, carrying the position ${carrying.length}; ` +
                `anchor ${anchor ? "OK (opener received the sentinel)" : "MISSING"}`);
    for (const c of carrying) console.log(`    LEAK: ${(c.method || "")} ${c.url.slice(0, 120)}`);
    // A missing anchor is a rig failure until shown otherwise: say what the rig saw.
    if (!anchor || process.env.LOCWIRE_DEBUG) {
      console.log(`    clicked=${run.clicked} messages=${JSON.stringify(run.messages).slice(0, 300)}`);
      for (const r of run.requests) console.log(`    req ${r.type} ${r.method} ${r.url.slice(0, 110)}`);
      if (run.popupMsg) console.log(`    popup said: ${run.popupMsg}`);
    }
  }
  if (!anchor) return 2;
  return carrying.length ? 1 : 0;
}

if (!CHROME) { console.log("BLIND: no Chrome/Chromium found (set CHROME=...)"); process.exit(2); }
const mod = await loadModule();

if (process.argv.includes("--selftest")) {
  const cases = [
    ["P1 sendBeacon plant, CSP removed", { plant: 'navigator.sendBeacon("/x",JSON.stringify({lat:c.latitude,lon:c.longitude}));', csp: false }, 1],
    ["P2 Image plant, CSP removed", { plant: 'new Image().src="/p?"+c.latitude;', csp: false }, 1],
    ["P3 Image plant, real CSP (re-hashed)", { plant: 'new Image().src="/p?"+c.latitude;', csp: true }, 0],
    ["BLIND geolocation override removed", { noGeo: true }, 2],
  ];
  let rc = 0;
  for (const [name, variant, want] of cases) {
    const got = grade(name, await runVariant(mod, variant));
    const ok = got === want;
    console.log(`  ${ok ? "ok  " : "FAIL"}  ${name}: exit ${got} (want ${want})`);
    if (!ok) rc = 1;
  }
  console.log(rc ? "SELFTEST FAILED" : "SELFTEST PASSED");
  process.exit(rc);
}

process.exit(grade("V0 the real page, real CSP", await runVariant(mod, {})));
