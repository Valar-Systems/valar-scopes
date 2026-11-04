// Blipscope web demo: replays the radar's display list (shim/LovyanGFX.hpp) on a canvas at the glass's own
// resolution. Coordinates arrive in panel pixels (0..SCREEN_SIZE); everything is scaled once by the
// context transform, so a 1 px line is a 1-panel-pixel-wide vector stroke at any size, never a bitmap.
"use strict";

const OP = { FILL_SCREEN: 0, LINE: 1, CIRCLE: 2, FILL_CIRCLE: 3, FILL_TRI: 4, FILL_RECT: 5, RECT: 6, FILL_RRECT: 7,
  RRECT: 8, FILL_ARC: 9, ARC: 10, TEXT: 11, PIXEL: 12, IMAGE: 13, ELLIPSE: 14, FILL_ELLIPSE: 15 };

function hex(c) { return "#" + (c >>> 0).toString(16).padStart(6, "0"); }
function rad(deg) { return deg * Math.PI / 180; }

export class Renderer {
  constructor(canvas, mod, opts) {
    this.c = canvas; this.m = mod; this.opts = opts;
    this.size = mod._demo_screen_size();
    this.font = mod.HEAPU8.slice(mod._demo_font_ptr(), mod._demo_font_ptr() + 256 * 5);   // glcd Font0
    this.ctx = canvas.getContext("2d");
    this.resize();
  }

  resize() {
    const r = this.c.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    this.px = Math.max(1, Math.round(r.width * dpr));
    this.c.width = this.c.height = this.px;
    this.k = this.px / this.size;            // backing px per panel px
  }

  draw() {
    const m = this.m, ctx = this.ctx, k = this.k, S = this.size;
    const ops = m.HEAPF32.subarray(m._demo_list_ptr() >> 2, (m._demo_list_ptr() >> 2) + m._demo_list_len());
    const textBase = m._demo_text_ptr();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, this.px, this.px);
    ctx.save();
    ctx.setTransform(k, 0, 0, k, 0, 0);
    // The glass is round: nothing outside the disc is ever visible on the panel.
    ctx.beginPath(); ctx.arc(S / 2, S / 2, S / 2, 0, 2 * Math.PI); ctx.clip();
    ctx.lineWidth = 1;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    const hair = 1 / k;                      // one backing pixel, to close the seams between fills
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
    const smooth = this.opts.smoothText;
    if (smooth) {
      ctx.font = `600 ${10 * sy}px "JetBrains Mono", ui-monospace, "SF Mono", Menlo, Consolas, monospace`;
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
