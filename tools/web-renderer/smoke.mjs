// Blipscope web renderer: the headless checks (docs/web-renderer.md, R3-R6), in Node, against a built model.
//
//   node tools/web-renderer/smoke.mjs [dist/s3-128] [--quick] [--r7p-only]
//
// The firmware runs at ~60 frames a second of wall time. globalThis.fetch is replaced by a recorder that answers
// from the synthetic scene (site/scene.js), so the renderer's OWN default fetch path is the one exercised --
// including what it sends: every call's init object is kept and checked for headers.
//   R3  the data path: /blips with the device's query, no header, ~12 a minute, contacts drawn; /config,
//       /airports and posts never fetched; every request under /api/v1/blipscope/. The scene offers a photo for
//       EVERY hex, and with no card open none is fetched (no prefetch)
//   R4  /enrich answered 404: each hex asked at most 3 times; no type line drawn
//   R5  /enrich answered: type lines drawn
//   R6  an off-base URL: answered locally (404), no request
//   R7  RETIRED with its sabotage (sabotage_setbuffer.mjs), by name (_handoffs/p4/web-weather/REG-web-weather.txt,
//       A3.4): it asserted that wx-layer on stops at the shim's setBuffer, and D1 makes setBuffer wrap its caller's
//       buffer instead. R7' and its sabotage (sabotage_plane.mjs) replace it:
//   R7' wx-layer on, with a crop served under /wx/, reaches a frame whose FIRST op is the weather plane: an OP_RASTER
//       of length 10 and format 1 (REG-web-weather, D6, A9.2). Its own module, on a virtual clock started at the crop's
//       frame + 30 s, so the crop is young enough to show; it runs last. --r7p-only runs it alone
//   R8  a card opened on a contact fetches THAT contact's photo, exactly once, under /api/v1/blipscope/photo/
//       (_handoffs/p4/web-photos/REG-web-photos.txt, W5 as amended)
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
const r7pOnly = args.includes("--r7p-only");
const require = createRequire(import.meta.url);
const loader = fs.readdirSync(dir).find((f) => /^blipscope-web-.*\.js$/.test(f));
if (!loader) { console.log(`INSTRUMENT: no blipscope-web-*.js in ${dir}`); process.exit(2); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const CONFIG = { latitude: "44.06", longitude: "-121.32", radius: "40", "radius-unit": "km" };
const results = [];
const check = (id, ok, what) => { results.push(ok); console.log(`  ${ok ? "ok  " : "FAIL"} ${id} ${what}`); };

// W5: every hex offered a photo (the fixture's bytes; only the requests are counted), on the one photo prefix.
const PREFIX = "/api/v1/blipscope/photo/";
const FIXTURE = new Uint8Array(fs.readFileSync(path.join(HERE, "fixtures", "photo-240.jpg")));
const PHOTOS = Object.fromEntries(makeScene().planes.map((p) => [p[0], { path: `${PREFIX}smoke-${p[0]}`, bytes: FIXTURE }]));
const photoRequests = (scene) => scene.log.map((e) => new URL(e.url, "https://x.invalid/").pathname).filter((p) => p.startsWith(PREFIX));

async function run(label, { enrich, seconds, photos = null }) {
  const scene = makeScene({ enrich, photos });
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

// R8 (_handoffs/p4/web-photos/REG-web-photos.txt, W5): the scene offers a photo for every hex; a tap on the first
// callsign label drawn opens a card, and that card's own contact -- read from its title, drawn at text size 2 -- must
// be the one whose photo is fetched, exactly once, under the prefix. Nothing before the tap.
async function r8() {
  const scene = makeScene({ enrich: true, photos: PHOTOS });
  globalThis.fetch = (url) => scene.fetch(String(url)).then((r) => ({
    status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  delete require.cache[path.join(dir, loader)];
  const mod = await require(path.join(dir, loader))({ print: () => {}, printErr: () => {} });
  for (const [k, v] of Object.entries(CONFIG)) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  mod._web_start();
  const planes = makeScene().planes, callsigns = new Set(planes.map((p) => p[1]));
  const drawn = () => {
    const ops = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
    const tb = mod._web_text_ptr(), out = [];
    for (let i = 0; i < ops.length; i += ops[i])
      if (ops[i + 1] === 11) out.push({ s: Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1"),
                                        x: ops[i + 4], y: ops[i + 5], sx: ops[i + 6] });
    return out;
  };
  let label = null;
  for (let k = 0; k < 900 && !label; k++) { mod._web_frame(); await sleep(16); label = drawn().find((t) => t.sx === 1 && callsigns.has(t.s)); }
  if (!label) throw new Error("R8: no callsign label was drawn in 900 frames");
  const before = photoRequests(scene).length;
  mod._web_touch(1, label.x, label.y);
  for (let i = 0; i < 3; i++) { mod._web_frame(); await sleep(16); }
  mod._web_touch(0, label.x, label.y);
  for (let i = 0; i < 240; i++) { mod._web_frame(); await sleep(16); }
  const title = drawn().find((t) => t.sx === 2 && callsigns.has(t.s));
  if (!title) throw new Error(`R8: tapping ${label.s}'s label opened no card`);
  const want = PHOTOS[planes.find((p) => p[1] === title.s)[0]].path, got = photoRequests(scene);
  console.log(`R8: ${path.basename(dir)}; tapped ${label.s}'s label, the card for ${title.s} opened; photo requests ${before} before, ${got.length} after`);
  check("R8", before === 0 && got.length === 1 && got[0] === want,
        `the card fetches its own photo once, under the prefix: ${got.join(", ") || "none"} (want ${want})`);
}

// R7' (_handoffs/p4/web-weather/REG-web-weather.txt, D6, A9.2): wx-layer on, with the fixture crop served under /wx/,
// reaches a frame whose FIRST op is the weather plane -- an OP_RASTER of length 10 and format 1 -- within 300 frames,
// with nothing thrown. The crop's frame is from 2024, so this module runs on its own virtual clock started at that
// frame + 30 s (X1's method: performance.now and Date.now replaced, 16 ms a frame, the event loop flushed after each),
// which is why it runs last: nothing reads the wall clock after it.
async function r7p() {
  const crop = fs.readFileSync(path.join(HERE, "..", "..", "test", "fixtures", "wx", "kdfw-60mi.wxc"));
  const epoch = crop.readUInt32LE(8);   // include/WxCrop.h :142, the frame's UTC seconds
  const VT0 = (epoch + 30) * 1000, perf0 = VT0 - 5000;
  let vt = VT0;
  Object.defineProperty(performance, "now", { value: () => vt - perf0, configurable: true, writable: true });
  Date.now = () => vt;
  const scene = makeScene({ enrich: true, t0: VT0 });
  // The Worker's reply headers (proxy/src/wx/handler.ts :89-90).
  const hdrs = { etag: '"smoke-r7p"', "x-wx-frame": String(epoch), "cache-control": "no-cache", "x-wx-max-age": "600" };
  let wx = 0;
  globalThis.fetch = (url) => {
    const u = String(url);
    if (new URL(u, "https://x.invalid/").pathname.startsWith("/api/v1/blipscope/wx/")) {
      wx++;
      return Promise.resolve({ status: 200, headers: { get: (n) => hdrs[String(n).toLowerCase()] ?? null },
        arrayBuffer: async () => crop.buffer.slice(crop.byteOffset, crop.byteOffset + crop.byteLength) });
    }
    return scene.fetch(u).then((r) => ({ status: r.status,
      arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  };
  delete require.cache[path.join(dir, loader)];
  const errs = [];
  const mod = await require(path.join(dir, loader))({ print: () => {}, printErr: (s) => errs.push(String(s)) });
  for (const [k, v] of Object.entries({ ...CONFIG, "wx-layer": "true" })) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  let thrown = null, at = -1;
  try {
    await mod._web_start();
    await new Promise((r) => setImmediate(r));
    for (let k = 0; k < 300 && at < 0; k++) {
      mod._web_frame();
      const ops = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
      if (ops.length >= 10 && ops[0] === 10 && ops[1] === 16 && ops[9] === 1) at = k;
      vt += 16;
      await new Promise((r) => setImmediate(r));
    }
  } catch (e) {
    thrown = String((e && e.message) || e);
  }
  console.log(`R7': ${path.basename(dir)}; crop frame ${epoch}; /wx/ requests ${wx}; thrown ${thrown === null ? "nothing" : JSON.stringify(thrown.slice(0, 60))}; ` +
              `printErr ${errs.length}; the first frame whose first op is the plane: ${at < 0 ? "none in 300" : at}`);
  check("R7'", thrown === null && at >= 0, "wx-layer on draws the weather plane FIRST: a format-1 OP_RASTER of length 10");
}

// The exit status IS the verdict, so it is set, never forced: Node 24 on Windows can die 127 inside process.exit()
// after loading wasm (libuv win\async.c:94), which loses the verdict (seen on R7' on 2026-11-25). process.exitCode lets
// the loop drain; the fallback timer is unref'd, so it fires only if something else keeps the loop alive.
function finish(code) {
  process.exitCode = code;
  setTimeout(() => process.exit(code), 3000).unref();
}

function verdict() {
  const ok = results.every(Boolean);
  console.log(ok ? "WEB RENDERER SMOKE AS PREDICTED" : `WEB RENDERER SMOKE: ${results.filter((x) => !x).length} NOT AS PREDICTED`);
  finish(ok ? 0 : 1);
}

async function all() {
  const types = new Set(makeScene().planes.map((p) => p[2]));
  // R3 + R5 (enrich answered), then R6 on the same module
  const a = await run("R3/R5", { enrich: true, seconds: quick ? 25 : 70, photos: PHOTOS });
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
  check("R3", photoRequests(a.scene).length === 0,
        `no photo fetched with no card open, though every hex offers one: ${photoRequests(a.scene).length} photo request(s)`);
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

  // R8, on a fresh module: open whichever contact's card a tap on its label opens, and read the photo requests
  await r8();

  // R7', on a fresh module: LAST, because it replaces the clock
  await r7p();
}

try {
  if (r7pOnly) await r7p();
  else await all();
  verdict();
} catch (e) {
  console.log("INSTRUMENT: " + (e && e.stack || e));
  finish(2);
}
