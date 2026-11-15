// The web renderer's carousel (docs/web-renderer-carousel.md, WC2 and WC3), in Node, against a built model.
//
//   node tools/web-renderer/carousel.mjs [dist/s3-128]
//
// Read from each frame the firmware itself drew -- its text and its dots -- never from the code's intent:
//   WC2  from Radar, six swipes left and then six right: Radar and List alternate; no frame draws Stats (exactly
//        "STATS") or Connect (exactly "CONNECT", "SET LOCATION" or "[ Reset ]", or text starting "http://"); every
//        frame draws exactly 2 indicator dots
//   WC3  no location configured: the face is Radar (its "SET YOUR LOCATION" note), never Connect
// The markers are the faces' own: List draws "AIRCRAFT", Stats "STATS", Connect always its "http://" address. The
// no-location Radar face prints a bare IP and never "http://", so it cannot read as Connect.
// Exit 0 when every check holds, 1 when one does not, 2 when the instrument could not run. Run against a build of
// main's code, it must exit 1 (the control, docs/web-renderer-carousel.md).
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
import { makeScene } from "./site/scene.js";

const dir = path.resolve(process.argv.slice(2).find((a) => !a.startsWith("--")) || "tools/web-renderer/dist/s3-128");
const require = createRequire(import.meta.url);
const loader = fs.readdirSync(dir).find((f) => /^blipscope-web-.*\.js$/.test(f));
if (!loader) { console.log(`INSTRUMENT: no blipscope-web-*.js in ${dir}`); process.exit(2); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const BEND = { latitude: "44.06", longitude: "-121.32", radius: "40", "radius-unit": "km" };
let failures = 0;
const check = (id, ok, what) => { if (!ok) failures++; console.log(`  ${ok ? "ok  " : "FAIL"} ${id} ${what}`); };

async function boot(config) {
  const scene = makeScene({ enrich: true });
  globalThis.fetch = (url) => scene.fetch(String(url)).then((r) => ({
    status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  delete require.cache[path.join(dir, loader)];
  const mod = await require(path.join(dir, loader))({ print: () => {}, printErr: () => {} });
  for (const [k, v] of Object.entries(config)) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  mod._web_start();
  await frames(mod, 120);
  return mod;
}

async function frames(mod, n) { for (let i = 0; i < n; i++) { mod._web_frame(); await sleep(16); } }

// The last presented frame: its strings, and the indicator dots (fillCircle, op 3, on the dots' row at the dots'
// x positions for 2 or 4 dots, radius 2 or 3).
function read(mod) {
  const size = mod._web_screen_size(), cx = size / 2, rowY = size - 16;
  const ops = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
  const tb = mod._web_text_ptr(), text = [];
  let dots = 0;
  for (let i = 0; i < ops.length; i += ops[i]) {
    if (ops[i + 1] === 11) text.push(Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1"));
    if (ops[i + 1] === 3 && ops[i + 4] === rowY && (ops[i + 5] === 2 || ops[i + 5] === 3) &&
        [cx - 18, cx - 6, cx + 6, cx + 18].includes(ops[i + 3])) dots++;
  }
  return { text, dots };
}

function faceOf(text) {
  if (text.some((s) => s === "STATS")) return "Stats";
  if (text.some((s) => s === "CONNECT" || s === "SET LOCATION" || s === "[ Reset ]" || s.startsWith("http://"))) return "Connect";
  if (text.some((s) => s === "AIRCRAFT")) return "List";
  return "Radar";
}

// A horizontal swipe: down, six moves across 60 % of the face, up; then frames to settle and draw.
async function swipe(mod, dir) {
  const size = mod._web_screen_size(), y = size / 2;
  const x0 = dir < 0 ? size * 0.8 : size * 0.2, x1 = dir < 0 ? size * 0.2 : size * 0.8;
  mod._web_touch(1, x0, y); await frames(mod, 2);
  for (let k = 1; k <= 6; k++) { mod._web_touch(1, x0 + ((x1 - x0) * k) / 6, y); await frames(mod, 1); }
  mod._web_touch(0, x1, y);
  await frames(mod, 30);
}

try {
  // WC2
  const m = await boot(BEND);
  const seen = [], dotsSeen = new Set(), forbidden = [];
  const look = () => { const r = read(m); dotsSeen.add(r.dots); const f = faceOf(r.text); if (f === "Stats" || f === "Connect") forbidden.push(f); return f; };
  const start = look();
  for (let i = 0; i < 6; i++) { await swipe(m, -1); seen.push(look()); }
  const left = seen.splice(0);
  for (let i = 0; i < 6; i++) { await swipe(m, +1); seen.push(look()); }
  const right = seen.splice(0);
  const alt = ["List", "Radar", "List", "Radar", "List", "Radar"];
  console.log(`WC2: ${path.basename(dir)}; start ${start}; left ${left.join(" ")}; right ${right.join(" ")}; dots ${[...dotsSeen].join(",")}`);
  check("WC2", start === "Radar", `starts on Radar (${start})`);
  check("WC2", JSON.stringify(left) === JSON.stringify(alt) && JSON.stringify(right) === JSON.stringify(alt),
        "six swipes each way alternate Radar and List");
  check("WC2", forbidden.length === 0, `no frame draws Stats or Connect (${forbidden.length ? forbidden.join(",") : "none"})`);
  check("WC2", dotsSeen.size === 1 && dotsSeen.has(2), `every frame read draws exactly 2 dots (${[...dotsSeen].join(",")})`);

  // WC3
  const n = await boot({ radius: "40", "radius-unit": "km" });
  const f = faceOf(read(n).text);
  console.log(`WC3: no location; the face is ${f}`);
  check("WC3", f === "Radar", `no location lands on Radar, not Connect (${f})`);
} catch (e) {
  console.log(`INSTRUMENT: ${e && e.stack ? e.stack.split("\n")[0] : e}`);
  process.exit(2);
}
console.log(failures ? `CAROUSEL: ${failures} check(s) failed` : "CAROUSEL AS PREDICTED");
process.exit(failures ? 1 : 0);
