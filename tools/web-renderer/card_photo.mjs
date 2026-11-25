// The card WITH its photo, on the web build (_handoffs/p4/web-photos/REG-web-photos.txt, W4 as amended), in Node,
// against a built model. card_nophoto.mjs's method: the scene's clock is chosen from the scene's own answers, the tap
// lands on SKW3307's own label, and the frame is read back. Here the scene offers a photo for SKW3307 (site/scene.js's
// opt-in `photos`), served from this test's own fixture at the model's size (fixtures/photo-<size>.jpg).
//
//   node tools/web-renderer/card_photo.mjs [dist/s3-128]
//
//   CP0 (precondition) the card for SKW3307 is open: its title at text size 2. Otherwise INSTRUMENT, exit 2.
//   CP1 exactly ONE photo request after the tap, to the path enrich named, and none before it
//   CP2 the frame holds ONE raster (OP_RASTER), panel-sized, at (0, 0), whose pixels' sha256 equals the HOST
//       decoder's for the same fixture (fixtures/photo-decode.json) -- so the WebAssembly-compiled TJpgDec gives the
//       host-compiled one's bytes -- and whose hash in the list is those pixels' own (FNV-1a 64, two 24-bit halves)
//   CP3 web_photo_shown() is that path while the photo is drawn; "" after the first tap flips the card to its data
//       page (no photo drawn there); "" after the second tap closes it
//   CP4 (the control) the same card with enrich's p null: zero photo requests, no raster, "No photo available"
// Exit 0 every row holds, 1 a row off, 2 the instrument. On a build without the photo path (red first), CP1-CP3 read
// FAIL -- a function the module does not export is that row's failure, never a crash -- and CP4 holds.
import crypto from "node:crypto";
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import { makeScene } from "./site/scene.js";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const dir = path.resolve(process.argv.slice(2).find((a) => !a.startsWith("--")) || "tools/web-renderer/dist/s3-128");
const require = createRequire(import.meta.url);
const loader = fs.readdirSync(dir).find((f) => /^blipscope-web-.*\.js$/.test(f));
if (!loader) { console.log(`INSTRUMENT: no blipscope-web-*.js in ${dir}`); process.exit(2); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const BEND = { latitude: "44.06", longitude: "-121.32", radius: "40", "radius-unit": "km" };
const CS = "SKW3307", HEX = "a7d9f5";
const PREFIX = "/api/v1/blipscope/photo/";
const OP_TEXT = 11, OP_RASTER = 16;
const instrument = (why) => { console.log(`INSTRUMENT: ${why}`); process.exit(2); };

// The scene's clock offset T (s): every second over one cycle, read from the scene's own answers (as card_nophoto).
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

function list(mod) { return mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len()); }

function texts(mod) {
  const ops = list(mod), tb = mod._web_text_ptr(), out = [];
  for (let i = 0; i < ops.length; i += ops[i]) {
    if (ops[i + 1] !== OP_TEXT) continue;
    const s = Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1");
    out.push({ s, x: ops[i + 4], y: ops[i + 5], sx: ops[i + 6] });
  }
  return out;
}

// [len, OP_RASTER, id, x, y, w, h, hashLo, hashHi, format]: read by position, so the format appended by
// REG-web-weather D3 (A4.5) moves nothing here
function rasters(mod) {
  const ops = list(mod), out = [];
  for (let i = 0; i < ops.length; i += ops[i])
    if (ops[i + 1] === OP_RASTER) out.push({ id: ops[i + 2], x: ops[i + 3], y: ops[i + 4], w: ops[i + 5], h: ops[i + 6], lo: ops[i + 7], hi: ops[i + 8] });
  return out;
}

function fnv(bytes) {
  let h = 1469598103934665603n;
  for (const b of bytes) { h ^= BigInt(b); h = (h * 1099511628211n) & 0xFFFFFFFFFFFFFFFFn; }
  return { lo: Number(h & 0xFFFFFFn), hi: Number((h >> 24n) & 0xFFFFFFn) };
}

const shown = (mod) => (typeof mod._web_photo_shown === "function" ? mod.UTF8ToString(mod._web_photo_shown()) : null);

// One run: a fresh module on its own scene, the card opened on SKW3307's label.
async function openCard(pick, photos) {
  const scene = makeScene({ enrich: true, t0: Date.now() - pick.T * 1000, photos });
  globalThis.fetch = (url) => scene.fetch(String(url)).then((r) => ({
    status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  const mod = await require(path.join(dir, loader))({ print: () => {}, printErr: () => {} });
  for (const [k, v] of Object.entries(BEND)) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  mod._web_start();
  let label = null;
  for (let k = 0; k < 600 && !label; k++) { await frames(mod, 1); label = texts(mod).find((t) => t.s === CS && t.sx === 1); }
  if (!label) instrument(`${CS}'s label was never drawn on the radar in 600 frames`);
  const photoReqs = () => scene.log.filter((e) => new URL(e.url, "https://page.invalid/").pathname.startsWith(PREFIX));
  const before = photoReqs().length;
  mod._web_touch(1, label.x, label.y); await frames(mod, 3);
  mod._web_touch(0, label.x, label.y);
  return { mod, scene, photoReqs, before };
}

async function tap(mod, x, y) { mod._web_touch(1, x, y); await frames(mod, 3); mod._web_touch(0, x, y); await frames(mod, 60); }

let failures = 0;
const row = (id, ok, text) => { if (!ok) failures++; console.log(`  ${ok ? "ok  " : "FAIL"} ${id} ${text}`); };
try {
  const pick = await chooseOffset();
  if (!pick) instrument(`${CS} is never within 30 km of the centre in the scene`);
  const probe = await require(path.join(dir, loader))({ print: () => {}, printErr: () => {} });
  const size = probe._web_screen_size();
  const table = JSON.parse(fs.readFileSync(path.join(HERE, "fixtures", "photo-decode.json"), "utf8"));
  const fx = table[`photo-${size}.jpg`];
  if (!fx) instrument(`no fixture at this model's size (${size} px): fixtures/photo-${size}.jpg`);
  const bytes = new Uint8Array(fs.readFileSync(path.join(HERE, "fixtures", `photo-${size}.jpg`)));
  if (crypto.createHash("sha256").update(bytes).digest("hex") !== fx.jpeg_sha256) instrument(`fixtures/photo-${size}.jpg is not the file photo-decode.json names`);
  const p = `${PREFIX}cardphoto-${fx.jpeg_sha256.slice(0, 12)}`;
  console.log(`card_photo: ${path.basename(dir)} (${size} px); scene clock +${pick.T} s; the photo: fixtures/photo-${size}.jpg ` +
              `(${bytes.length} B, host RGB565 ${fx.rgb565_sha256.slice(0, 12)}) at ${p}`);

  // ---- the card with its photo ----
  const A = await openCard(pick, { [HEX]: { path: p, bytes } });
  let r = [];
  const dts = [];   // W7, REPORTED: each frame's time while the photo arrives; the decode runs on the loop, in one of them
  for (let k = 0; k < 600 && !r.length; k++) {
    const t0 = performance.now(); A.mod._web_frame(); dts.push(performance.now() - t0);
    await sleep(16); r = rasters(A.mod);
  }
  await frames(A.mod, 10);
  r = rasters(A.mod);
  const t = texts(A.mod);
  if (!t.some((o) => o.s === CS && o.sx === 2)) instrument(`CP0: the card for ${CS} is not open (no title at size 2)`);
  console.log(`  ok   CP0 the card for ${CS} is open`);
  const reqs = A.photoReqs().map((e) => new URL(e.url, "https://page.invalid/").pathname);
  row("CP1", A.before === 0 && reqs.length === 1 && reqs[0] === p,
      `photo requests: ${A.before} before the tap, ${reqs.length} after (${reqs.join(", ") || "none"}); want 0, then exactly 1 to ${p}`);
  let cp2 = r.length === 1 && r[0].w === size && r[0].h === size && r[0].x === 0 && r[0].y === 0, why = `${r.length} raster(s)`;
  if (r.length === 1) {
    const ptr = typeof A.mod._web_raster_ptr === "function" ? A.mod._web_raster_ptr(r[0].id) : 0;
    const px = ptr ? A.mod.HEAPU8.slice(ptr, ptr + size * size * 2) : new Uint8Array(0);
    const sha = crypto.createHash("sha256").update(px).digest("hex"), own = fnv(px);
    cp2 = cp2 && sha === fx.rgb565_sha256 && own.lo === r[0].lo && own.hi === r[0].hi;
    why = `one ${r[0].w} x ${r[0].h} at (${r[0].x}, ${r[0].y}); pixels sha256 ${sha.slice(0, 12)} (host ${fx.rgb565_sha256.slice(0, 12)}); ` +
          `the list's hash is the pixels' own: ${own.lo === r[0].lo && own.hi === r[0].hi}`;
  }
  row("CP2", cp2, why);
  const sorted = [...dts].sort((x, y) => x - y), med = sorted.length ? sorted[sorted.length >> 1] : 0;
  console.log(`  REPORTED W7 the slowest frame while the photo arrived (it holds the decode): ${(sorted.at(-1) || 0).toFixed(1)} ms; ` +
              `median ${med.toFixed(1)} ms over ${dts.length} frame(s) (not graded)`);
  const s1 = shown(A.mod);
  await tap(A.mod, Math.round(size / 2), Math.round(size * 0.3));
  const s2 = shown(A.mod), flipped = rasters(A.mod).length;
  await tap(A.mod, Math.round(size / 2), Math.round(size * 0.3));
  const s3 = shown(A.mod), closed = !texts(A.mod).some((o) => o.s === CS && o.sx === 2);
  row("CP3", s1 === p && s2 === "" && flipped === 0 && s3 === "" && closed,
      `web_photo_shown(): ${JSON.stringify(s1)} with the photo drawn; ${JSON.stringify(s2)} after the flip (rasters ${flipped}); ` +
      `${JSON.stringify(s3)} after the close (card closed: ${closed})`);

  // ---- CP4, the control: the same card, enrich's p null ----
  const B = await openCard(pick, null);
  await frames(B.mod, 120);
  const tb = texts(B.mod), noPhoto = tb.some((o) => o.s === "No photo available");
  if (!tb.some((o) => o.s === CS && o.sx === 2)) instrument(`CP4: the control's card for ${CS} is not open`);
  row("CP4", B.photoReqs().length === 0 && rasters(B.mod).length === 0 && noPhoto,
      `p null: ${B.photoReqs().length} photo request(s), ${rasters(B.mod).length} raster(s), "No photo available" drawn: ${noPhoto}`);
} catch (e) {
  instrument(e && e.stack ? e.stack.split("\n")[0] : String(e));
}
console.log(failures ? `CARD PHOTO: ${failures} row(s) off` : "CARD PHOTO: every row holds");
process.exit(failures ? 1 : 0);
