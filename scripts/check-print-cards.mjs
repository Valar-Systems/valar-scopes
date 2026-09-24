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
// AND IT WAS NOT IN CI, SO IT CAUGHT NOTHING. Until 2026-09-24 this ran only
// when someone remembered to run it, and the quickstart card's back-face note
// sat 12.7px past the card edge -- its second line never printed -- while this
// script, run on main, would have said FAIL. It now runs as the `print-cards`
// job (.github/workflows/print-cards.yml), and checks three more things:
//
//   SIZE      every .card is exactly 4x6in (384x576 CSS px). A card that grows
//             is not "still fitting", it is a different card.
//   PAGES     the file is printed to PDF and must produce exactly one page per
//             card. A card that no longer fits its page spills onto another.
//   REQUIRED  the wordmark, the QR and the data note must each be present, carry
//             their text, and sit inside their card. Losing one is a card that
//             "fits" because something is missing -- the one failure a fit check
//             is structurally unable to see.
//
// Measurement waits for document.fonts.ready: the cards use web fonts, and a
// measurement taken on the `load` event can describe the fallback layout.
//
// RUN:  node scripts/check-print-cards.mjs [--selftest] [--pdf-out <dir>]
//       --pdf-out also writes print-ready PDFs (the letter-sheet harnesses as
//       they are, and 4x6in pages from the plain card files).
// EXIT: 0 everything fits - 1 something is clipped, resized or missing - 2 the rig is broken

import { readFileSync, writeFileSync, mkdtempSync, existsSync, mkdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import { basename, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const CARDS = [
  "docs/blipscope-features-card.html",
  "docs/blipscope-features-card-PRINT-TEST.html",
  "docs/blipscope-quickstart-card.html",
  "docs/blipscope-quickstart-card-PRINT-TEST.html",
];

const CARD_W = 384, CARD_H = 576; // 4in x 6in at 96 CSS px per inch

// What each card must carry. `card` is the .card index in the file; `text` must
// appear in the element's text (whitespace-normalised).
const NOTE = "Aircraft data comes from volunteer networks we help fund.";
const REQUIRED = {
  quickstart: [
    { what: "wordmark", card: 0, sel: ".wordmark", text: "BLIPSCOPE" },
    { what: "QR", card: 1, sel: ".qr-slot", text: "valarsystems.com" },
    { what: "data note", card: 1, sel: ".note", text: NOTE },
  ],
  features: [
    { what: "wordmark", card: 0, sel: ".wordmark", text: "COLLECTING" },
    { what: "QR", card: 1, sel: ".qr-slot", text: "valarsystems.com" },
  ],
};
const requiredFor = (path) => (path.includes("quickstart") ? REQUIRED.quickstart : REQUIRED.features);

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
document.fonts.ready.then(function () { setTimeout(function () {
  var cards = [], leaves = [], found = [];
  var sels = ${JSON.stringify([...new Set([...REQUIRED.quickstart, ...REQUIRED.features].map((r) => r.sel))])};
  document.querySelectorAll('.card').forEach(function (card, i) {
    var cb = card.getBoundingClientRect();
    cards.push({ w: +cb.width.toFixed(1), h: +cb.height.toFixed(1) });
    card.querySelectorAll('*').forEach(function (el) {
      if (el.children.length) return;              // leaf elements only
      var t = (el.textContent || '').trim();
      if (!t) return;
      var r = el.getBoundingClientRect();
      if (r.height === 0 && r.width === 0) return;
      leaves.push({ card: i, bottom: +(r.bottom - cb.top).toFixed(1), right: +(r.right - cb.left).toFixed(1),
                    cardH: +cb.height.toFixed(1), cardW: +cb.width.toFixed(1), text: t.slice(0, 44) });
    });
    sels.forEach(function (sel) {
      card.querySelectorAll(sel).forEach(function (el) {
        var r = el.getBoundingClientRect();
        found.push({ card: i, sel: sel, top: +(r.top - cb.top).toFixed(1), bottom: +(r.bottom - cb.top).toFixed(1),
                     left: +(r.left - cb.left).toFixed(1), right: +(r.right - cb.left).toFixed(1),
                     text: (el.textContent || '').replace(/\\s+/g, ' ').trim() });
      });
    });
  });
  var pre = document.createElement('pre');
  pre.id = 'cardreport';
  pre.textContent = JSON.stringify({ cards: cards, leaves: leaves, found: found });
  pre.style.cssText = 'position:fixed;left:-99999px;top:0';
  document.body.appendChild(pre);
}, 200); });
</script>`;

function findChrome() {
  for (const c of CHROME_CANDIDATES) if (existsSync(c)) return c;
  return null;
}

// A FRESH PROFILE PER INVOCATION. Measuring (--dump-dom) and then printing
// (--print-to-pdf) back to back on a shared profile handed the second run to the
// first one's still-closing browser, which exited without writing a PDF: every
// page count came back -1. A throwaway --user-data-dir makes each run its own.
const CHROME_FLAGS = ["--headless", "--disable-gpu", "--no-sandbox", "--hide-scrollbars", "--virtual-time-budget=8000"];
const chromeArgs = (...rest) => [...CHROME_FLAGS, "--user-data-dir=" + mkdtempSync(join(tmpdir(), "cardprof-")), ...rest];

function writeTemp(html, tag) {
  const f = join(mkdtempSync(join(tmpdir(), "cardcheck-")), tag + ".html");
  writeFileSync(f, html, "utf8");
  return f;
}

function measure(chrome, html, tag) {
  const injected = html.includes("</body>") ? html.replace("</body>", PROBE + "\n</body>") : html + PROBE;
  if (injected === html) throw new Error("probe was not injected into " + tag);
  const dom = execFileSync(chrome, chromeArgs("--dump-dom", pathToFileURL(writeTemp(injected, tag)).href),
    { encoding: "utf8", maxBuffer: 64 * 1024 * 1024, stdio: ["ignore", "pipe", "ignore"] });
  const m = dom.match(/<pre id="cardreport"[^>]*>([\s\S]*?)<\/pre>/);
  if (!m) return null;                              // null means BLIND, not clean
  const json = m[1].replace(/&quot;/g, '"').replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");
  return JSON.parse(json);
}

// Print to PDF and count pages. Chrome writes one "/Type /Page" object per page.
function printPdf(chrome, html, tag, outPath) {
  const out = outPath ? resolve(outPath) : join(mkdtempSync(join(tmpdir(), "cardpdf-")), tag + ".pdf");
  execFileSync(chrome, chromeArgs("--no-pdf-header-footer", "--print-to-pdf=" + out,
    pathToFileURL(writeTemp(html, tag)).href), { stdio: ["ignore", "ignore", "ignore"] });
  if (!existsSync(out)) return { pages: -1, out };
  const pdf = readFileSync(out).toString("latin1");
  return { pages: (pdf.match(/\/Type\s*\/Page(?!s)/g) || []).length, out };
}

// 0.5px of tolerance: sub-pixel rounding is not a clipped line.
const escapees = (rep) => rep.leaves.filter((r) => r.bottom > r.cardH + 0.5 || r.right > r.cardW + 0.5);

// Every problem with one file, as strings. Empty = the file is fine.
function problems(path, rep, pages) {
  const out = [];
  rep.cards.forEach((c, i) => {
    if (Math.abs(c.w - CARD_W) > 0.5 || Math.abs(c.h - CARD_H) > 0.5) out.push(`card ${i} is ${c.w}x${c.h}, not ${CARD_W}x${CARD_H} (4x6in)`);
  });
  for (const e of escapees(rep)) out.push(`clipped +${(e.bottom - e.cardH).toFixed(1)}px below ${JSON.stringify(e.text)}`);
  for (const req of requiredFor(path)) {
    const hit = rep.found.filter((f) => f.card === req.card && f.sel === req.sel && f.text.includes(req.text));
    if (hit.length === 0) { out.push(`missing ${req.what}: no ${req.sel} with ${JSON.stringify(req.text)} on card ${req.card}`); continue; }
    const h = hit[0];
    if (h.bottom <= h.top || h.top < -0.5 || h.bottom > CARD_H + 0.5 || h.right > CARD_W + 0.5) {
      out.push(`${req.what} is outside its card (${h.top}..${h.bottom} of ${CARD_H})`);
    }
  }
  if (pages !== undefined && pages !== rep.cards.length) out.push(`printed to ${pages} page(s) for ${rep.cards.length} card(s)`);
  return out;
}

// The plain card files carry no @page size; for a print-shop PDF each card gets
// a 4x6in page. Added at render time only -- the repo file is not changed.
const FOUR_BY_SIX = "<style>@page{size:4in 6in;margin:0} @media print{body{padding:0!important;gap:0!important}}</style>";

function main() {
  const selftest = process.argv.includes("--selftest");
  const pi = process.argv.indexOf("--pdf-out");
  const pdfDir = pi >= 0 ? process.argv[pi + 1] : null;
  const chrome = findChrome();
  if (!chrome) {
    console.error("FAIL: no Chrome/Edge found, so the cards cannot be measured.");
    console.error("      Reporting the RIG as incomplete rather than as a pass.");
    return 2;
  }
  console.log("browser: " + chrome);

  if (selftest) {
    // THE CONTROLS. Each plants one failure the check exists to catch, and each
    // must be caught -- otherwise this file is one more check that cannot fail.
    const qs = "docs/blipscope-quickstart-card.html";
    const src = readFileSync(qs, "utf8");
    const plants = [
      ["overflow: 40 rows pushed into the back face", (s) => s.replace('<div class="b-sec">', '<div class="b-sec">' + '<p>overflow test overflow test overflow test</p>\n'.repeat(40))],
      ["size: the card grows to 6.5in", (s) => s.replace("width:4in;height:6in;", "width:4in;height:6.5in;")],
      ["required: the data note is deleted", (s) => s.replace(/<p class="note">[\s\S]*?<\/p>/, "")],
      ["required: the QR is deleted", (s) => s.replace(/<div class="qr-slot">[\s\S]*?<\/div>/, "")],
      ["required: the note pushed off the card", (s) => s.replace("<p class=\"note\">", "<p class=\"note\" style=\"margin-top:120px\">")],
    ];
    const clean = measure(chrome, src, "clean");
    if (!clean) { console.error("FAIL: probe did not run; BLIND"); return 2; }
    let rc = 0;
    const cp = problems(qs, clean);
    if (cp.length === 0) console.log("  ok    CONTROL: the real quickstart card reports no problems");
    else { console.log(`  FAIL  the real card already has problems: ${cp.join("; ")}`); rc = 1; }
    for (const [name, plant] of plants) {
      const broken = plant(src);
      if (broken === src) { console.log(`  FAIL  plant did not apply: ${name}`); rc = 2; continue; }
      const rep = measure(chrome, broken, "planted");
      if (!rep) { console.log(`  FAIL  probe did not run for: ${name}`); rc = 2; continue; }
      const p = problems(qs, rep);
      if (p.length) console.log(`  ok    caught -- ${name}: ${p[0]}`);
      else { console.log(`  FAIL  NOT caught -- ${name}. This check is worthless for it.`); rc = 2; }
    }
    // The PAGE count must be able to fail too: a card grown to 6.5in cannot fit
    // one 4x6in page each, and the control proves the counter sees two cards as 2.
    const printable = (s) => s.replace("</head>", FOUR_BY_SIX + "\n</head>");
    const good = printPdf(chrome, printable(src), "pages-control").pages;
    const grown = printPdf(chrome, printable(plants[1][1](src)), "pages-planted").pages;
    if (good === 2) console.log("  ok    CONTROL: the real card prints to 2 pages");
    else { console.log(`  FAIL  the real card printed to ${good} page(s), not 2`); rc = 2; }
    if (grown !== 2 && grown > 0) console.log(`  ok    caught -- pages: the 6.5in card prints to ${grown} pages`);
    else { console.log(`  FAIL  NOT caught -- pages: the 6.5in card printed to ${grown} page(s)`); rc = 2; }

    console.log(rc === 0 ? "SELFTEST PASSED" : "SELFTEST FAILED");
    return rc;
  }

  if (pdfDir) mkdirSync(pdfDir, { recursive: true });
  let rc = 0;
  for (const path of CARDS) {
    if (!existsSync(path)) { console.log(`  FAIL  ${path} is missing`); rc = 1; continue; }
    const html = readFileSync(path, "utf8");
    const rep = measure(chrome, html, "c");
    if (!rep) { console.error(`  FAIL  ${path}: the probe did not run -- BLIND, not clean`); rc = 2; continue; }
    const isHarness = path.includes("PRINT-TEST");
    const printable = isHarness ? html : html.replace("</head>", FOUR_BY_SIX + "\n</head>");
    const name = basename(path, ".html") + (isHarness ? "-letter" : "-4x6") + ".pdf";
    const { pages } = printPdf(chrome, printable, "p", pdfDir ? join(pdfDir, name) : undefined);
    const bad = problems(path, rep, pages);
    if (bad.length === 0) {
      // Report the tightest margin, so a card creeping toward the edge is
      // visible BEFORE it clips rather than on the day it does.
      const worst = rep.leaves.reduce((a, b) => (b.cardH - b.bottom < a.cardH - a.bottom ? b : a));
      console.log(`  ok    ${path}  (${rep.cards.length} cards 4x6in, ${pages} PDF pages, required elements present; ` +
        `tightest margin ${(worst.cardH - worst.bottom).toFixed(1)}px: ${JSON.stringify(worst.text)})`);
    } else {
      console.log(`  FAIL  ${path}:`);
      for (const b of bad.slice(0, 8)) console.log(`          ${b}`);
      rc = 1;
    }
  }
  console.log(rc === 0 ? "\nAll print cards fit." : "\nPRINT CARDS FAILED");
  return rc;
}

process.exit(main());
