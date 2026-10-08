#!/usr/bin/env node
// The config page's setup checklist must reflect the LIVE location after BOTH ways of saving it.
//
// #383 (2026-10-08): after "Use my location" saved the location, the banner still said
// "1. Set your location" and the boxes stayed red until the owner pressed Save. window.bpSetupDone()
// -- the re-check that collapses step 1 -- had ONE caller, the form-save path; the v15 auto-save
// path never called it, and the red outlines were cleared only by typing. A second path that skipped
// what the first one establishes (CLAUDE.md).
//
// Two halves, both against the page's own script in src/ConfigurationWebServer.cpp:
//   PATHS     the form-save path AND the "Use my location" success path each call bpSetupDone().
//   BEHAVIOUR bpSetupDone(), run against a small fake DOM, collapses step 1 (or removes the banner)
//             AND clears the red outlines once a location is set -- and changes nothing while it
//             is empty (the control).
// No dependencies: the fake DOM is just enough for this function.
//
// RUN:  node scripts/check-setup-banner.mjs [--selftest]
// EXIT: 0 ok   1 a check failed   2 blind (the script could not be found in the source)

import { readFileSync } from "node:fs";

const SOURCE = "src/ConfigurationWebServer.cpp";
class Blind extends Error {}

// The page script is a sequence of C++ raw strings R"(...)"; join them in order.
export function pageScript(cpp) {
  const parts = [...cpp.matchAll(/R"\(([\s\S]*?)\)"/g)].map((m) => m[1]);
  return parts.join("");
}

function balanced(src, from) {
  const open = src.indexOf("{", from);
  if (open < 0) return null;
  let d = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === "{") d++;
    else if (src[i] === "}" && --d === 0) return src.slice(from, i + 1);
  }
  return null;
}

export function extract(js) {
  const at = js.indexOf("window.bpSetupDone=function(){");
  if (at < 0) throw new Blind("window.bpSetupDone definition");
  const def = balanced(js, at);
  if (!def) throw new Blind("the end of bpSetupDone");
  // The auto-save success: from the line that writes the saved values into the boxes to the
  // success message.
  const s0 = js.indexOf("shLa.value=j.lat;shLo.value=j.lon;");
  const s1 = js.indexOf("Saved your location from this browser", s0);
  if (s0 < 0 || s1 < 0) throw new Blind("the \"Use my location\" success path");
  const autoSave = js.slice(s0, s1);
  // The form-save path: the existing caller.
  const formSave = /if\(!w&&window\.bpSetupDone\)window\.bpSetupDone\(\);/.test(js);
  return { def, autoSave, formSave };
}

// ---- a fake DOM, only as much as bpSetupDone touches ---------------------------------------
function el(tag) {
  return {
    tag, id: "", style: {}, children: [], parentNode: null, _text: "",
    get textContent() { return this._text + this.children.map((c) => c.textContent).join(""); },
    set textContent(t) { this._text = t; this.children = []; },
    appendChild(c) { this.children.push(c); c.parentNode = this; return c; },
    removeChild(c) { this.children = this.children.filter((x) => x !== c); c.parentNode = null; return c; },
    get firstChild() { return this.children[0] || null; },
    addEventListener() {},
  };
}
function world({ lat, lon, enrolled }) {
  const page = el("body"), banner = el("div"), step1 = el("div"), la = el("input"), lo = el("input");
  banner.id = "bpBanner"; step1.id = "bpStep1";
  step1.appendChild(Object.assign(el("b"), { _text: "1. Set your location." }));
  banner.appendChild(step1); page.appendChild(banner);
  la.value = lat; lo.value = lon;
  for (const i of [la, lo]) { i.style.outline = "2px solid #ff4d4d"; i.style.background = "#4a0000"; }
  const byId = { bpBanner: banner, bpStep1: step1 };
  const document = {
    createElement: el,
    getElementById: (id) => (byId[id] && (id === "bpBanner" ? banner.parentNode : true) ? byId[id] : null),
    querySelector: (q) => ({ "input[name=latitude]": la, "input[name=longitude]": lo })[q] || null,
  };
  const window = { BP_ENROLLED: enrolled, BP_REFUSED: "0" };
  return { page, banner, step1, la, lo, document, window };
}
function runDef(def, w) {
  // eslint-disable-next-line no-new-func
  new Function("window", "document", def + ";")(w.window, w.document);
  w.window.bpSetupDone();
}

export function check(cpp) {
  const js = pageScript(cpp);
  const { def, autoSave, formSave } = extract(js);
  const res = [];
  // The behaviour below evaluates ONE function. A slip anywhere else in a <script> block kills
  // the whole setup page, so every block must at least parse.
  const blocks = [...js.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
  const unparsed = blocks.filter((s) => { try { new Function(s); return false; } catch { return true; } });
  res.push([`every page <script> block parses (${blocks.length} found)`, blocks.length > 0 && unparsed.length === 0]);
  res.push(["the form-save path calls bpSetupDone()", formSave]);
  res.push(["the \"Use my location\" success path calls bpSetupDone()", /bpSetupDone\(\)/.test(autoSave)]);
  const red = (i) => i.style.outline && i.style.outline !== "";
  { // CONTROL: no location -> nothing changes
    const w = world({ lat: "", lon: "", enrolled: "1" }); runDef(def, w);
    res.push(["CONTROL: with no location, the banner and the red boxes stay", w.banner.parentNode === w.page && red(w.la) && red(w.lo)]);
  }
  { // location set, device verified -> the whole block goes, boxes not red
    const w = world({ lat: "44.1", lon: "-121.3", enrolled: "1" }); runDef(def, w);
    res.push(["location set + verified: the banner is removed", w.banner.parentNode === null]);
    res.push(["location set + verified: the boxes are no longer red", !red(w.la) && !red(w.lo)]);
  }
  { // location set, not yet verified -> step 1 collapses to DONE, boxes not red
    const w = world({ lat: "44.1", lon: "-121.3", enrolled: "0" }); runDef(def, w);
    res.push(["location set + unverified: step 1 reads DONE", /^DONE - Set your location\./.test(w.step1.textContent)]);
    res.push(["location set + unverified: the boxes are no longer red", !red(w.la) && !red(w.lo)]);
  }
  return res;
}

function run(cpp, quiet = false) {
  try {
    const res = check(cpp);
    let bad = 0;
    for (const [name, ok] of res) { if (!ok) bad++; if (!quiet) console.log(`  ${ok ? "ok  " : "FAIL"}  ${name}`); }
    if (!quiet) console.log(bad ? `FAIL: ${bad} check(s)` : "ok: the setup checklist follows both save paths");
    return bad ? 1 : 0;
  } catch (e) {
    if (e instanceof Blind) { if (!quiet) console.log(`BLIND: could not find ${e.message}`); return 2; }
    throw e;
  }
}

const cpp = readFileSync(SOURCE, "utf8");
if (process.argv.includes("--selftest")) {
  const plants = [
    ["CONTROL: the real source", cpp, 0],
    ["the auto-save path's bpSetupDone() call removed", cpp.replace(/shLa\.value=j\.lat;shLo\.value=j\.lon;if\(window\.bpSetupDone\)window\.bpSetupDone\(\);/, "shLa.value=j.lat;shLo.value=j.lon;"), 1],
    ["the outline clearing removed from bpSetupDone", cpp.replace(/\[la,lo\]\.forEach\(function\(i\)\{i\.style\.outline='';i\.style\.background=''\}\);/, ""), 1],
    ["a syntax error in the auto-save path", cpp.replace(/window\.bpSetupDone\(\);bpFb/, "window.bpSetupDone(;bpFb"), 1],
    ["BLIND: bpSetupDone renamed away", cpp.replace(/window\.bpSetupDone=function/, "window.bpSetupDoneX=function"), 2],
  ];
  let rc = 0;
  for (const [name, src, want] of plants) {
    if (!name.startsWith("CONTROL") && src === cpp) { console.log(`  FAIL  plant did not apply: ${name}`); rc = 2; continue; }
    const got = run(src, true);
    const ok = got === want;
    console.log(`  ${ok ? "ok  " : "FAIL"}  ${name}: exit ${got} (want ${want})`);
    if (!ok) rc = 1;
  }
  console.log(rc ? "SELFTEST FAILED" : "SELFTEST PASSED");
  process.exit(rc);
}
process.exit(run(cpp));
