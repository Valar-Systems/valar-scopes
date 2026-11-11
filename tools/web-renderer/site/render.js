// Blipscope web renderer (docs/web-renderer.md): mounts the radar firmware, compiled to WebAssembly, on a canvas.
//
//   import { mount } from "./blipscope-web-render-v19.js";
//   const radar = await mount(canvas, {
//     loaderUrl: "./blipscope-web-s3-128-v19.js",     // the Emscripten loader for one model
//     wasmUrl:   "./blipscope-web-s3-128-v19.wasm",
//     fontUrl:   "./blipscope-web-mono-v19.woff2",     // optional: enables textMode "smooth"
//     config:    { latitude: "44.0582", longitude: "-121.3153", radius: "40", "radius-unit": "km" },
//     apiBase:   "",                                   // where /api/v1/blipscope/... is served; "" = this origin
//   });
//
// The firmware runs its own fetch task: GET <apiBase>/api/v1/blipscope/blips?lat=..&lon=..&r=..&limit=40 at its
// own cadence, and /api/v1/blipscope/enrich/<hex> for labels. Nothing else is requested, and no request carries a
// header. Every draw call arrives as a vector command in panel pixels and is replayed here at the canvas's
// backing resolution (CSS px x devicePixelRatio).
"use strict";

const OP = { FILL_SCREEN: 0, LINE: 1, CIRCLE: 2, FILL_CIRCLE: 3, FILL_TRI: 4, FILL_RECT: 5, RECT: 6, FILL_RRECT: 7,
  RRECT: 8, FILL_ARC: 9, ARC: 10, TEXT: 11, PIXEL: 12, IMAGE: 13, ELLIPSE: 14, FILL_ELLIPSE: 15 };
const FONT_FAMILY = "BlipscopeMono";
const MIN_HOLD_FRAMES = 3;   // a press is reported for at least this many frames: a click can be shorter than one

function hex(c) { return "#" + (c >>> 0).toString(16).padStart(6, "0"); }
function rad(deg) { return deg * Math.PI / 180; }

export class Renderer {
  constructor(canvas, mod, opts) {
    this.c = canvas; this.m = mod; this.opts = opts;
    this.size = mod._web_screen_size();
    this.font = mod.HEAPU8.slice(mod._web_font_ptr(), mod._web_font_ptr() + 256 * 5);   // LovyanGFX's Font0
    this.ctx = canvas.getContext("2d");
    this.resize();
  }

  resize() {
    const r = this.c.getBoundingClientRect();
    const dpr = (typeof window !== "undefined" && window.devicePixelRatio) || 1;
    this.px = Math.max(1, Math.round((r.width || this.size) * dpr));
    this.c.width = this.c.height = this.px;
    this.k = this.px / this.size;            // backing px per panel px
  }

  draw() {
    const m = this.m, ctx = this.ctx, k = this.k, S = this.size;
    const ops = m.HEAPF32.subarray(m._web_list_ptr() >> 2, (m._web_list_ptr() >> 2) + m._web_list_len());
    const textBase = m._web_text_ptr();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.px, this.px);
    ctx.save();
    ctx.setTransform(k, 0, 0, k, 0, 0);
    // The glass is round: nothing outside the disc is ever visible on the panel.
    ctx.beginPath(); ctx.arc(S / 2, S / 2, S / 2, 0, 2 * Math.PI); ctx.clip();
    ctx.lineWidth = 1;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    const hair = 1 / k;                      // one backing pixel, to close the seams between adjacent fills
    for (let i = 0; i < ops.length;) {
      const len = ops[i], op = ops[i + 1], col = hex(ops[i + 2]);
      const a = i + 3;
      switch (op) {
        case OP.FILL_SCREEN: ctx.fillStyle = col; ctx.fillRect(0, 0, S, S); break;
        case OP.LINE:
          ctx.strokeStyle = col; ctx.beginPath();
          ctx.moveTo(ops[a] + 0.5, ops[a + 1] + 0.5); ctx.lineTo(ops[a + 2] + 0.5, ops[a + 3] + 0.5); ctx.stroke(); break;
        case OP.CIRCLE:
          ctx.strokeStyle = col; ctx.beginPath(); ctx.arc(ops[a] + 0.5, ops[a + 1] + 0.5, ops[a + 2], 0, 2 * Math.PI); ctx.stroke(); break;
        case OP.FILL_CIRCLE:
          ctx.fillStyle = col; ctx.beginPath(); ctx.arc(ops[a] + 0.5, ops[a + 1] + 0.5, ops[a + 2] + 0.5, 0, 2 * Math.PI); ctx.fill(); break;
        case OP.FILL_TRI:
          ctx.fillStyle = col; ctx.strokeStyle = col; ctx.lineWidth = hair; ctx.beginPath();
          ctx.moveTo(ops[a] + 0.5, ops[a + 1] + 0.5); ctx.lineTo(ops[a + 2] + 0.5, ops[a + 3] + 0.5);
          ctx.lineTo(ops[a + 4] + 0.5, ops[a + 5] + 0.5); ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.lineWidth = 1; break;
        case OP.FILL_RECT: ctx.fillStyle = col; ctx.fillRect(ops[a], ops[a + 1], ops[a + 2], ops[a + 3]); break;
        case OP.PIXEL: ctx.fillStyle = col; ctx.fillRect(ops[a], ops[a + 1], 1, 1); break;
        case OP.RECT: ctx.strokeStyle = col; ctx.strokeRect(ops[a] + 0.5, ops[a + 1] + 0.5, ops[a + 2] - 1, ops[a + 3] - 1); break;
        case OP.FILL_RRECT:
          ctx.fillStyle = col; ctx.beginPath(); ctx.roundRect(ops[a], ops[a + 1], ops[a + 2], ops[a + 3], ops[a + 4]); ctx.fill(); break;
        case OP.RRECT:
          ctx.strokeStyle = col; ctx.beginPath();
          ctx.roundRect(ops[a] + 0.5, ops[a + 1] + 0.5, ops[a + 2] - 1, ops[a + 3] - 1, Math.max(0, ops[a + 4] - 0.5)); ctx.stroke(); break;
        case OP.FILL_ARC: case OP.ARC: {
          // LovyanGFX: degrees, 0 at 3 o'clock, clockwise -- the canvas's own convention with y down.
          const x = ops[a] + 0.5, y = ops[a + 1] + 0.5;
          const ro = Math.max(ops[a + 2], ops[a + 3]) + 0.5, ri = Math.max(0, Math.min(ops[a + 2], ops[a + 3]) - 0.5);
          ctx.beginPath(); ctx.arc(x, y, ro, rad(ops[a + 4]), rad(ops[a + 5])); ctx.arc(x, y, ri, rad(ops[a + 5]), rad(ops[a + 4]), true);
          ctx.closePath();
          if (op === OP.FILL_ARC) { ctx.fillStyle = col; ctx.fill(); } else { ctx.strokeStyle = col; ctx.stroke(); }
          break;
        }
        case OP.ELLIPSE: case OP.FILL_ELLIPSE:
          ctx.beginPath(); ctx.ellipse(ops[a] + 0.5, ops[a + 1] + 0.5, ops[a + 2] + 0.5, ops[a + 3] + 0.5, 0, 0, 2 * Math.PI);
          if (op === OP.FILL_ELLIPSE) { ctx.fillStyle = col; ctx.fill(); } else { ctx.strokeStyle = col; ctx.stroke(); }
          break;
        case OP.IMAGE:
          ctx.fillStyle = "#202020"; ctx.fillRect(ops[a], ops[a + 1], ops[a + 2], ops[a + 3]); break;
        case OP.TEXT: this.text(ctx, ops, i + 2, textBase); break;
      }
      i += len;
    }
    ctx.restore();
  }

  // [fg, bg, x, y, sx, sy, offset, count]
  text(ctx, ops, b, textBase) {
    const fg = hex(ops[b]), bg = ops[b + 1], sx = ops[b + 4], sy = ops[b + 5];
    let x = ops[b + 2]; const y = ops[b + 3];
    const bytes = this.m.HEAPU8.subarray(textBase + ops[b + 6], textBase + ops[b + 6] + ops[b + 7]);
    const fsx = Math.trunc(65536 * sx), fsy = Math.trunc(65536 * sy);
    const adv = (6 * fsx) >> 16, cellH = (8 * fsy) >> 16;
    const smooth = this.opts.textMode === "smooth" && this.opts.fontReady;
    if (smooth) {
      ctx.font = `600 ${10 * sy}px ${FONT_FAMILY}, ui-monospace, monospace`;
      ctx.textBaseline = "alphabetic";
      ctx.textAlign = "center";
    }
    for (let n = 0; n < bytes.length; n++) {
      let c = bytes[n];
      if (bg >= 0) { ctx.fillStyle = hex(bg); ctx.fillRect(x, y, adv, cellH); }
      ctx.fillStyle = fg;
      if (smooth) {
        ctx.fillText(String.fromCharCode(c), x + (5 * sx) / 2, y + 7 * sy);
      } else {
        // GLCDfont::drawChar, run for run: column i spans [(i*sx)>>16, ((i+1)*sx)>>16), rows likewise.
        if (c >= 176) c++;
        const g = c * 5;
        let x1 = 0;
        for (let i = 0; i < 5; i++) {
          const line = this.font[g + i], x0 = x1;
          x1 = ((i + 1) * fsx) >> 16;
          let j = 0;
          while (j < 8) {
            const on = (line >> j) & 1;
            let j1 = j + 1;
            while (j1 < 8 && ((line >> j1) & 1) === on) j1++;
            if (on) { const y0 = (j * fsy) >> 16, yy = (j1 * fsy) >> 16; ctx.fillRect(x + x0, y + y0, x1 - x0, yy - y0); }
            j = j1;
          }
        }
      }
      x += adv;
    }
    if (smooth) ctx.textAlign = "start";
  }
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src; s.onload = resolve; s.onerror = () => reject(new Error("could not load " + src));
    document.head.appendChild(s);
  });
}

// The model's factory: the loader defines window.BlipscopeRadar_<slug> (s3-128 -> BlipscopeRadar_s3_128).
async function factoryFor(opts) {
  if (opts.factoryName && typeof window[opts.factoryName] === "function") return window[opts.factoryName];
  await loadScript(opts.loaderUrl);
  const name = opts.factoryName || Object.keys(window).find((k) => k.startsWith("BlipscopeRadar_")
    && opts.loaderUrl.includes(k.slice("BlipscopeRadar_".length).replace(/_/g, "-")));
  if (!name || typeof window[name] !== "function") throw new Error("the loader defined no matching BlipscopeRadar_ factory");
  return window[name];
}

// Starts a module: the configuration first (as the config page would have stored it), then setup()'s radar half.
export function start(mod, opts) {
  const set = (k, v) => mod.ccall("web_config_set", null, ["string", "string"], [String(k), String(v)]);
  for (const [k, v] of Object.entries(opts.config || {})) set(k, v);
  mod.ccall("web_set_api_base", null, ["string"], [opts.apiBase || ""]);
  mod._web_start();
}

// What a frame drew and what the network did, for a page's own display or a test.
export function stats(mod) {
  const s = mod.HEAPF32.subarray(mod._web_stats_ptr() >> 2, (mod._web_stats_ptr() >> 2) + 4);
  const n = mod.HEAPU32.subarray(mod._web_net_stats() >> 2, (mod._web_net_stats() >> 2) + 6);
  let ops = 0;
  const l = mod.HEAPF32.subarray(mod._web_list_ptr() >> 2, (mod._web_list_ptr() >> 2) + mod._web_list_len());
  for (let i = 0; i < l.length; i += l[i]) ops++;
  return { ops, textBytes: s[3], pixelDraws: s[0], rasterImages: s[1],
           net: { blips: n[0], enrich: n[1], answeredLocally: n[2], postsDropped: n[3], timeouts: n[4], fromLoop: n[5] } };
}

export async function mount(canvas, options) {
  const opts = { textMode: "device", dim: false, ...options, fontReady: false };
  const factory = await factoryFor(opts);
  const mod = await factory({
    locateFile: (f) => (f.endsWith(".wasm") ? opts.wasmUrl : f),
    blipscopeFetch: opts.fetch,   // optional: a page-supplied answerer (a demo scene); default is a same-origin fetch
    print: opts.log || (() => {}),
    printErr: opts.log || (() => {}),
  });
  if (opts.fontUrl && typeof FontFace !== "undefined") {
    try {
      const face = new FontFace(FONT_FAMILY, `url(${opts.fontUrl})`, { weight: "100 800" });
      await face.load();
      document.fonts.add(face);
      opts.fontReady = true;
    } catch (e) { /* no font: smooth text falls back to the device font */ }
  }
  start(mod, opts);
  const renderer = new Renderer(canvas, mod, opts);

  // Touch: the firmware polls the touch chip once per frame (tft.getTouch).
  const touch = { down: false, x: 0, y: 0, held: 0, release: false };
  let pointerDown = false;
  const toPanel = (ev) => {
    const r = canvas.getBoundingClientRect(), n = renderer.size;
    return [Math.round(((ev.clientX - r.left) / r.width) * n), Math.round(((ev.clientY - r.top) / r.height) * n)];
  };
  const onDown = (ev) => { pointerDown = true; canvas.setPointerCapture(ev.pointerId); [touch.x, touch.y] = toPanel(ev);
    touch.down = true; touch.held = 0; touch.release = false; ev.preventDefault(); };
  const onMove = (ev) => { if (pointerDown) [touch.x, touch.y] = toPanel(ev); };
  const onUp = (ev) => { if (!pointerDown) return; pointerDown = false; [touch.x, touch.y] = toPanel(ev); touch.release = true; };
  const noMenu = (ev) => ev.preventDefault();
  canvas.style.touchAction = "none";
  canvas.addEventListener("pointerdown", onDown);
  canvas.addEventListener("pointermove", onMove);
  canvas.addEventListener("pointerup", onUp);
  canvas.addEventListener("pointercancel", onUp);
  canvas.addEventListener("contextmenu", noMenu);
  const onResize = () => renderer.resize();
  window.addEventListener("resize", onResize);

  let paused = false, alive = true, raf = 0, frameMs = 0;
  const frame = () => {
    if (touch.down && touch.release && touch.held >= MIN_HOLD_FRAMES) { touch.down = false; touch.release = false; }
    mod._web_touch(touch.down ? 1 : 0, touch.x, touch.y);
    if (touch.down) touch.held++;
    const t0 = performance.now();
    mod._web_frame();
    renderer.draw();
    frameMs = frameMs * 0.9 + (performance.now() - t0) * 0.1;
    canvas.style.filter = opts.dim ? `brightness(${Math.max(0.08, mod._web_brightness() / 255)})` : "";
  };
  const tick = () => { if (!alive) return; if (!paused) frame(); raf = requestAnimationFrame(tick); };
  raf = requestAnimationFrame(tick);

  return {
    module: mod,
    renderer,
    setTextMode(mode) { opts.textMode = mode === "smooth" ? "smooth" : "device"; },
    get fontReady() { return opts.fontReady; },
    setDim(on) { opts.dim = !!on; },
    pause(p) { paused = !!p; },
    // Step frames by hand (tests, or a throttled browser): optionally holding a touch at panel (x, y).
    step(n = 1, down = null, x = 0, y = 0) {
      for (let i = 0; i < n; i++) { if (down !== null) mod._web_touch(down ? 1 : 0, x, y); mod._web_frame(); }
      renderer.draw();
    },
    stats() { return { ...stats(mod), frameMs }; },
    destroy() {
      alive = false; cancelAnimationFrame(raf);
      canvas.removeEventListener("pointerdown", onDown); canvas.removeEventListener("pointermove", onMove);
      canvas.removeEventListener("pointerup", onUp); canvas.removeEventListener("pointercancel", onUp);
      canvas.removeEventListener("contextmenu", noMenu); window.removeEventListener("resize", onResize);
    },
  };
}
