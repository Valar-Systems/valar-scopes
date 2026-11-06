import { describe, it, expect } from "vitest";
import { enrollHtml } from "../src/enrollpage";
import { SUPPORT_EMAIL } from "../src/support";

/* ===========================================================================
 * THE ENROL PAGE'S INLINE SCRIPT, ACTUALLY EXECUTED.
 *
 * WHY THIS FILE EXISTS. Turnstile refreshes an expiring token by re-invoking
 * the page's data-callback. An onSolve that submits every time it is called is
 * therefore a ~5-minute timer, and on 2026-09-01 that was measured in
 * production: one board's ledger read 1,988 enrolments over 20 days, and the
 * fleet-wide daily counter sat at 594/595/597 on three consecutive days -- two
 * tabs, beating around the clock, on days nobody was at the bench.
 *
 * It hid because re-enrolment is idempotent by design: the key is a pure
 * function of the id, so every repeat returned the identical key and every
 * device kept working. There was no symptom to notice. A regression here would
 * be equally silent, which is exactly the kind that needs a test rather than a
 * comment.
 *
 * WHY IT RUNS THE SCRIPT INSTEAD OF GREPPING FOR THE GUARD. Asserting that the
 * source contains "submitted" is substring matching against the shape the
 * author imagined the bug would take -- the failure mode this repo has been
 * bitten by repeatedly. The defect is behavioural ("onSolve submits twice"), so
 * the check has to be behavioural too: call it twice, count the fetches.
 *
 * There is no jsdom here (workers pool), so the handful of DOM calls the script
 * makes are stubbed by hand. That is a feature: the stub is small enough to
 * read, and if the page grows a dependency this file fails loudly rather than
 * silently testing less than it claims.
 * ======================================================================== */

interface Harness {
  /** null when the script bailed before installing the callback (no valid id). */
  solve: ((token: string) => void) | null;
  posts: Array<{ url: string; body: unknown }>;
  /** The stubbed elements, so a test can read what the page rendered. */
  nodes: Record<string, { hidden: boolean; className: string; innerHTML: string; textContent: string }>;
}

/** Execute the page's inline <script> against a minimal stub environment. */
function runPage(query: string, fetchImpl?: (url: string, init: any) => Promise<any>): Harness {
  const html = enrollHtml("test-sitekey");
  const m = html.match(/<script>\n([\s\S]*?)<\/script>/);
  if (!m) throw new Error("could not find the inline script in the enrol page");
  const source = m[1];

  const posts: Harness["posts"] = [];
  const el = () => ({ hidden: false, className: "", innerHTML: "", textContent: "" });
  const nodes: Record<string, ReturnType<typeof el>> = {
    out: el(), gate: el(), devid: el(), hint: el(), fallback: el(),
  };

  const win: any = {};
  const doc = {
    getElementById(idAttr: string) {
      if (!nodes[idAttr]) throw new Error(`page asked for unknown element #${idAttr}`);
      return nodes[idAttr];
    },
  };
  const fetchStub = fetchImpl ?? ((url: string, init: any) => {
    posts.push({ url, body: JSON.parse(init.body) });
    return Promise.resolve({
      status: 200,
      json: () => Promise.resolve({ status: "enrolled", id: "0123456789abcdef", key: "k", enrollments: 1 }),
    });
  });

  // eslint-disable-next-line @typescript-eslint/no-implied-eval
  const fn = new Function(
    "window", "document", "location", "fetch", "setTimeout", "URLSearchParams",
    source,
  );
  fn(win, doc, { search: query }, fetchStub, () => 0, URLSearchParams);

  return { solve: typeof win.onSolve === "function" ? win.onSolve : null, posts, nodes };
}

describe("enrol page submits once per load", () => {
  it("posts exactly one enrolment for one solve", async () => {
    const h = runPage("?id=0123456789abcdef");
    h.solve!("token-1");
    await Promise.resolve();
    expect(h.posts.length).toBe(1);
    expect(h.posts[0].url).toBe("/blipscope/enroll");
    expect(h.posts[0].body).toEqual({ id: "0123456789abcdef", token: "token-1" });
  });

  it("IGNORES the token refreshes Turnstile fires on its own", async () => {
    const h = runPage("?id=0123456789abcdef");
    // Exactly what a tab left open does: the widget re-invokes the callback
    // with a fresh token every few minutes, forever.
    h.solve!("token-1");
    h.solve!("token-2");
    h.solve!("token-3");
    await Promise.resolve();
    expect(h.posts.length).toBe(1);
    // ...and it is the FIRST token that was used, not a later one -- proving the
    // guard short-circuits rather than merely coalescing.
    expect((h.posts[0].body as { token: string }).token).toBe("token-1");
  });

  it("does not resume submitting after a server error", async () => {
    // A revoked board 403s every time. If the guard reset on failure, the
    // refresh cycle would turn a terminal error into a permanent loop -- the
    // original bug wearing a different hat.
    const posts: Array<{ url: string; body: unknown }> = [];
    const h = runPage("?id=0123456789abcdef", (url, init) => {
      posts.push({ url, body: JSON.parse(init.body) });
      return Promise.resolve({ status: 403, json: () => Promise.resolve({ error: "revoked" }) });
    });
    h.solve!("token-1");
    await Promise.resolve();
    await Promise.resolve();
    h.solve!("token-2");
    h.solve!("token-3");
    await Promise.resolve();
    expect(posts.length).toBe(1);
  });

  it("never installs a callback, let alone submits, without a usable device id", () => {
    const h = runPage("?id=not-a-device-id");
    expect(h.solve).toBeNull();
    expect(h.posts.length).toBe(0);
  });
});

/* ===========================================================================
 * A2 (docs/device-activation.md): a refusal a customer can act on in one tap.
 * The page is run, not grepped: the id must reach the rendered text AND both
 * halves of the pre-filled email, decoded the way a mail client decodes them.
 * ======================================================================== */
describe("A2: the activation refusals carry the id and a pre-filled email", () => {
  const ID = "0123456789abcdef";
  const refuse = (status: number, error: string) => (_url: string, _init: any) =>
    Promise.resolve({ status, json: () => Promise.resolve({ error, id: ID, support: SUPPORT_EMAIL }) });
  const flush = async () => { for (let i = 0; i < 6; i++) await Promise.resolve(); };
  const mailto = (html: string) => {
    const m = html.match(/href="mailto:([^"?]+)\?([^"]+)"/);
    if (!m) return null;
    const q = new URLSearchParams(m[2]!);
    return { to: m[1], subject: q.get("subject") ?? "", body: q.get("body") ?? "" };
  };

  it("not_listed: the id, the address, the email link and the own-receiver line", async () => {
    const h = runPage(`?id=${ID}`, refuse(403, "not_listed"));
    h.solve!("token-1");
    await flush();
    const html = h.nodes.out.innerHTML;
    expect(html).toContain("not registered as a Valar unit");
    expect(html).toContain(ID);
    expect(html).toContain(SUPPORT_EMAIL);
    expect(html).toContain("own receiver");
    const link = mailto(html);
    expect(link).not.toBeNull();
    expect(link!.to).toBe(SUPPORT_EMAIL);
    expect(link!.subject).toContain(ID);
    expect(link!.body).toContain(ID);
  });

  it("CONTROL already_activated: its own message, also carrying the id", async () => {
    const h = runPage(`?id=${ID}`, refuse(409, "already_activated"));
    h.solve!("token-1");
    await flush();
    const html = h.nodes.out.innerHTML;
    expect(html).toContain("already been activated");
    expect(html).not.toContain("not registered as a Valar unit");
    expect(mailto(html)!.body).toContain(ID);
  });
});
