#!/usr/bin/env node
// Does the printed card's "reset Wi-Fi" instruction match the FIRMWARE?
//
// check-print-cards.mjs asserts the card's sentences EXACTLY -- which proves the
// wording did not change, and nothing else. It cannot see the firmware moving out
// from under a sentence that stayed the same. That is exactly how the card came to
// say "Swipe left twice to Stats, tap [ Reset Wi-Fi ]" for months after the reset
// control had moved to Connect, three swipes away, behind a menu (2026-09-26).
//
// So this check takes every FACT in the sentence from the firmware source, not
// from a list someone keeps in their head:
//   screen      the screen whose tap opens the reset menu  (HandleTap: screen == Screen::X && resetRowY0)
//   label       what that screen's Draw function writes at resetY (DrawX: centred("[ ... ]", resetY)
//   menu item   the reset menu's Wi-Fi option             (DrawResetMenu: "Reset Wi-Fi")
//   swipes      left swipes from Radar to that screen: the Screen enum order, minus the
//               screens AdvanceScreen skips, walked in the direction a LEFT swipe advances
//               (HandleSwipe: Swipe::Left -> AdvanceScreen(+1 | -1)). Never hard-coded.
// and requires the card's sentence to equal the one those facts produce, word for word.
//
// A fact the parser cannot find is BLIND (exit 2), never a pass: a check that cannot
// see the firmware is indistinguishable from a card that is right.
//
// RUN:  node scripts/check-card-gestures.mjs [--selftest]
// EXIT: 0 card matches firmware   1 it does not   2 blind (a fact could not be derived)

import { readFileSync } from "node:fs";

const CARDS = ["docs/blipscope-quickstart-card.html", "docs/blipscope-quickstart-card-PRINT-TEST.html"];
const HEADER = "src/AircraftManager.h";
const SOURCE = "src/AircraftManager.cpp";
const COUNT_WORDS = { 1: "once", 2: "twice", 3: "three times", 4: "four times", 5: "five times" };

class Blind extends Error {}
const need = (v, what) => { if (v === undefined || v === null || v === "") throw new Blind(what); return v; };

function functionBody(src, name) {
  const at = src.search(new RegExp(`\\n[^\\n]*AircraftManager::${name}\\s*\\(`));
  if (at < 0) return null;
  const open = src.indexOf("{", at);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === "{") depth++;
    else if (src[i] === "}" && --depth === 0) return src.slice(open, i + 1);
  }
  return null;
}

export function deriveFacts(header, source) {
  const enumBody = need((header.match(/enum\s+class\s+Screen\s*\{([^}]*)\}/) || [])[1], "enum class Screen");
  const order = enumBody.split(",").map((s) => s.replace(/=.*$/, "").trim()).filter(Boolean);
  const adv = need(functionBody(source, "AdvanceScreen"), "AdvanceScreen()");
  const skipped = [...adv.matchAll(/==\s*Screen::(\w+)\s*\)\s*\n?\s*continue/g)].map((m) => m[1]);
  const carousel = order.filter((s) => !skipped.includes(s));
  const swipe = need(functionBody(source, "HandleSwipe"), "HandleSwipe()");
  const dir = Number(need((swipe.match(/Swipe::Left\)\s*AdvanceScreen\(\s*([+-]?1)\s*\)/) || [])[1], "Swipe::Left -> AdvanceScreen(dir)"));
  const tap = need(functionBody(source, "HandleTap"), "HandleTap()");
  const screen = need((tap.match(/screen\s*==\s*Screen::(\w+)\s*&&\s*resetRowY0/) || [])[1], "the screen whose tap opens the reset menu");
  const draw = need(functionBody(source, `Draw${screen}`), `Draw${screen}()`);
  const label = need((draw.match(/centred\(\s*"(\[[^"]*\])"\s*,\s*resetY/) || [])[1], `the label Draw${screen} writes at resetY`);
  const menu = need(functionBody(source, "DrawResetMenu"), "DrawResetMenu()");
  const item = need((menu.match(/"(Reset Wi-Fi)"/) || [])[1], "the reset menu's Wi-Fi item");
  const from = carousel.indexOf("Radar"), to = carousel.indexOf(screen);
  if (from < 0 || to < 0) throw new Blind(`Radar or ${screen} not in the carousel ${carousel.join(",")}`);
  const n = carousel.length;
  const swipes = (((to - from) * dir) % n + n) % n;
  if (!swipes) throw new Blind(`${screen} is Radar itself`);
  return { carousel, skipped, dir, screen, label, item, swipes };
}

export function expectedSentence(f) {
  const count = need(COUNT_WORDS[f.swipes], `a word for ${f.swipes} swipes`);
  return `Swipe left ${count} to ${f.screen}, tap ${f.label}, choose ${f.item}, then tap again to confirm.`;
}

export function cardSentence(html) {
  const m = html.match(/<p class="reset-how">([\s\S]*?)<\/p>/);
  if (!m) return null;
  return m[1].replace(/<[^>]+>/g, "").replace(/&amp;/g, "&").replace(/\s+/g, " ").trim();
}

function check(header, source, cards) {
  const facts = deriveFacts(header, source);
  const want = expectedSentence(facts);
  const bad = [];
  for (const [path, html] of cards) {
    const got = cardSentence(html);
    if (got === null) throw new Blind(`${path}: no <p class="reset-how">`);
    if (got !== want) bad.push(`${path}:\n    card:     "${got}"\n    firmware: "${want}"`);
  }
  return { facts, want, bad };
}

function run(header, source, cards, quiet = false) {
  try {
    const { facts, want, bad } = check(header, source, cards);
    if (!quiet) {
      console.log(`firmware: carousel ${facts.carousel.join(" -> ")} (skips ${facts.skipped.join(",") || "none"}), ` +
                  `left = AdvanceScreen(${facts.dir > 0 ? "+" : ""}${facts.dir}); reset on ${facts.screen}, ` +
                  `label ${facts.label}, menu "${facts.item}", ${facts.swipes} left swipe(s) from Radar`);
      console.log(`expected: "${want}"`);
    }
    if (bad.length) { if (!quiet) console.log("FAIL: the card disagrees with the firmware\n  " + bad.join("\n  ")); return 1; }
    if (!quiet) console.log(`ok: ${cards.length} card(s) match the firmware`);
    return 0;
  } catch (e) {
    if (e instanceof Blind) { if (!quiet) console.log(`BLIND: could not derive ${e.message}`); return 2; }
    throw e;
  }
}

const header = readFileSync(HEADER, "utf8");
const source = readFileSync(SOURCE, "utf8");
const cards = CARDS.map((p) => [p, readFileSync(p, "utf8")]);

if (process.argv.includes("--selftest")) {
  // Each plant changes the firmware or the card in memory; the check must answer as stated.
  const swapEnum = (h, a, b) => h.replace(/enum\s+class\s+Screen\s*\{([^}]*)\}/, (all, body) =>
    all.replace(body, body.split(",").map((s) => s.trim() === a ? ` ${b}` : s.trim() === b ? ` ${a}` : s).join(",")));
  const plants = [
    ["CONTROL: the real firmware and cards", header, source, cards, 0],
    ["Connect moved one carousel step EARLIER (swap with Stats) -> 2 swipes", swapEnum(header, "Stats", "Connect"), source, cards, 1],
    ["left swipe reversed (AdvanceScreen(-1)) -> 1 swipe", header, source.replace(/Swipe::Left\)\s*AdvanceScreen\(\+1\)/, "Swipe::Left)  AdvanceScreen(-1)"), cards, 1],
    // Only the TAP moves here, so DrawStats has no "[ ... ]" at resetY: the firmware is
    // incoherent and the label cannot be derived. BLIND is the right answer -- a refusal,
    // never a pass. (First written expecting 1; the checker was right and the plant was not.)
    ["the reset tap moved to Stats without its label -> blind", header, source.replace(/screen\s*==\s*Screen::Connect\s*&&\s*resetRowY0/, "screen == Screen::Stats && resetRowY0"), cards, 2],
    ["button relabelled", header, source.replace(/centred\("\[ Reset \]"/, 'centred("[ Wipe ]"'), cards, 1],
    ["card sentence edited", header, source, cards.map(([p, h]) => [p, h.replace("three times", "twice")]), 1],
    ["BLIND: the Screen enum renamed away", header.replace("enum class Screen", "enum class Page"), source, cards, 2],
  ];
  let rc = 0;
  for (const [name, h, s, c, want] of plants) {
    if (name.startsWith("CONTROL") ? false : (h === header && s === source && c === cards)) { console.log(`  FAIL  plant did not apply: ${name}`); rc = 2; continue; }
    const got = run(h, s, c, true);
    const ok = got === want;
    console.log(`  ${ok ? "ok  " : "FAIL"}  ${name}: exit ${got} (want ${want})`);
    if (!ok) rc = 1;
  }
  console.log(rc ? "SELFTEST FAILED" : "SELFTEST PASSED");
  process.exit(rc);
}

process.exit(run(header, source, cards));
