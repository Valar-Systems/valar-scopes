#!/usr/bin/env node
// Do the print cards still fit? Measured per element, because the obvious
// measurements cannot fail.
//
// WHY THIS EXISTS, AND WHY IT IS SHAPED LIKE THIS. The cards are 4x6in with
// `overflow:hidden`, so copy that does not fit is not wrong on screen -- it is
// ABSENT, and absence on a printed card is invisible by construction. On
// 2026-09-18 a rewrite of the alerts block was checked twice and passed twice,
// and neither check could have failed:
//
//   1. "max bottom over every descendant" -- dominated by a full-height frame
//      div, so it reported the SAME number (549.1) before and after a change
//      that moved sixteen rows. It only budged when a deliberate 40-row
//      sabotage blew past the frame itself.
//   2. "bottom of the last text element" -- that is the PINNED FOOTER, which
//      by definition never moves.
//
// Both answered "no overflow". Both were true and both were worthless: a
// statistic that cannot move cannot fail, and a check that cannot fail is
// indistinguishable from a card that is correct.
//
// So this compares EVERY leaf text element against its own card's box and
// reports each one that escapes. The selftest plants an overflowing row and
// requires it to be caught, so the failing direction is proven rather than
// assumed.
//
// RUN:  node scripts/check-print-cards.mjs [--selftest]
// EXIT: 0 everything fits - 1 something is clipped - 2 the rig is broken

import { readFileSync, writeFileSync, mkdtempSync, existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join } from "node:path";

const CARDS = [
  "docs/blipscope-features-card.html",
  "docs/blipscope-features-card-PRINT-TEST.html",
  "docs/blipscope-quickstart-card.html",
  "docs/blipscope-quickstart-card-PRINT-TEST.html",
];

// Chrome is FOUND, not assumed -- and when it is missing this reports the RIG
// as incomplete rather than printing a pass, because "no overflow found" from a
// browser that never ran is the most reassuring possible way to be blind.
const CHROME_CANDIDATES = [
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
  "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
  "/usr/bin/google-chrome",
  "/usr/bin/chromium",
  "/usr/bin/chromium-browser",
];

const PROBE = `
<script>
window.addEventListener('load', function () {
  const out = [];
  document.querySelectorAll('.card').forEach(function (card, i) {
    const cb = card.getBoundingClientRect();
    card.querySelectorAll('*').forEach(function (el) {
      if (el.children.length) return;              // leaf elements only
      const t = (el.textContent || '').trim();
      if (!t) return;
      const r = el.getBoundingClientRect();
      if (r.height === 0 && r.width === 0) return;
      out.push({
        card: i,
        bottom: +(r.bottom - cb.top).toFixed(1),
        right: +(r.right - cb.left).toFixed(1),
        cardH: +cb.height.toFixed(1),
        cardW: +cb.width.toFixed(1),
        text: t.slice(0, 44)
      });
    });
  });
  const pre = document.createElement('pre');
  pre.id = 'cardreport';
  pre.textContent = JSON.stringify(out);
  pre.style.cssText = 'position:fixed;left:-99999px;top:0';
  document.body.appendChild(pre);
});
</script>`;

function findChrome() {
  for (const c of CHROME_CANDIDATES) if (existsSync(c)) return c;
  return null;
}

function measure(chrome, html, tag) {
  const dir = mkdtempSync(join(tmpdir(), "cardcheck-"));
  const f = join(dir, tag + ".html");
  const injected = html.includes("</body>")
    ? html.replace("</body>", PROBE + "\n</body>")
    : html + PROBE;
  if (injected === html) throw new Error("probe was not injected into " + tag);
  writeFileSync(f, injected, "utf8");
  const dom = execFileSync(chrome, [
    "--headless", "--disable-gpu", "--no-sandbox", "--hide-scrollbars",
    "--virtual-time-budget=4000", "--dump-dom",
    "file:///" + f.replace(/\\/g, "/"),
  ], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024, stdio: ["ignore", "pipe", "ignore"] });
  const m = dom.match(/<pre id="cardreport"[^>]*>([\s\S]*?)<\/pre>/);
  if (!m) return null;                              // null means BLIND, not clean
  const json = m[1].replace(/&quot;/g, '"').replace(/&amp;/g, "&")
                   .replace(/&lt;/g, "<").replace(/&gt;/g, ">");
  return JSON.parse(json);
}

// 0.5px of tolerance: sub-pixel rounding is not a clipped line.
const escapees = (rows) =>
  rows.filter((r) => r.bottom > r.cardH + 0.5 || r.right > r.cardW + 0.5);

function main() {
  const selftest = process.argv.includes("--selftest");
  const chrome = findChrome();
  if (!chrome) {
    console.error("FAIL: no Chrome/Edge found, so the cards cannot be measured.");
    console.error("      Reporting the RIG as incomplete rather than as a pass.");
    return 2;
  }
  console.log("browser: " + chrome);

  if (selftest) {
    // THE CONTROL. Plant a row that cannot fit and require it to be caught --
    // otherwise this file is the third check in a row that cannot fail.
    const src = readFileSync(CARDS[0], "utf8");
    const anchor = '<div class="b-head">';
    if (!src.includes(anchor)) { console.error("FAIL: selftest anchor missing"); return 2; }
    const filler = '<div class="feat"><div class="k">XX</div><div class="v">'
                 + "overflow test ".repeat(8) + "</div></div>\n";
    const broken = src.replace(anchor, filler.repeat(40) + anchor);
    if (broken === src) { console.error("FAIL: sabotage did not apply"); return 2; }

    const clean = measure(chrome, src, "clean");
    const dirty = measure(chrome, broken, "dirty");
    if (!clean || !dirty) { console.error("FAIL: probe did not run; BLIND"); return 2; }

    let rc = 0;
    if (escapees(clean).length === 0) console.log("  ok    the real card reports no escapees");
    else { console.log("  FAIL  the real card already overflows"); rc = 1; }

    const d = escapees(dirty).length;
    if (d > 0) console.log(`  ok    a planted overflow IS detected (${d} escapee(s)) -- it can fail`);
    else { console.log("  FAIL  a planted overflow was NOT detected. This check is worthless."); rc = 2; }

    console.log(rc === 0 ? "SELFTEST PASSED" : "SELFTEST FAILED");
    return rc;
  }

  let rc = 0;
  for (const path of CARDS) {
    if (!existsSync(path)) { console.log(`  skip  ${path} (absent)`); continue; }
    const rows = measure(chrome, readFileSync(path, "utf8"), "c");
    if (!rows) {
      console.error(`  FAIL  ${path}: the probe did not run -- BLIND, not clean`);
      rc = 2; continue;
    }
    const bad = escapees(rows);
    if (bad.length === 0) {
      // Report the tightest margin, so a card creeping toward the edge is
      // visible BEFORE it clips rather than on the day it does.
      const worst = rows.reduce((a, b) => (b.cardH - b.bottom < a.cardH - a.bottom ? b : a));
      console.log(`  ok    ${path}  (tightest margin ${(worst.cardH - worst.bottom).toFixed(1)}px: ${JSON.stringify(worst.text)})`);
    } else {
      console.log(`  FAIL  ${path}: ${bad.length} element(s) clipped by the card edge`);
      for (const e of bad.slice(0, 6)) {
        console.log(`          +${(e.bottom - e.cardH).toFixed(1)}px below  ${JSON.stringify(e.text)}`);
      }
      rc = 1;
    }
  }
  console.log(rc === 0 ? "\nAll print cards fit." : "\nPRINT CARDS FAILED");
  return rc;
}

process.exit(main());
