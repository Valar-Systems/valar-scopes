// AF3 (docs/web-kiosk-airfield.md): the venue's own field, in Node, against a built model.
//
//   node tools/web-renderer/airports_remote.mjs [dist/s3-128]
//
// Read from the frames the firmware itself drew, never from the code's intent:
//   AF3          with "airports-remote" = "true", the page's fetch hook answers /airports with one EXAMPLE field, XFLD,
//                about 10 km from the configured location: the drawn text holds "XFLD"
//   AF3 CONTROL  the same page with the key unset: no /airports request reaches the hook, and "XFLD" is never drawn
// Exit 0 when both hold, 1 when one does not, 2 when the instrument could not run.
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
import { makeScene } from "./site/scene.js";

const dir = path.resolve(process.argv.slice(2).find((a) => !a.startsWith("--")) || "tools/web-renderer/dist/s3-128");
const require = createRequire(import.meta.url);
const loader = fs.readdirSync(dir).find((f) => /^blipscope-web-.*\.js$/.test(f));
if (!loader) { console.log(`INSTRUMENT: no blipscope-web-*.js in ${dir}`); process.exit(2); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const BEND = { latitude: "44.0582", longitude: "-121.3153", radius: "40", "radius-unit": "km", airports: "true" };
const FIELD = JSON.stringify({ v: 1, a: [[44.1, -121.2, "XFLD", "S"]] });   // EXAMPLE: not a real airport code
let failures = 0;
const check = (id, ok, what) => { if (!ok) failures++; console.log(`  ${ok ? "ok  " : "FAIL"} ${id} ${what}`); };

async function boot(config) {
  const scene = makeScene({ enrich: true });
  const seen = { airports: 0 };
  globalThis.fetch = (url) => {
    const u = String(url);
    if (new URL(u, "https://page.invalid").pathname.endsWith("/api/v1/blipscope/airports")) {
      seen.airports++;
      const body = Buffer.from(FIELD);
      return Promise.resolve({ status: 200, arrayBuffer: async () => body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength) });
    }
    return scene.fetch(u).then((r) => ({
      status: r.status, arrayBuffer: async () => r.body.buffer.slice(r.body.byteOffset, r.body.byteOffset + r.body.byteLength) }));
  };
  delete require.cache[path.join(dir, loader)];
  const mod = await require(path.join(dir, loader))({ print: () => {}, printErr: () => {} });
  for (const [k, v] of Object.entries(config)) mod.ccall("web_config_set", null, ["string", "string"], [k, v]);
  mod.ccall("web_set_api_base", null, ["string"], [""]);
  mod._web_start();
  return { mod, seen };
}

// Every string the last presented frame drew.
function text(mod) {
  const ops = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
  const tb = mod._web_text_ptr(), out = [];
  for (let i = 0; i < ops.length; i += ops[i]) {
    if (ops[i + 1] === 11) out.push(Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1"));
  }
  return out;
}

// Frames until XFLD is drawn, or the bound: 12 s, well past the first fetch after boot.
async function watch(mod, frames = 720) {
  let drawn = false;
  for (let i = 0; i < frames; i++) {
    mod._web_frame();
    await sleep(16);
    if (i % 30 === 29 && text(mod).includes("XFLD")) { drawn = true; break; }
  }
  return drawn;
}

try {
  const on = await boot({ ...BEND, "airports-remote": "true" });
  const drawnOn = await watch(on.mod);
  check("AF3", on.seen.airports >= 1 && drawnOn,
    `key on: /airports reached the hook ${on.seen.airports} time(s); XFLD drawn ${drawnOn}`);
  const off = await boot({ ...BEND });
  const drawnOff = await watch(off.mod);
  check("AF3 CONTROL", off.seen.airports === 0 && !drawnOff,
    `key unset: /airports reached the hook ${off.seen.airports} time(s); XFLD drawn ${drawnOff}`);
} catch (e) {
  console.log(`INSTRUMENT: ${e && e.message ? e.message : e}`);
  process.exit(2);
}
console.log(failures ? `AF3: ${failures} check(s) off` : "AF3: as predicted");
process.exit(failures ? 1 : 0);
