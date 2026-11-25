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
//   node scripts/check-support-links.mjs --selftest   planted HTML and an injected fetch, no network
import { readFileSync } from "node:fs";

const CONTROL = "https://valarscopes.com/blipscope/";
const UA = "BlipscopeLinkCheck/1 (+https://github.com/Valar-Systems/valar-scopes)";
const EXIT = { PASS: 0, FAILED: 1, BLIND: 2, OUTSIDE: 3 };

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
  const fails = rows.filter((r) => !r).length;
  console.log(fails ? `SELFTEST FAILED: ${fails}` : "SELFTEST PASSED");
  return fails ? 1 : 0;
}

if (process.argv.includes("--selftest")) {
  process.exit(await selftest());
} else {
  const html = readFileSync(new URL("../pages/support.html", import.meta.url), "utf8");
  const r = await check(html, (u, init) => fetch(u, init));
  for (const l of r.lines) console.log(l);
  console.log(`VERDICT: ${r.verdict}`);
  process.exit(EXIT[r.verdict]);
}
