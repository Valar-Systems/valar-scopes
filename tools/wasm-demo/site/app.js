// Blipscope web demo: loads a SKU's firmware module, runs one firmware frame per animation frame, replays it.
import { Renderer } from "./render.js";

const $ = (id) => document.getElementById(id);
const canvas = $("radar");
const opts = { smoothText: false, dim: false };
const loaded = {};          // sku -> { mod, renderer }
let active = null;
let pointerDown = false;

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src; s.onload = resolve; s.onerror = () => reject(new Error("could not load " + src));
    document.head.appendChild(s);
  });
}

async function load(sku) {
  if (loaded[sku]) return loaded[sku];
  $("status").textContent = `Loading the ${sku} firmware…`;
  await loadScript(`${sku}/radar.js`);
  const factory = window["BlipscopeRadar_" + sku.replace(/-/g, "_")];
  const mod = await factory({
    locateFile: (f) => `${sku}/${f}`,
    print: (t) => console.log(t),
    printErr: (t) => console.warn(t),
  });
  loaded[sku] = { mod, renderer: new Renderer(canvas, mod, opts) };
  return loaded[sku];
}

async function select(sku) {
  for (const s of ["s3-128", "s3-21"]) $("sku-" + s).setAttribute("aria-pressed", String(s === sku));
  try { localStorage.setItem("blipscope-demo-sku", sku); } catch (e) { /* storage unavailable: fine */ }
  try {
    active = await load(sku);
    active.renderer.resize();
    const n = active.mod._demo_screen_size();
    $("status").textContent = `Running the ${sku} build: ${n} × ${n} panel`;
  } catch (e) {
    $("status").textContent = "The firmware did not load: " + e.message;
  }
}

// The firmware polls the touch chip once per loop (tft.getTouch). A quick mouse click can press and release
// between two frames, which no real finger does, so a press is held for at least MIN_HOLD frames before its
// release is passed on; the position keeps following the pointer meanwhile.
const MIN_HOLD = 3;
const touch = { down: false, x: 0, y: 0, heldFrames: 0, releasePending: false };
function toPanel(ev) {
  const r = canvas.getBoundingClientRect();
  const n = active.mod._demo_screen_size();
  return [Math.round(((ev.clientX - r.left) / r.width) * n), Math.round(((ev.clientY - r.top) / r.height) * n)];
}
canvas.addEventListener("pointerdown", (ev) => {
  if (!active) return;
  pointerDown = true; canvas.setPointerCapture(ev.pointerId);
  [touch.x, touch.y] = toPanel(ev);
  touch.down = true; touch.heldFrames = 0; touch.releasePending = false;
  ev.preventDefault();
});
canvas.addEventListener("pointermove", (ev) => {
  if (!active || !pointerDown) return;
  [touch.x, touch.y] = toPanel(ev);
});
for (const t of ["pointerup", "pointercancel"]) {
  canvas.addEventListener(t, (ev) => {
    if (!active || !pointerDown) return;
    pointerDown = false;
    [touch.x, touch.y] = toPanel(ev);
    touch.releasePending = true;
  });
}
function applyTouch() {
  if (touch.down && touch.releasePending && touch.heldFrames >= MIN_HOLD) { touch.down = false; touch.releasePending = false; }
  active.mod._demo_touch(touch.down ? 1 : 0, touch.x, touch.y);
  if (touch.down) touch.heldFrames++;
}
canvas.addEventListener("contextmenu", (ev) => ev.preventDefault());

function setText(smooth) {
  opts.smoothText = smooth;
  $("text-device").setAttribute("aria-pressed", String(!smooth));
  $("text-smooth").setAttribute("aria-pressed", String(smooth));
}
$("text-device").addEventListener("click", () => setText(false));
$("text-smooth").addEventListener("click", () => setText(true));
$("sku-s3-128").addEventListener("click", () => select("s3-128"));
$("sku-s3-21").addEventListener("click", () => select("s3-21"));
$("dim").addEventListener("change", (e) => { opts.dim = e.target.checked; });
$("showstats").addEventListener("change", (e) => { $("stats").hidden = !e.target.checked; });
window.addEventListener("resize", () => { if (active) active.renderer.resize(); });

// A test hook: pause the animation loop and step frames by hand (a throttled or headless browser runs
// requestAnimationFrame far below the panel's rate, which turns every tap into a long press).
let paused = false;
window.blipscopeDemo = {
  active: () => active,
  pause(p) { paused = !!p; },
  step(n = 1, down = null, x = 0, y = 0) {
    for (let i = 0; i < n; i++) { if (down !== null) active.mod._demo_touch(down ? 1 : 0, x, y); active.mod._demo_frame(); }
    active.renderer.draw();
  },
};

let lastStats = 0, frames = 0, msSum = 0;
function tick(now) {
  if (active && !paused) {
    const t0 = performance.now();
    applyTouch();
    active.mod._demo_frame();
    active.renderer.draw();
    msSum += performance.now() - t0; frames++;
    canvas.style.filter = opts.dim ? `brightness(${Math.max(0.08, active.mod._demo_brightness() / 255)})` : "";
    if (now - lastStats > 500) {
      const s = active.mod.HEAPF32.subarray(active.mod._demo_stats_ptr() >> 2, (active.mod._demo_stats_ptr() >> 2) + 4);
      $("st-ops").textContent = Math.round(countOps(active.mod)).toLocaleString();
      $("st-text").textContent = s[3].toLocaleString();
      $("st-px").textContent = s[0].toLocaleString();
      $("st-img").textContent = s[1].toLocaleString();
      $("st-ms").textContent = `${(msSum / frames).toFixed(1)} ms firmware + draw`;
      lastStats = now; frames = 0; msSum = 0;
    }
  }
  requestAnimationFrame(tick);
}
function countOps(mod) {
  const ops = mod.HEAPF32.subarray(mod._demo_list_ptr() >> 2, (mod._demo_list_ptr() >> 2) + mod._demo_list_len());
  let n = 0;
  for (let i = 0; i < ops.length; i += ops[i]) n++;
  return n;
}

let first = "s3-128";
try { first = localStorage.getItem("blipscope-demo-sku") || first; } catch (e) { /* fine */ }
select(first === "s3-21" ? "s3-21" : "s3-128");
requestAnimationFrame(tick);
