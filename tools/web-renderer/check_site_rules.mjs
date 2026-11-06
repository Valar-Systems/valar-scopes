// The site's content rules, checked on the BUILT files (docs/web-renderer.md, the follow-up, F2-F4).
//
//   node tools/web-renderer/check_site_rules.mjs <dist>             the release set and the flag's probe
//   node tools/web-renderer/check_site_rules.mjs --selftest <dist>  every plant must be caught, on copies
//
// The release set's JavaScript (each model's loader, the render module) must hold no eval, no storage, no cookies
// and no absolute URL. The wasm reaches the network only through the loader's fetch import (web_http.cpp answers
// every path but /blips and /enrich locally, R6), so it is held to its URL strings alone, against the named list
// below: a new one fails until someone has looked at it. The probe proves -sDYNAMIC_EXECUTION=0 took, since the flag
// changes no byte of the web build: build.sh links a program that needs eval with its own flags (none allowed) and
// again with only that flag removed (exactly one required, or the probe could not see the flag at all).
//
// Exit 0: clean. 1: a finding, each printed with its category and file. 2: an instrument failure -- no loader, no
// render module, no wasm or no probe was read, or the control showed no eval. Reading nothing never passes.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const JS_RULES = [
  ["eval", "eval(", /\beval\s*\(/g],
  ["eval", "Function(", /(?<![\w$.])Function\s*\(/g],          // bare or after `new`; not instrumentFunction(
  ["eval", "a string timer", /\bset(?:Timeout|Interval)\s*\(\s*["'`]/g],
  ["storage", "localStorage", /\blocalStorage\b/g],
  ["storage", "sessionStorage", /\bsessionStorage\b/g],
  ["storage", "indexedDB", /\bindexedDB\b/g],
  ["storage", "caches.", /\bcaches\s*\./g],
  ["storage", "navigator.storage", /\bnavigator\s*\.\s*storage\b/g],
  ["cookies", "document.cookie", /\bdocument\s*\.\s*cookie\b/g],
  ["cookies", "cookieStore", /\bcookieStore\b/g],
  ["absolute URL", "http(s)://", /\bhttps?:\/\//gi],
  ["absolute URL", "ws(s)://", /\bwss?:\/\//gi],
  ["absolute URL", "a //host string", /["'`]\/\/[A-Za-z0-9]/g],
];

// The wasm's URL strings: firmware constants, none reachable from the web build (R6). Each is named with its source.
const WASM_URLS = new Map([
  ["https://auth.opensky-network.org", "OpenSkyAuthTokenHandler: the device's own OpenSky login"],
  ["https://opensky-network.org", "AircraftManager: the OpenSky feed, a device-only source"],
  ["https://ntfy.sh", "AircraftManager: ntfy alerts, posted only by a device (the web build drops every POST)"],
  ["https://web.invalid", "build.sh's CLOUD_FEED_BASE placeholder, re-rooted onto the page by web_http.cpp"],
]);
const WASM_URL = /(?:https?|wss?):\/\/[A-Za-z0-9.-]+/g;

function scanJs(text, only) {
  const out = [];
  for (const [cat, name, re] of JS_RULES) {
    if (only && cat !== only) continue;
    for (const m of text.matchAll(re))
      out.push({ cat, name, at: JSON.stringify(text.slice(Math.max(0, m.index - 20), m.index + 20)) });
  }
  return out;
}

function scanWasm(bytes) {
  const out = [];
  for (const m of bytes.toString("latin1").matchAll(WASM_URL))
    if (!WASM_URLS.has(m[0])) out.push({ cat: "absolute URL", name: "a URL not on the wasm list", at: m[0] });
  return out;
}

function walk(dir) {
  const out = [];
  for (const e of fs.existsSync(dir) ? fs.readdirSync(dir, { withFileTypes: true }) : []) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...walk(p));
    else out.push(p);
  }
  return out;
}

// Returns { code, lines }.
function check(dist) {
  const files = walk(dist);
  const named = (re) => files.filter((f) => re.test(path.basename(f)));
  const loaders = named(/^blipscope-web-(?!render-).*\.js$/);
  const render = named(/^blipscope-web-render-.*\.js$/);
  const wasm = named(/^blipscope-web-.*\.wasm$/);
  const probe = path.join(dist, "probe", "dynamic-probe.js");
  const control = path.join(dist, "probe", "dynamic-probe-control.js");
  const lines = [];
  const missing = [];
  if (!loaders.length) missing.push("no model loader (blipscope-web-<model>-v<N>.js)");
  if (!render.length) missing.push("no render module (blipscope-web-render-v<N>.js)");
  if (!wasm.length) missing.push("no wasm (blipscope-web-<model>-v<N>.wasm)");
  if (!fs.existsSync(probe) || !fs.existsSync(control)) missing.push("no probe pair (probe/dynamic-probe*.js)");
  if (missing.length) return { code: 2, lines: missing.map((m) => "INSTRUMENT: read " + m + "; reading nothing never passes") };

  const findings = [];
  const rel = (f) => path.relative(dist, f).split(path.sep).join("/");
  for (const f of [...loaders, ...render]) {
    const found = scanJs(fs.readFileSync(f, "utf8"));
    lines.push(`  ${found.length ? "FIND" : "ok  "}  ${rel(f)}: ${found.length} finding(s)`);
    for (const x of found) findings.push({ ...x, file: rel(f) });
  }
  for (const f of wasm) {
    const bytes = fs.readFileSync(f);
    const urls = [...new Set([...bytes.toString("latin1").matchAll(WASM_URL)].map((m) => m[0]))].sort();
    const found = scanWasm(bytes);
    lines.push(`  ${found.length ? "FIND" : "ok  "}  ${rel(f)}: ${urls.length} URL string(s), ${found.length} not on the list`);
    for (const x of found) findings.push({ ...x, file: rel(f) });
  }
  const p = scanJs(fs.readFileSync(probe, "utf8"), "eval");
  const c = scanJs(fs.readFileSync(control, "utf8"), "eval");
  lines.push(`  ${p.length ? "FIND" : "ok  "}  the flag's probe: ${p.length} eval finding(s) with the build's flags`);
  lines.push(`  ${c.length === 1 ? "ok  " : "BAD "}  its control: ${c.length} eval finding(s) without -sDYNAMIC_EXECUTION=0 (exactly 1 required)`);
  for (const x of p) findings.push({ ...x, cat: "eval", name: x.name + " (the flag did not take)", file: rel(probe) });
  for (const x of findings) lines.push(`  ${x.cat}: ${x.name} in ${x.file} at ${x.at}`);
  if (c.length !== 1) {
    lines.push("INSTRUMENT: the control does not show exactly one eval, so the probe cannot show the flag took");
    return { code: 2, lines, findings };
  }
  lines.push(`site rules: ${loaders.length + render.length} JS file(s), ${wasm.length} wasm, the probe pair; ` +
             (findings.length ? `${findings.length} finding(s)` : "0 findings"));
  return { code: findings.length ? 1 : 0, lines, findings };
}

// Every plant goes into a COPY in a temporary folder, is asserted to have applied, and must be caught exactly.
function selftest(dist) {
  const real = check(dist);
  if (real.code !== 0) {
    console.log(real.lines.join("\n"));
    console.log("selftest: the real build must be clean before plants mean anything");
    return 2;
  }
  const files = walk(dist);
  const loader = files.find((f) => /^blipscope-web-(?!render-).*\.js$/.test(path.basename(f)));
  const render = files.find((f) => /^blipscope-web-render-.*\.js$/.test(path.basename(f)));
  const wasm = files.find((f) => /^blipscope-web-.*\.wasm$/.test(path.basename(f)));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "web-site-rules-"));
  const copy = (dir) => {
    fs.mkdirSync(path.join(dir, "probe"), { recursive: true });
    for (const f of [loader, render, wasm]) fs.copyFileSync(f, path.join(dir, path.basename(f)));
    for (const f of ["dynamic-probe.js", "dynamic-probe-control.js"])
      fs.copyFileSync(path.join(dist, "probe", f), path.join(dir, "probe", f));
    return dir;
  };
  let bad = 0;
  const say = (ok, what) => { console.log(`  ${ok ? "ok " : "BAD"}  ${what}`); if (!ok) bad++; };

  const clean = check(copy(path.join(tmp, "clean")));
  say(clean.code === 0, `CONTROL: clean copies of the real files pass (exit ${clean.code})`);

  const JS_PLANTS = [
    ["eval", 'eval("1+1");'], ["eval", 'new Function("return 1");'], ["eval", 'setTimeout("tick()", 1000);'],
    ["storage", 'localStorage.getItem("k");'], ["storage", 'sessionStorage.setItem("k", "v");'],
    ["storage", 'indexedDB.open("d");'], ["storage", 'caches.open("c");'], ["storage", "navigator.storage.persist();"],
    ["cookies", 'document.cookie = "a=1";'], ["cookies", 'cookieStore.get("a");'],
    ["absolute URL", 'fetch("https://example.com/x");'], ["absolute URL", 'new WebSocket("wss://example.com/x");'],
    ["absolute URL", 'import("//example.com/x.js");'],
  ];
  JS_PLANTS.forEach(([cat, plant], i) => {
    const dir = copy(path.join(tmp, `js-${i}`));
    const f = path.join(dir, path.basename(loader));
    const before = fs.readFileSync(f, "utf8");
    fs.writeFileSync(f, before + "\n;" + plant + "\n");
    const applied = fs.readFileSync(f, "utf8").includes(plant) && fs.readFileSync(f, "utf8") !== before;
    const r = check(dir);
    const exact = r.code === 1 && r.findings.length === 1 && r.findings[0].cat === cat;
    say(applied && exact, `S-js-${i} ${cat}: ${plant} -> exit ${r.code}, ` +
        `${(r.findings || []).map((x) => x.cat + "/" + x.name).join(", ") || "no finding"}${applied ? "" : " (NOT APPLIED)"}`);
  });

  // The wasm plant: one firmware URL changed at the same length, so nothing else in the binary moves.
  {
    const dir = copy(path.join(tmp, "wasm"));
    const f = path.join(dir, path.basename(wasm));
    const b = fs.readFileSync(f);
    const needle = Buffer.from("https://ntfy.sh"), plant = Buffer.from("https://evil.sh");
    const at = b.indexOf(needle), once = at >= 0 && b.indexOf(needle, at + 1) < 0;
    if (once) plant.copy(b, at);
    fs.writeFileSync(f, b);
    const r = check(dir);
    const exact = r.code === 1 && r.findings.length === 1 && r.findings[0].at === "https://evil.sh";
    say(once && exact, `S-wasm absolute URL: https://ntfy.sh -> https://evil.sh -> exit ${r.code}, ` +
        `${(r.findings || []).map((x) => x.at).join(", ") || "no finding"}${once ? "" : " (the needle is not in the wasm exactly once)"}`);
  }

  const empty = check(fs.mkdtempSync(path.join(tmp, "empty-")));
  say(empty.code === 2, `an empty folder: exit ${empty.code} (2 required: reading nothing never passes)`);

  fs.rmSync(tmp, { recursive: true, force: true });
  console.log(`site rules selftest: ${bad ? bad + " problem(s)" : "every plant caught, exactly"} (${JS_PLANTS.length + 1} plants)`);
  return bad ? 1 : 0;
}

const args = process.argv.slice(2);
if (args[0] === "--selftest") {
  process.exit(selftest(args[1] || "tools/web-renderer/dist"));
} else {
  const r = check(args[0] || "tools/web-renderer/dist");
  console.log(r.lines.join("\n"));
  process.exit(r.code);
}
