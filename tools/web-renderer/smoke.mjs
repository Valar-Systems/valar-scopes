// Blipscope web renderer: the headless checks (docs/web-renderer.md, R3-R6), in Node, against a built model.
//
//   node tools/web-renderer/smoke.mjs [dist/s3-128] [--quick] [--r7-only]
//
// The firmware runs at ~60 frames a second of wall time. globalThis.fetch is replaced by a recorder that answers
// from the synthetic scene (site/scene.js), so the renderer's OWN default fetch path is the one exercised --
// including what it sends: every call's init object is kept and checked for headers.
//   R3  the data path: /blips with the device's query, no header, ~12 a minute, contacts drawn; /config,
//       /airports, photos and posts never fetched; every request under /api/v1/blipscope/
//   R4  /enrich answered 404: each hex asked at most 3 times; no type line drawn
//   R5  /enrich answered: type lines drawn
//   R6  an off-base URL: answered locally (404), no request
//   R7  wx-layer on: the module stops at the shim's setBuffer, an abort with the shim's own line (docs/wx-device.md,
//       AB1). --r7-only runs it alone; its sabotage (sabotage_setbuffer.mjs) removes the abort and requires it red
// Exit 0 when every check holds, 1 when one does not, 2 when the instrument could not run.
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import { makeScene } from "./site/scene.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const dir = path.resolve(args.find((a) => !a.startsWith("--")) || "tools/web-renderer/dist/s3-128");
const quick = args.includes("--quick");
const r7only = args.includes("--r7-only");

// R7's expected line, read from the shim's setBuffer -- taken from the other side, never retyped here. null when it
// cannot be read, which is the instrument's failure, not the code's.
function setBufferMessage() {
  let shim;
  try { shim = fs.readFileSync(path.join(HERE, "shim", "LovyanGFX.hpp"), "utf8"); } catch { return null; }
  const body = shim.match(/void setBuffer\([^)]*\)\s*\{([\s\S]*?)std::abort\(\);/);
  const f = body && body[1].match(/std::fprintf\(stderr,([\s\S]*?)\);/);
  if (!f) return null;
  const s = [...f[1].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((x) => x[1]).join("").replace(/\\n$/, "");
  return s.length ? s : null;
}
if (args.includes("--print-setbuffer-message")) {   // a probe of the reading alone, with no build
  const m = setBufferMessage();
  console.log(m === null ? "INSTRUMENT: the shim's setBuffer message could not be read" : m);
  process.exit(m === null ? 2 : 0);
}
const require = createRequire(import.meta.url);
const loader = fs.readdirSync(dir).find((f) => /^blipscope-web-.*\.js$/.test(f));
if (!loader) { console.log(`INSTRUMENT: no blipscope-web-*.js in ${dir}`); process.exit(2); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const CONFIG = { latitude: "44.06", longitude: "-121.32", radius: "40", "radius-unit": "km" };
const results = [];
const check = (id, ok, what) => { results.push(ok); console.log(`  ${ok ? "ok  " : "FAIL"} ${id} ${what}`); };

async function run(label, { enrich, seconds }) {
  const scene = makeScene({ enrich });
  const inits = [];
  globalThis.fetch = (url, init) => { inits.push(init); return scene.fetch(String(url)).then((r) => ({
    status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) })); };
  delete require.cache[path.join(dir, loader)];
  const factory = require(path.join(dir, loader));
  const mod = await factory({ print: () => {}, printErr: () => {} });
  for (const [k, v] of Object.entries(CONFIG)) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  mod._web_start();
  const t0 = Date.now();
  while (Date.now() - t0 < seconds * 1000) { mod._web_frame(); await sleep(16); }
  const ops = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
  const tb = mod._web_text_ptr(), text = [];
  for (let i = 0; i < ops.length; i += ops[i])
    if (ops[i + 1] === 11) text.push(Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1"));
  const net = mod.HEAPU32.subarray(mod._web_net_stats() >> 2, (mod._web_net_stats() >> 2) + 6);
  console.log(`${label}: ${path.basename(dir)}, ${seconds} s; requests ${scene.log.length}; ` +
              `net blips ${net[0]} enrich ${net[1]} answered-locally ${net[2]} posts ${net[3]} timeouts ${net[4]} from-loop ${net[5]}`);
  return { mod, scene, inits, text, net, seconds };
}

// R7 (docs/wx-device.md, AB1): wx-layer on must stop the module at the shim's setBuffer -- the per-pixel weather layer
// cannot run on a stand-in with no pixels. Initialise() reaches it inside _web_start(). Held only when something was
// thrown with "Aborted" in it AND printErr carried the shim's own line; with the abort removed, neither happens.
async function r7() {
  const msg = setBufferMessage();
  if (msg === null) { console.log("INSTRUMENT: the shim's setBuffer message could not be read (shim/LovyanGFX.hpp)"); process.exit(2); }
  const scene = makeScene({ enrich: false });
  globalThis.fetch = (url) => scene.fetch(String(url)).then((r) => ({
    status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  const errs = [];
  delete require.cache[path.join(dir, loader)];
  const factory = require(path.join(dir, loader));
  const mod = await factory({ print: () => {}, printErr: (s) => errs.push(String(s)) });
  for (const [k, v] of Object.entries({ ...CONFIG, "wx-layer": "true" })) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  let thrown = null;
  try {
    await mod._web_start();
    for (let i = 0; i < 30; i++) { await mod._web_frame(); await sleep(16); }
  } catch (e) {
    thrown = e;
  }
  const what = thrown === null ? "" : String((thrown && thrown.message) || thrown);
  const said = errs.some((l) => l.includes(msg));
  console.log(`R7: ${path.basename(dir)}; thrown ${thrown === null ? "nothing" : JSON.stringify(what.slice(0, 60))}; ` +
              `the message ${said ? "printed" : "absent"}`);
  check("R7", /Aborted/.test(what) && said, "wx-layer on stops at the shim's setBuffer: an abort, with its message");
}

function verdict() {
  const ok = results.every(Boolean);
  console.log(ok ? "WEB RENDERER SMOKE AS PREDICTED" : `WEB RENDERER SMOKE: ${results.filter((x) => !x).length} NOT AS PREDICTED`);
  process.exit(ok ? 0 : 1);
}

if (r7only) {
  try {
    await r7();
  } catch (e) {
    console.log("INSTRUMENT: " + (e && e.stack || e));
    process.exit(2);
  }
  verdict();
}

const types = new Set(makeScene().planes.map((p) => p[2]));
try {
  // R3 + R5 (enrich answered), then R6 on the same module
  const a = await run("R3/R5", { enrich: true, seconds: quick ? 25 : 70 });
  const blips = a.scene.log.filter((r) => r.url.startsWith("/api/v1/blipscope/blips?"));
  const q = blips.length ? new URL(blips[0].url, "https://x.invalid/").searchParams : new URLSearchParams();
  check("R3", a.scene.log.every((r) => r.url.startsWith("/api/v1/blipscope/blips?") || r.url.startsWith("/api/v1/blipscope/enrich/")),
        "every request is under /api/v1/blipscope/ (blips or enrich)");
  check("R3", q.get("lat") === "44.06" && q.get("lon") === "-121.32" && q.get("r") === "40" && q.get("limit") === "40",
        `the device's query: ${blips[0] ? blips[0].url : "(none)"}`);
  check("R3", a.inits.length > 0 && a.inits.every((i) => i && i.headers === undefined && i.credentials === "omit"),
        `no request carries a header (${a.inits.length} inits read)`);
  const perMin = (blips.length / a.seconds) * 60;
  check("R3", quick ? blips.length >= 4 : perMin >= 10 && perMin <= 14, `/blips ${blips.length} in ${a.seconds} s (${perMin.toFixed(1)} a minute)`);
  const contacts = a.text.filter((s) => /^[A-Z]{2,3}\d|^N\d/.test(s)).length;
  check("R3", contacts >= 15, `contacts drawn with labels: ${contacts}`);
  check("R3", a.net[2] >= 2 && a.net[5] === 0, `config and airports answered locally (${a.net[2]}), none from the loop`);
  check("R5", a.text.some((s) => types.has(s)), `type lines drawn: ${a.text.filter((s) => types.has(s)).join(" ") || "(none)"}`);
  const before = a.scene.log.length, local0 = a.mod.HEAPU32[(a.mod._web_net_stats() >> 2) + 2];
  const st = a.mod.ccall("web_http_probe", "number", ["string"], ["https://elsewhere.invalid/api/v1/blipscope/blips"]);
  const st2 = a.mod.ccall("web_http_probe", "number", ["string"], ["https://web.invalid/api/v1/blipscope/config"]);
  const local1 = a.mod.HEAPU32[(a.mod._web_net_stats() >> 2) + 2];
  check("R6", st === 404 && st2 === 404 && a.scene.log.length === before && local1 === local0 + 2,
        `off-base and not-allowed URLs answered locally (${st}, ${st2}), requests ${a.scene.log.length - before}`);

  // R4: /enrich answered 404
  if (!quick) {
    const b = await run("R4", { enrich: false, seconds: 130 });
    const per = {};
    for (const r of b.scene.log) { const m = r.url.match(/\/enrich\/([0-9a-f]+)/); if (m) per[m[1]] = (per[m[1]] || 0) + 1; }
    const worst = Math.max(0, ...Object.values(per));
    check("R4", Object.keys(per).length > 0 && worst <= 3, `each hex asked at most 3 times (${Object.keys(per).length} hexes, most ${worst})`);
    check("R4", !b.text.some((s) => types.has(s)), "no type line drawn");
  }

  // R7, on a fresh module: last, because an abort leaves its module dead
  await r7();
} catch (e) {
  console.log("INSTRUMENT: " + (e && e.stack || e));
  process.exit(2);
}
verdict();
