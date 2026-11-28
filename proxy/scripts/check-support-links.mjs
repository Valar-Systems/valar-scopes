#!/usr/bin/env node
// The support page's links to our own site must answer, and the terms they point to must be the terms the page states.
// A dead link on the support page is worse than none, and a live page with different terms is worse still.
//
// The input comes from the other side: the links are read OUT OF pages/support.html (every href to valarscopes.com),
// and so is the page's own "frozen on <Month D, YYYY>" phrase. Nothing here names a URL the page does not.
//
//   1. CONTROL first: https://valarscopes.com/blipscope/ must answer 200. If it does not, the site cannot be reached
//      from where this runs, which says nothing about the links: BLIND (exit 2), never a verdict on them.
//   2. Every link: a GET following redirects; the final status must be 200. A 4xx is final (FAILED). A 5xx, a timeout
//      or a network error is retried (3 attempts, 2 s apart, 10 s each); if it never answers, OUTSIDE (exit 3).
//   3. The wording: the page must state exactly one freeze date, and at least one linked page must carry the same
//      phrase in its text (tags stripped, whitespace collapsed). A 200 without it is FAILED.
//
//   node scripts/check-support-links.mjs              exit 0 PASS, 1 FAILED, 2 BLIND, 3 OUTSIDE; prints urls, statuses
//   node scripts/check-support-links.mjs --changed-since <sha>
//                                                     CI's form: the live check runs ONLY when the diff from <sha> to
//                                                     HEAD touches the SCOPE below, so a site outage cannot turn an
//                                                     unrelated PR red. Otherwise "skipped: no link-bearing change", exit 0.
//                                                     A skip must be a skip: an empty diff, a scope file missing from the
//                                                     tree, or a git failure is BLIND (exit 2), never a skip. A PUSH run's
//                                                     form, given the event's `before`.
//   node scripts/check-support-links.mjs --changed-since-merge [--recorded <sha>]
//                                                     A PULL REQUEST run's form: the base is the test merge commit's FIRST
//                                                     PARENT (read from `git cat-file -p HEAD`, so a depth-1 checkout
//                                                     serves), then as --changed-since. The event's recorded base.sha is
//                                                     printed beside it and never used: it can predate the merge being
//                                                     tested, and then a diff from it carries main's own files into the PR
//                                                     (#836: recorded 27b51243b564, merged onto 3bd9d536aa13). A HEAD that
//                                                     is not a two-parent merge reads BLIND, never a fallback.
//   node scripts/check-support-links.mjs --selftest   planted HTML, an injected fetch and planted diffs, plus one real
//                                                     throwaway git repo with a stale recorded base; no network
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";

const CONTROL = "https://valarscopes.com/blipscope/";
const UA = "BlipscopeLinkCheck/1 (+https://github.com/Valar-Systems/valar-scopes)";
const EXIT = { PASS: 0, FAILED: 1, BLIND: 2, OUTSIDE: 3 };
// The files that carry, embed or check these links: a change to any of them runs the live check in CI.
const SCOPE = ["proxy/pages/support.html", "proxy/src/pages.generated.ts", "proxy/scripts/check-support-links.mjs"];
const REPO = new URL("../../", import.meta.url);

/** The CI scope decision: "run", "skip" or "BLIND", with why. `exists(path)` says whether a scope path is in the tree. */
function decideScope(files, exists) {
  const missing = SCOPE.filter((p) => !exists(p));
  if (missing.length) return { action: "BLIND", why: `the scope names ${missing.join(", ")}, which is not in the tree: a skip could never be told from a rename` };
  if (files.length === 0) return { action: "BLIND", why: "the diff lists no file: an empty diff cannot tell no change from a broken diff" };
  const hit = files.filter((f) => SCOPE.includes(f));
  if (hit.length) return { action: "run", why: `link-bearing change: ${hit.length} of ${files.length} files (${hit.join(", ")})` };
  return { action: "skip", why: `skipped: no link-bearing change (${files.length} files changed, none in the scope)` };
}

/** The base a pull request's CI diffs from: its test merge commit's FIRST parent, the base tip that merge was built on,
 *  so `parent1..HEAD` is exactly what the PR adds to the tree under test. Read from the commit object's header (the
 *  lines before the first blank line), so a depth-1 checkout serves. Anything but two full-sha parents reads BLIND. */
function baseForMerge(commitText) {
  const header = String(commitText).split("\n\n")[0].split("\n");
  const parents = header.filter((l) => l.startsWith("parent ")).map((l) => l.slice("parent ".length).trim());
  if (parents.length !== 2) return { ok: false, why: `HEAD has ${parents.length} parent(s), not 2: it is not a test merge, so the PR's base cannot be known` };
  if (!parents.every((p) => /^[0-9a-f]{40}$/.test(p))) return { ok: false, why: "a parent line is not a full sha" };
  return { ok: true, base: parents[0] };
}

/** L2's stale-base case, in a real throwaway repo removed on every exit. Main is A then B, and B touches support.html;
 *  the PR branches from A and touches only docs/x.md; M = merge(B, PR). The repo reads no global or system git config
 *  (no hooks, no signing, no line-ending rules), and its synthetic identity is set for it alone. */
function staleBaseCase() {
  const dir = mkdtempSync(join(tmpdir(), "linkbase-"));
  const cleanup = () => rmSync(dir, { recursive: true, force: true });
  process.on("exit", cleanup);
  const repo = join(dir, "repo");
  mkdirSync(repo);
  writeFileSync(join(dir, "empty.gitconfig"), "");
  const env = { ...process.env, GIT_CONFIG_GLOBAL: join(dir, "empty.gitconfig"), GIT_CONFIG_NOSYSTEM: "1",
    GIT_AUTHOR_NAME: "selftest", GIT_AUTHOR_EMAIL: "selftest@example.invalid",
    GIT_COMMITTER_NAME: "selftest", GIT_COMMITTER_EMAIL: "selftest@example.invalid" };
  const git = (...a) => execFileSync("git", a, { cwd: repo, env, encoding: "utf8" }).trim();
  const put = (p, s) => { mkdirSync(dirname(join(repo, p)), { recursive: true }); writeFileSync(join(repo, p), s); };
  git("init", "-q", "-b", "main");
  for (const p of [...SCOPE, "docs/x.md"]) put(p, `${p} v1\n`);
  git("add", "-A");
  git("commit", "-q", "-m", "A");
  const A = git("rev-parse", "HEAD");
  git("checkout", "-q", "-b", "pr");
  put("docs/x.md", "docs/x.md v2\n");
  git("commit", "-q", "-a", "-m", "P");
  const P = git("rev-parse", "HEAD");
  git("checkout", "-q", "main");
  put("proxy/pages/support.html", "proxy/pages/support.html v2\n");
  git("commit", "-q", "-a", "-m", "B");
  const B = git("rev-parse", "HEAD");
  git("merge", "-q", "--no-ff", "-m", "M", "pr");
  const M = git("rev-parse", "HEAD");
  const decide = (base) => decideScope(git("diff", "--name-only", base, M).split("\n").filter(Boolean), (p) => existsSync(join(repo, p)));
  return { A, B, P, M, text: git("cat-file", "-p", M), decide, cleanup };
}

/** The scope decision against a base, from the real tree: exits BLIND (2) or 0 (a skip); returns on "run". */
function scopeAgainst(base) {
  let files;
  try {
    if (!base || !/^[0-9a-f]{7,40}$/.test(base)) throw new Error("no base sha given");
    files = execFileSync("git", ["diff", "--name-only", base, "HEAD"], { cwd: REPO, encoding: "utf8" }).split("\n").filter(Boolean);
  } catch (e) {
    console.log(`BLIND: the diff could not be read (${e instanceof Error ? e.message.split("\n")[0] : String(e)})`);
    process.exit(EXIT.BLIND);
  }
  const d = decideScope(files, (p) => existsSync(new URL(p, REPO)));
  console.log(d.why);
  if (d.action === "BLIND") { console.log("VERDICT: BLIND"); process.exit(EXIT.BLIND); }
  if (d.action === "skip") process.exit(0);
}

const text = (html) => html.replace(/<[^>]*>/g, " ").replace(/&nbsp;/g, " ").replace(/\s+/g, " ");
const siteLinks = (html) => [...new Set([...html.matchAll(/href="(https:\/\/valarscopes\.com\/[^"#?]*)"/g)].map((m) => m[1]))].sort();
const freezePhrases = (html) => [...text(html).matchAll(/frozen on ([A-Z][a-z]+ \d{1,2}, \d{4})/g)].map((m) => `frozen on ${m[1]}`);

async function get(url, doFetch, waitMs) {
  let last = "";
  for (let a = 1; a <= 3; a++) {
    try {
      const res = await doFetch(url, { redirect: "follow", headers: { "User-Agent": UA }, signal: AbortSignal.timeout(10_000) });
      if (res.status >= 500 && a < 3) { last = String(res.status); await new Promise((r) => setTimeout(r, waitMs)); continue; }
      return { status: res.status, body: res.status === 200 ? await res.text() : null };
    } catch (e) {
      last = e instanceof Error ? e.name : String(e);
      if (a < 3) await new Promise((r) => setTimeout(r, waitMs));
    }
  }
  return { status: null, err: last };
}

async function check(html, doFetch, waitMs = 2_000) {
  const lines = [];
  const links = siteLinks(html), phrases = freezePhrases(html);
  if (links.length === 0) return { verdict: "BLIND", lines: ["no valarscopes.com link in the page: nothing to check"] };
  if (phrases.length !== 1) return { verdict: "OUTSIDE", lines: [`the page states ${phrases.length} freeze dates, not 1`] };
  const c = await get(CONTROL, doFetch, waitMs);
  if (c.status !== 200) return { verdict: "BLIND", lines: [`control ${CONTROL} -> ${c.status ?? c.err}`] };
  lines.push(`control ${CONTROL} -> 200`);
  let failed = false, outside = false, carried = false;
  for (const u of links) {
    const r = await get(u, doFetch, waitMs);
    lines.push(`${u} -> ${r.status ?? r.err}`);
    if (r.status === 200) { if (text(r.body).includes(phrases[0])) carried = true; }
    else if (r.status !== null && r.status >= 400 && r.status < 500) failed = true;
    else outside = true;
  }
  if (!failed && !outside && !carried) { lines.push(`no linked page carries "${phrases[0]}"`); failed = true; }
  return { verdict: outside ? "OUTSIDE" : failed ? "FAILED" : "PASS", lines };
}

async function selftest() {
  const T = "https://valarscopes.com/blipscope/cloud/";
  const page = (links, freeze = "frozen\n  on November 25, 2026") =>
    `<p>Every Blipscope on our list, ${freeze}, keeps it.</p>` + links.map((u) => `<a href="${u}">x</a>`).join("");
  const stub = (script) => {
    const seen = {};
    const f = async (url) => {
      seen[url] = (seen[url] ?? 0) + 1;
      const s = script[url]?.[seen[url] - 1] ?? script[url]?.at(-1);
      if (s === undefined || s === "neterr") throw new TypeError("fetch failed");
      return new Response(s.body ?? "", { status: s.status });
    };
    f.seen = seen;
    return f;
  };
  const ok = { status: 200, body: "<p>Every Blipscope on our list of earlier devices, frozen <b>on</b>\nNovember 25, 2026, keeps it.</p>" };
  const rows = [];
  const row = (name, pass) => { rows.push(pass); console.log(`${pass ? "PASS" : "FAIL"} ${name}`); };

  const ex = siteLinks(page([T, "https://valarscopes.com/blipscope/", "https://adsb.fi"]));
  row("S-extract: exactly the 2 valarscopes links, not the other", JSON.stringify(ex) === JSON.stringify(["https://valarscopes.com/blipscope/", T]));
  row("S-none: 0 links reads BLIND, never a pass", (await check(page([]), stub({}), 0)).verdict === "BLIND");
  row("S-control: the control failing reads BLIND", (await check(page([T]), stub({ [CONTROL]: [{ status: 503 }], [T]: [ok] }), 0)).verdict === "BLIND");
  const f404 = stub({ [CONTROL]: [{ status: 200 }], [T]: [{ status: 404 }] });
  row("S-404: a 404 reads FAILED, not retried", (await check(page([T]), f404, 0)).verdict === "FAILED" && f404.seen[T] === 1);
  const fnet = stub({ [CONTROL]: [{ status: 200 }], [T]: ["neterr"] });
  row("S-neterr: no answer after 3 attempts reads OUTSIDE, never a pass", (await check(page([T]), fnet, 0)).verdict === "OUTSIDE" && fnet.seen[T] === 3);
  row("S-retry: a 503 then a 200 with the phrase reads PASS", (await check(page([T]), stub({ [CONTROL]: [{ status: 200 }], [T]: [{ status: 503 }, ok] }), 0)).verdict === "PASS");
  row("S-wording: a 200 WITHOUT the phrase reads FAILED", (await check(page([T]), stub({ [CONTROL]: [{ status: 200 }], [T]: [{ status: 200, body: "<p>frozen on the day these terms change</p>" }] }), 0)).verdict === "FAILED");
  row("S-split: the phrase split by a tag and a line break reads PASS", (await check(page([T]), stub({ [CONTROL]: [{ status: 200 }], [T]: [ok] }), 0)).verdict === "PASS");
  row("S-two-dates: a page stating 2 freeze dates reads OUTSIDE", (await check(page([T], "frozen on November 25, 2026 and frozen on December 1, 2026"), stub({ [CONTROL]: [{ status: 200 }], [T]: [ok] }), 0)).verdict === "OUTSIDE");
  const all = () => true;
  row("S-scope-hit: a scope file in the diff runs the live check", decideScope(["docs/x.md", "proxy/pages/support.html"], all).action === "run");
  row("S-scope-miss: a diff with no scope file is a skip", decideScope(["docs/x.md", "proxy/src/index.ts"], all).action === "skip");
  row("S-scope-empty: an empty diff reads BLIND, never a skip", decideScope([], all).action === "BLIND");
  row("S-scope-stale: a scope file missing from the tree reads BLIND", decideScope(["docs/x.md"], (p) => p !== "proxy/pages/support.html").action === "BLIND");

  // L1: the base a PR run diffs from is its test merge's FIRST parent, read from the commit object.
  const P1 = "1".repeat(40), P2 = "2".repeat(40), TR = "3".repeat(40);
  const commit = (parents) => `tree ${TR}\n` + parents.map((p) => `parent ${p}\n`).join("") +
    "author a <a@example.invalid> 1 +0000\ncommitter c <c@example.invalid> 1 +0000\n\nparent " + P2 + " in the message is not a parent\n";
  const two = baseForMerge(commit([P1, P2]));
  row("L1-two: a test merge's commit gives its FIRST parent", two.ok === true && two.base === P1);
  row("L1-one: one parent (not a test merge) reads BLIND, never a fallback", baseForMerge(commit([P1])).ok === false);
  row("L1-none: no parent line reads BLIND", baseForMerge(commit([])).ok === false);
  // L2, L3: a real repo whose recorded base (A) is stale against the merge's first parent (B).
  const sc = staleBaseCase();
  try {
    const old = sc.decide(sc.A);
    row(`L2-old: the stale recorded base carries main's change into the diff (${old.why})`,
      old.action === "run" && old.why.includes("1 of 2 files"));
    const nb = baseForMerge(sc.text);
    const nw = nb.ok ? sc.decide(nb.base) : { action: "BLIND", why: nb.why };
    row(`L2-new: the merge's first parent gives the PR's own change only, a skip (${nw.why})`,
      nb.ok === true && nb.base === sc.B && nw.action === "skip" && nw.why.includes("(1 files changed"));
    row("L3-plant (parent 2): diffing from the PR side turns L2-new red", sc.decide(sc.P).action !== "skip");
    row("L3-plant (the recorded base): diffing from A turns L2-new red", sc.decide(sc.A).action !== "skip");
  } finally {
    sc.cleanup();
  }
  const fails = rows.filter((r) => !r).length;
  console.log(fails ? `SELFTEST FAILED: ${fails}` : "SELFTEST PASSED");
  return fails ? 1 : 0;
}

if (process.argv.includes("--selftest")) {
  process.exit(await selftest());
}
if (process.argv.includes("--changed-since-merge")) {
  const ri = process.argv.indexOf("--recorded");
  const recorded = ri > 0 ? process.argv[ri + 1] ?? "" : "";
  let b;
  try {
    b = baseForMerge(execFileSync("git", ["cat-file", "-p", "HEAD"], { cwd: REPO, encoding: "utf8" }));
  } catch (e) {
    b = { ok: false, why: `the HEAD commit could not be read (${e instanceof Error ? e.message.split("\n")[0] : String(e)})` };
  }
  console.log(`base: ${b.ok ? `the test merge's first parent ${b.base.slice(0, 12)}` : "none"}; the event's recorded base.sha ` +
    `${recorded ? recorded.slice(0, 12) : "(none)"} (printed, never used)`);
  if (!b.ok) { console.log(`BLIND: ${b.why}`); console.log("VERDICT: BLIND"); process.exit(EXIT.BLIND); }
  try {
    execFileSync("git", ["fetch", "--no-tags", "--depth=1", "origin", b.base], { cwd: REPO, stdio: ["ignore", "ignore", "pipe"] });
  } catch (e) {
    console.log(`BLIND: the base could not be fetched (${e instanceof Error ? e.message.split("\n")[0] : String(e)})`);
    console.log("VERDICT: BLIND");
    process.exit(EXIT.BLIND);
  }
  scopeAgainst(b.base);
} else if (process.argv.includes("--changed-since")) {
  scopeAgainst(process.argv[process.argv.indexOf("--changed-since") + 1]);
}
{
  const html = readFileSync(new URL("../pages/support.html", import.meta.url), "utf8");
  const r = await check(html, (u, init) => fetch(u, init));
  for (const l of r.lines) console.log(l);
  console.log(`VERDICT: ${r.verdict}`);
  process.exit(EXIT[r.verdict]);
}
