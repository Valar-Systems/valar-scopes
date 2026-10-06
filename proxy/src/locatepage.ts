/* ===========================================================================
 * "USE MY LOCATION" -- the HTTPS helper the device's settings page opens.
 * Spec: docs/RELEASE-v15.md §5.
 *
 * WHY A HOSTED PAGE. The settings page is plain HTTP at http://<device>.local,
 * and navigator.geolocation only exists in a secure context. Measured
 * 2026-09-24: on that origin getCurrentPosition fails at once ("Only secure
 * origins are allowed"), with no prompt. So the settings page opens THIS page in
 * a popup -- the enrolment pattern (enrollpage.ts) -- and gets the position back
 * by postMessage, which crosses HTTPS -> HTTP because only a message moves.
 *
 * THE POSITION NEVER LEAVES THE BROWSER IN A REQUEST. It travels exactly one
 * way, helper -> opener by postMessage. This page makes no fetch, XHR, beacon,
 * WebSocket, form post, image or script load, and no navigation; it stores
 * nothing. Enforced by the CSP below (default-src 'none' covers images and fonts,
 * not just connect-src) AND by scripts/check-locate-page.mjs, which watches the
 * wire in headless Chromium -- a header can be relaxed in one edit; the wire
 * test fails however a leak is written.
 *
 * ONE RULE PER QUESTION, SHARED WITH THE PAGE. The origin rule (ORIGIN_RE) and
 * the formatter (formatCoord) are exported for the tests AND inlined into the
 * page's script from these same definitions (.source / .toString()), so the page
 * cannot drift from what the tests check. The script is identical on every
 * request -- it reads ?o= from its own URL -- which is what lets the CSP pin it
 * by hash.
 *
 * THE CLICK ON OUR OWN PAGE IS THE GATE. getCurrentPosition runs only when the
 * "Share" button on THIS page is tapped. The browser remembers a granted
 * permission per origin, so without our own click any website could open
 * /locate and read a returning customer's position silently.
 * ======================================================================== */

/** Origins the position may be posted to: the device page, on mDNS or a private IPv4
 *  address (the AP-mode page is http://192.168.4.1). Anchored at both ends -- a
 *  dropped `$` would accept http://blipscope.local.evil.example. */
export const ORIGIN_RE =
  /^http:\/\/(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.local|10(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){3}|192\.168(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){2}|172\.(?:1[6-9]|2\d|3[01])(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){2})$/i;

export function isAllowedOrigin(o: string): boolean {
  return ORIGIN_RE.test(o);
}

/** 4 decimal places (~11 m), the precision the settings page echoes. A value that rounds
 *  to zero is written "0.0000", never "-0.0000". */
export function formatCoord(v: number): string {
  const s = v.toFixed(4);
  return s === "-0.0000" ? "0.0000" : s;
}

export const GPS_FALLBACK = "https://www.gps-coordinates.org/";

/** The customer-facing sentences, one per case (spec §5). Exported so the tests assert
 *  the page carries them, rather than a copy typed into the test. */
export const LOCATE_COPY = {
  ask: "Share this browser's location with your Blipscope? It goes to the settings page you came from and nowhere else.",
  denied: "Location is turned off for this page, so nothing was filled in. You can find your numbers at gps-coordinates.org and paste them into either box.",
  unavailable: "This browser couldn't work out where it is. Paste your numbers from gps-coordinates.org instead.",
  timeout: "Finding your location took too long. Try again, or paste your numbers from gps-coordinates.org.",
  badOrigin: "This page only answers your Blipscope's own settings page. Open it from the “Use my location” button there.",
  sent: "Sent to your settings page. You can close this window.",
} as const;

const STYLE = `:root{color-scheme:dark;--bg:#0f1115;--fg:#e8e6e1;--dim:#9aa0a6;--bad:#ff6b6b;--line:#2a2f3a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.55 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;display:flex;align-items:center;justify-content:center;min-height:100vh;padding:24px}
.card{width:100%;max-width:30rem;border:1px solid var(--line);border-radius:10px;padding:24px;background:#141821}
h1{margin:0 0 10px;font-size:1.2rem}
p{margin:0 0 16px}
.bad{color:var(--bad)} .dim{color:var(--dim);font-size:.9rem}
button{font:inherit;background:#1f6feb;color:#fff;border:0;border-radius:6px;padding:10px 16px;cursor:pointer}
button:disabled{opacity:.5;cursor:not-allowed}
a{color:#6ea8fe}`;

// The page's whole script. Built ONCE, from the exported rule and formatter, so it is
// byte-identical on every request (and its CSP hash is a constant).
const SCRIPT = `(function(){
var ORIGIN_RE=${ORIGIN_RE.toString()};
var formatCoord=${formatCoord.toString()};
var COPY=${JSON.stringify(LOCATE_COPY)};
var o=new URLSearchParams(location.search).get("o")||"";
var msg=document.getElementById("msg"),btn=document.getElementById("go");
function say(t,bad){msg.textContent=t;msg.className=bad?"bad":"";}
if(!ORIGIN_RE.test(o)||!window.opener){say(COPY.badOrigin,true);btn.disabled=true;return;}
btn.addEventListener("click",function(){
  btn.disabled=true;
  navigator.geolocation.getCurrentPosition(function(p){
    var c=p.coords;
    window.opener.postMessage({type:"blipscope-location",lat:formatCoord(c.latitude),lon:formatCoord(c.longitude),acc:Math.round(c.accuracy)},o);
    say(COPY.sent,false);
    setTimeout(function(){window.close();},400);
  },function(e){
    var t=e&&e.code===1?COPY.denied:e&&e.code===3?COPY.timeout:COPY.unavailable;
    say(t,true);
    window.opener.postMessage({type:"blipscope-location-error",code:e?e.code:0,text:t},o);
    btn.disabled=false;
  },{enableHighAccuracy:true,timeout:15000,maximumAge:0});
});
})();`;

export function locateScript(): string {
  return SCRIPT;
}

export function locateHtml(): string {
  return `<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<meta name="referrer" content="no-referrer">
<title>Use my location - Blipscope</title>
<style>${STYLE}</style>
</head><body>
<main class="card">
<h1>Use my location</h1>
<p id="msg">${LOCATE_COPY.ask}</p>
<p><button id="go" type="button">Share my location</button></p>
<p class="dim">Prefer not to? Find your numbers at <a href="${GPS_FALLBACK}" target="_blank" rel="noopener noreferrer">gps-coordinates.org</a> and paste them into either box on the settings page.</p>
</main>
<script>${SCRIPT}</script>
</body></html>`;
}

async function sha256b64(s: string): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s));
  let bin = "";
  for (const b of new Uint8Array(d)) bin += String.fromCharCode(b);
  return btoa(bin);
}

let cspCache: string | null = null;

/** default-src 'none' covers fonts and images, not only connect-src -- `connect-src 'none'`
 *  alone would leave `new Image().src = "...?lat="` open. A navigation cannot be blocked by
 *  CSP at all, which is why the wire test watches navigations too. */
export async function locateCsp(): Promise<string> {
  if (!cspCache) {
    cspCache = `default-src 'none'; script-src 'sha256-${await sha256b64(SCRIPT)}'; ` +
               `style-src 'sha256-${await sha256b64(STYLE)}'; form-action 'none'; base-uri 'none'; ` +
               `frame-ancestors 'none'`;
  }
  return cspCache;
}

export async function locateResponse(): Promise<Response> {
  const bytes = new TextEncoder().encode(locateHtml());
  return new Response(bytes, {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Length": String(bytes.byteLength),
      "Content-Security-Policy": await locateCsp(),
      "Referrer-Policy": "no-referrer",
      "Cache-Control": "public, max-age=300",
    },
  });
}
