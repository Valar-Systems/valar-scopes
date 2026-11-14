// T-web, the no-photo detail card (#670; docs/card-nophoto-fix.md, section 3), in Node, against a built model.
//
//   node tools/web-renderer/card_nophoto.mjs [dist/s3-128]
//
// The web build fetches no photos, so every card in it is a no-photo card: the case itself, end to end, through
// the firmware's own drawing and its own tap path. On the synthetic scene (site/scene.js), enrich answered:
//   1. the scene's clock is chosen from the scene's own /blips answers (never from a copy of its arithmetic): the
//      second at which SKW3307 is within 30 km of the centre and farthest from every other aircraft, so a nearer
//      marker cannot take the tap;
//   2. the radar runs until SKW3307's callsign label is drawn, and the tap lands at that text op's own x, y (the
//      label box AircraftLabelBox builds is inclusive of its corner), down and up through web_touch;
//   3. 60 frames later, the frame's text is read.
//
//   CN0 (precondition) the card for SKW3307 is open: its title drawn at text size 2, and the slot layout's caption
//       ("Loading photo..." or "No photo available"). Otherwise INSTRUMENT, exit 2: a card that did not open, or
//       opened on another contact, is never read as rows dropped.
//   CN1 the route row "SLC -> SEA" is drawn (routelabel::CardLine)
//   CN2 "Type: CRJ9" is drawn
//   CN3 "SkyWest Airlines" is drawn
//
// Exit 0 every row drawn, 1 a row missing, 2 the instrument. Predicted on the code before the fix: s3-128 CN0 ok,
// CN1-CN3 FAIL, exit 1; s3-21 all ok, exit 0. That pair is the check's control: shown able to pass and to fail.
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
const CS = "SKW3307", HEX = "a7d9f5";
const ROWS = [["CN1", "SLC -> SEA"], ["CN2", "Type: CRJ9"], ["CN3", "SkyWest Airlines"]];
const CAPTIONS = ["Loading photo...", "No photo available"];
const instrument = (why) => { console.log(`INSTRUMENT: ${why}`); process.exit(2); };

// The scene's clock offset T (s): every second over one cycle, read from the scene's own answers.
async function chooseOffset() {
  const lat0 = Number(BEND.latitude), lon0 = Number(BEND.longitude), now = Date.now();
  const km = (a, b) => Math.hypot((a[2] - b[2]) / 1e4 * 111.32, (a[3] - b[3]) / 1e4 * 111.32 * Math.cos(lat0 * Math.PI / 180));
  const centre = [0, 0, Math.round(lat0 * 1e4), Math.round(lon0 * 1e4)];
  let best = null;
  for (let T = 0; T < 400; T++) {
    const r = await makeScene({ enrich: true, t0: now - T * 1000 }).fetch(`/api/v1/blipscope/blips?lat=${lat0}&lon=${lon0}`);
    const a = JSON.parse(Buffer.from(r.body).toString("utf8")).a;
    const me = a.find((p) => p[0] === HEX);
    if (!me || km(me, centre) > 30) continue;
    const sep = Math.min(...a.filter((p) => p[0] !== HEX).map((p) => km(p, me)));
    if (!best || sep > best.sep) best = { T, sep, fromCentre: km(me, centre) };
  }
  return best;
}

async function frames(mod, n) { for (let i = 0; i < n; i++) { mod._web_frame(); await sleep(16); } }

// The last presented frame's text ops: [len, OP_TEXT (11), fg, bg, x, y, sx, sy, offset, count].
function texts(mod) {
  const ops = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
  const tb = mod._web_text_ptr(), out = [];
  for (let i = 0; i < ops.length; i += ops[i]) {
    if (ops[i + 1] !== 11) continue;
    const s = Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1");
    out.push({ s, x: ops[i + 4], y: ops[i + 5], sx: ops[i + 6] });
  }
  return out;
}

let failures = 0;
try {
  const pick = await chooseOffset();
  if (!pick) instrument(`${CS} is never within 30 km of the centre in the scene`);
  const scene = makeScene({ enrich: true, t0: Date.now() - pick.T * 1000 });
  globalThis.fetch = (url) => scene.fetch(String(url)).then((r) => ({
    status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  const mod = await require(path.join(dir, loader))({ print: () => {}, printErr: () => {} });
  for (const [k, v] of Object.entries(BEND)) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  mod._web_start();
  const size = mod._web_screen_size();
  console.log(`card_nophoto: ${path.basename(dir)} (${size} px); scene clock +${pick.T} s: ${CS} ${pick.fromCentre.toFixed(1)} km ` +
              `from the centre, ${pick.sep.toFixed(1)} km from the nearest other aircraft`);

  let label = null;
  for (let k = 0; k < 600 && !label; k++) { await frames(mod, 1); label = texts(mod).find((t) => t.s === CS && t.sx === 1); }
  if (!label) instrument(`${CS}'s label was never drawn on the radar in 600 frames`);
  mod._web_touch(1, label.x, label.y); await frames(mod, 3);
  mod._web_touch(0, label.x, label.y);
  await frames(mod, 60);

  const t = texts(mod), drawn = new Set(t.map((o) => o.s));
  const title = t.some((o) => o.s === CS && o.sx === 2), caption = CAPTIONS.find((c) => drawn.has(c));
  console.log(`  tapped ${CS}'s label at ${label.x}, ${label.y}; the frame draws ${t.length} string(s)`);
  if (!title || !caption) instrument(`CN0: the card for ${CS} is not open (title at size 2: ${title}; slot caption: ${caption || "none"})`);
  console.log(`  ok   CN0 the card for ${CS} is open: its title, and "${caption}"`);
  for (const [id, row] of ROWS) {
    const ok = drawn.has(row);
    if (!ok) failures++;
    console.log(`  ${ok ? "ok  " : "FAIL"} ${id} "${row}" is drawn`);
  }
} catch (e) {
  instrument(e && e.stack ? e.stack.split("\n")[0] : String(e));
}
console.log(failures ? `CARD NO-PHOTO: ${failures} row(s) missing` : "CARD NO-PHOTO: every row drawn");
process.exit(failures ? 1 : 0);
