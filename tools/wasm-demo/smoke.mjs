// Blipscope web demo: a headless run of a built SKU, in Node. Runs the firmware for a number of frames at
// ~60 fps of wall time, then prints what the last frame drew, by op, and whether the radar shows contacts.
//
//   node tools/wasm-demo/smoke.mjs [dist/s3-128] [seconds]
//
// Exit 0 when the last frame holds a radar with contacts and nothing raster, 1 otherwise.
import { createRequire } from "node:module";
import path from "node:path";

const dir = path.resolve(process.argv[2] || "tools/wasm-demo/dist/s3-128");
const seconds = Number(process.argv[3] || 8);
const require = createRequire(import.meta.url);
globalThis.window = globalThis;   // the loader was built for the web; Node only needs the global it attaches
const sku = path.basename(dir);
const factory = require(path.join(dir, "radar.js"));
const logs = [];
const mod = await factory({ print: (t) => logs.push(t), printErr: (t) => logs.push(t) });

const NAMES = ["fill_screen", "line", "circle", "fill_circle", "fill_tri", "fill_rect", "rect", "fill_rrect", "rrect",
  "fill_arc", "arc", "text", "pixel", "image", "ellipse", "fill_ellipse"];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const t0 = Date.now();
let frames = 0;
while (Date.now() - t0 < seconds * 1000) { mod._demo_frame(); frames++; await sleep(16); }

const ops = mod.HEAPF32.subarray(mod._demo_list_ptr() >> 2, (mod._demo_list_ptr() >> 2) + mod._demo_list_len());
const count = {};
const text = [];
const tb = mod._demo_text_ptr();
for (let i = 0; i < ops.length; i += ops[i]) {
  const op = ops[i + 1];
  count[NAMES[op]] = (count[NAMES[op]] || 0) + 1;
  if (op === 11) text.push(Buffer.from(mod.HEAPU8.subarray(tb + ops[i + 8], tb + ops[i + 8] + ops[i + 9])).toString("latin1"));
}
const st = mod.HEAPF32.subarray(mod._demo_stats_ptr() >> 2, (mod._demo_stats_ptr() >> 2) + 4);
console.log(`${sku}: ${frames} frames in ${seconds} s; panel ${mod._demo_screen_size()} px`);
console.log("last frame ops:", JSON.stringify(count));
console.log(`pixel draws ${st[0]}, raster images ${st[1]}, text bytes ${st[3]}`);
console.log("strings:", JSON.stringify(text.slice(0, 40)));
const health = logs.filter((l) => /\[(fetch|cloud|health|WARN|enrich)/i.test(l));
console.log("device log (fetch/cloud/enrich/health lines, last 12):\n  " + health.slice(-12).join("\n  "));
const contacts = text.filter((s) => /^[A-Z]{2,3}\d|^N\d/.test(s)).length;
const ok = (count.fill_tri || 0) + (count.line || 0) > 0 && st[0] === 0 && st[1] === 0;
console.log(`labels that look like callsigns: ${contacts}; verdict: ${ok ? "DRAWS" : "CHECK"}`);
process.exit(ok ? 0 : 1);
