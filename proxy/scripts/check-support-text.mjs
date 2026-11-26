#!/usr/bin/env node
// The support page's TEXT, as served, against the repo's own file: every line a deploy changes must be live once it ships, and
// every retired privacy claim must be gone. The input comes from the other side: the lines are read from
// `git diff <base> HEAD -- proxy/pages/support.html`, and the retired phrases from scripts/check_privacy_text.py at HEAD.
// No sentence is typed here.
//
//   T0 CONTROL: HTTP 200, and the page's privacy anchor id="privacy" present once. If not, BLIND (exit 2): a page we cannot
//      read says nothing about its text.
//   T1: every ROW LINE present in the served page. A row line is a line added between <base> and HEAD, stripped, at least
//      MIN_LEN characters long, absent from <base>'s file and exactly once in HEAD's (so its presence proves the new text,
//      and nothing old can satisfy it).
//   T2: every RETIRED phrase absent (lower case, whitespace collapsed, as check_privacy_text.py reads).
//   T3: HEAD's "frozen on <Month D, YYYY>" phrase exactly once in the served page's text, and the set of valarscopes.com
//      links equal to HEAD's (check-support-links.mjs's own text and link rules).
//   Exit 0 PASS; 1 FAILED (each failing row printed with its count); 2 BLIND; 3 OUTSIDE (no row line at all, a HEAD line that
//   is not unique, or HEAD's page not stating exactly one freeze date: the instrument cannot judge).
//
//   node scripts/check-support-text.mjs --base <sha> --url <support page url>
//   node scripts/check-support-text.mjs --base <sha> --serve-rev <rev>
//                                                         OFFLINE: grade the file at <rev> as if served (no network). D0
//                                                         runs it with <rev> = the base, so the before-run's expected counts
//                                                         are COMPUTED by this same code, never typed.
//   node scripts/check-support-text.mjs --selftest        planted pages and diffs, an injected fetch, no network
import { execFileSync } from "node:child_process";

const MIN_LEN = 12;
const UA = "BlipscopeSupportTextCheck/1 (support@valarsystems.com)";
const EXIT = { PASS: 0, FAILED: 1, BLIND: 2, OUTSIDE: 3 };
const REPO = new URL("../../", import.meta.url);

const text = (html) => html.replace(/<[^>]*>/g, " ").replace(/&nbsp;/g, " ").replace(/\s+/g, " ");
const norm = (t) => t.replace(/\s+/g, " ").toLowerCase();
const siteLinks = (html) => [...new Set([...html.matchAll(/href="(https:\/\/valarscopes\.com\/[^"#?]*)"/g)].map((m) => m[1]))].sort();
const freezePhrases = (html) => [...text(html).matchAll(/frozen on ([A-Z][a-z]+ \d{1,2}, \d{4})/g)].map((m) => `frozen on ${m[1]}`);
const count = (hay, needle) => (needle ? hay.split(needle).length - 1 : 0);

/** The row lines: added in the diff, stripped, at least MIN_LEN long, absent from base, and their HEAD count. */
function rowLines(diff, baseHtml, headHtml) {
  const added = diff.split(/\r?\n/).filter((l) => l.startsWith("+") && !l.startsWith("+++")).map((l) => l.slice(1).trim());
  const rows = [...new Set(added.filter((l) => l.length >= MIN_LEN && !baseHtml.includes(l)))];
  return { rows, notUnique: rows.filter((l) => count(headHtml, l) !== 1) };
}

/** RETIRED = ( "...", ... ) in check_privacy_text.py, read as data. */
function retiredPhrases(py) {
  const m = /^RETIRED = \(([\s\S]*?)^\)/m.exec(py);
  return m ? [...m[1].matchAll(/"([^"]*)"/g)].map((x) => x[1]) : null;
}

function grade({ diff, baseHtml, headHtml, retired, status, body }) {
  const lines = [];
  if (status !== 200 || count(body ?? "", 'id="privacy"') !== 1) {
    return { verdict: "BLIND", lines: [`T0 CONTROL: HTTP ${status}, id="privacy" x${count(body ?? "", 'id="privacy"')}: the page cannot be read as the support page`] };
  }
  lines.push(`T0 CONTROL: HTTP 200, id="privacy" x1`);
  const { rows, notUnique } = rowLines(diff, baseHtml, headHtml);
  const phrases = freezePhrases(headHtml);
  if (rows.length === 0) return { verdict: "OUTSIDE", lines: [...lines, "no row line: nothing changed between base and HEAD to check"] };
  if (notUnique.length) return { verdict: "OUTSIDE", lines: [...lines, `${notUnique.length} row line(s) not exactly once in HEAD's page`] };
  if (retired === null) return { verdict: "OUTSIDE", lines: [...lines, "the RETIRED list could not be read from check_privacy_text.py"] };
  if (phrases.length !== 1) return { verdict: "OUTSIDE", lines: [...lines, `HEAD's page states ${phrases.length} freeze dates, not 1`] };
  let failed = false;
  const present = rows.filter((l) => body.includes(l)).length;
  lines.push(`T1 ${present === rows.length ? "PASS" : "FAIL"}: changed lines present ${present}/${rows.length}`);
  failed ||= present !== rows.length;
  const nb = norm(body);
  const back = retired.filter((p) => nb.includes(p.toLowerCase()));
  lines.push(`T2 ${back.length === 0 ? "PASS" : "FAIL"}: retired phrases present ${back.length} of ${retired.length}`);
  failed ||= back.length !== 0;
  const fz = count(text(body), phrases[0]);
  const linksEqual = JSON.stringify(siteLinks(body)) === JSON.stringify(siteLinks(headHtml));
  lines.push(`T3 ${fz === 1 && linksEqual ? "PASS" : "FAIL"}: the freeze phrase x${fz}; valarscopes.com links ${linksEqual ? "equal to" : "DIFFERENT from"} HEAD's (served ${siteLinks(body).length}, HEAD ${siteLinks(headHtml).length})`);
  failed ||= !(fz === 1 && linksEqual);
  return { verdict: failed ? "FAILED" : "PASS", lines };
}

function selftest() {
  const head = '<h2 id="privacy">Privacy</h2>\n<p>The new sentence about the request log.</p>\n<p>Every Blipscope, frozen\n  on November 25, 2026, keeps it.</p>\n<a href="https://valarscopes.com/blipscope/cloud/">x</a>\n';
  const base = '<h2 id="privacy">Privacy</h2>\n<p>anonymous usage counts</p>\n';
  const diff = "--- a/x\n+++ b/x\n+<p>The new sentence about the request log.</p>\n+<p>Every Blipscope, frozen\n+  on November 25, 2026, keeps it.</p>\n+<a href=\"https://valarscopes.com/blipscope/cloud/\">x</a>\n+</p>\n";
  const retired = ["anonymous usage counts", "not where you are"];
  const g = (o) => grade({ diff, baseHtml: base, headHtml: head, retired, status: 200, body: head, ...o });
  const rows = [];
  const row = (name, pass) => { rows.push(pass); console.log(`${pass ? "PASS" : "FAIL"} ${name}`); };
  const r = rowLines(diff, base, head);
  row("S-extract: 4 row lines, the 4-char '</p>' left out", r.rows.length === 4 && !r.rows.includes("</p>"));
  row("S-pass: the HEAD page served reads PASS", g({}).verdict === "PASS");
  row("S-T1: one changed line missing reads FAILED", g({ body: head.replace("The new sentence about the request log.", "Old text.") }).verdict === "FAILED");
  row("S-T2: a retired phrase back reads FAILED", g({ body: head + "<p>Anonymous   usage counts</p>" }).verdict === "FAILED");
  row("S-T3a: the freeze phrase twice reads FAILED", g({ body: head + "<p>frozen on November 25, 2026</p>" }).verdict === "FAILED");
  row("S-T3b: a different link set reads FAILED", g({ body: head.replace("/blipscope/cloud/", "/blipscope/") }).verdict === "FAILED");
  row("S-red: the BASE page served reads FAILED, never PASS", g({ body: base }).verdict === "FAILED");
  row("S-404: a 404 reads BLIND", g({ status: 404 }).verdict === "BLIND");
  row("S-ctl: no privacy anchor reads BLIND", g({ body: head.replace('id="privacy"', "") }).verdict === "BLIND");
  row("S-N0: an empty diff reads OUTSIDE", g({ diff: "" }).verdict === "OUTSIDE");
  row("S-dup: a changed line twice in HEAD reads OUTSIDE", g({ headHtml: head + "<p>The new sentence about the request log.</p>" }).verdict === "OUTSIDE");
  row("S-retired-parse: the RETIRED tuple is read as data", JSON.stringify(retiredPhrases('X = 1\nRETIRED = (\n    "a b",\n    "c — d",\n)\n')) === JSON.stringify(["a b", "c — d"]));
  const fails = rows.filter((x) => !x).length;
  console.log(fails ? `SELFTEST FAILED: ${fails}` : "SELFTEST PASSED");
  return fails ? 1 : 0;
}

if (process.argv.includes("--selftest")) process.exit(selftest());
const arg = (k) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : undefined; };
const base = arg("--base"), url = arg("--url"), serveRev = arg("--serve-rev");
if (!base || !/^[0-9a-f]{7,40}$/.test(base) || (!url === !serveRev) || (serveRev && !/^[0-9a-f]{7,40}$/.test(serveRev))) {
  console.error("usage: check-support-text.mjs --base <sha> (--url <support page url> | --serve-rev <sha>)"); process.exit(2);
}
const git = (...a) => execFileSync("git", a, { cwd: REPO, encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
let diff, baseHtml, headHtml, py;
try {
  diff = git("diff", "--unified=0", base, "HEAD", "--", "proxy/pages/support.html");
  baseHtml = git("show", `${base}:proxy/pages/support.html`);
  headHtml = git("show", "HEAD:proxy/pages/support.html");
  py = git("show", "HEAD:scripts/check_privacy_text.py");
} catch (e) {
  console.log(`BLIND: the repo could not be read (${e instanceof Error ? e.message.split("\n")[0] : String(e)})`);
  console.log("VERDICT: BLIND");
  process.exit(EXIT.BLIND);
}
let status = null, body = "";
if (serveRev) {
  try { body = git("show", `${serveRev}:proxy/pages/support.html`); status = 200; } catch (e) { status = "unreadable rev"; }
} else {
  try {
    const res = await fetch(url, { headers: { "User-Agent": UA }, redirect: "follow", signal: AbortSignal.timeout(20_000) });
    status = res.status; body = await res.text();
  } catch (e) { status = `error ${e instanceof Error ? e.name : String(e)}`; }
}
const r = grade({ diff, baseHtml, headHtml, retired: retiredPhrases(py), status, body });
console.log(`base ${base.slice(0, 12)} -> HEAD ${git("rev-parse", "--short=12", "HEAD").trim()}; served: ${serveRev ? `the file at ${serveRev.slice(0, 12)} (OFFLINE)` : "the live page"}`);
for (const l of r.lines) console.log(l);
console.log(`VERDICT: ${r.verdict}`);
process.exit(EXIT[r.verdict]);
