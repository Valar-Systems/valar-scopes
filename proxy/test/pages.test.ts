import { describe, expect, it } from "vitest";
import { KNOWN_ROUTES } from "../src/metrics";
import { indexHtml, supportHtml } from "../src/pages.generated";
import { call } from "./helpers";

// The two static pages: the edition hub at "/" and Blipscope's support page.
//
// "/" was an unrouted 404 until this landed, which meant anyone who trimmed the
// path off a leaderboard link -- or followed the store's redirect -- got a JSON
// error object. docs/web-url-convention.md had always reserved the root for a
// hub listing the editions; this is that page.
const PAGES: ReadonlyArray<{ path: string; html: string; title: string }> = [
  { path: "/", html: indexHtml, title: "Valar Scopes" },
  { path: "/blipscope/support", html: supportHtml, title: "Blipscope Support" },
];

const hrefs = (html: string): string[] =>
  [...html.matchAll(/href="([^"]+)"/g)].map((m) => m[1] as string);

describe("static pages", () => {
  for (const { path, title } of PAGES) {
    it(`serves ${path} as cacheable HTML`, async () => {
      const res = await call(new Request(`https://proxy.test${path}`));
      expect(res.status).toBe(200);
      expect(res.headers.get("Content-Type")).toBe("text/html; charset=utf-8");
      expect(res.headers.get("Cache-Control")).toBe("public, max-age=300");
      const body = await res.text();
      expect(body).toContain(`<title>${title}</title>`);
      // Content-Length is set by hand from the encoded bytes, so a multi-byte
      // character in the markup would desync it from the body. Browsers truncate
      // on a short Content-Length rather than erroring, which is silent.
      expect(Number(res.headers.get("Content-Length"))).toBe(
        new TextEncoder().encode(body).byteLength,
      );
    });
  }

  it("rejects a non-GET to a static page", async () => {
    // The method gate sits above these routes; asserted so a future reshuffle
    // that moves them above it gets caught.
    const res = await call(new Request("https://proxy.test/", { method: "POST" }));
    expect(res.status).toBe(405);
  });

  it("lists both pages in KNOWN_ROUTES so they are visible in analytics", () => {
    // An unlisted path buckets to "/other". The root is where store traffic
    // lands, so losing it in the noise defeats the point of having metrics.
    for (const { path } of PAGES) expect(KNOWN_ROUTES.has(path), path).toBe(true);
  });
});

describe("page links do not rot", () => {
  // THIS IS THE TEST THAT WOULD HAVE CAUGHT #142. When /leaderboard moved to
  // /blipscope/leaderboard, nothing asserted that the links we ship actually
  // resolve -- so a page could advertise a dead path and every other test would
  // still pass. Every same-origin href on these pages is fetched for real.
  for (const { path, html } of PAGES) {
    it(`every same-origin link on ${path} resolves`, async () => {
      const internal = hrefs(html).filter(
        (h) =>
          h.startsWith("/") &&
          // /missileer/* is reverse-proxied to valar-eam-feed (see missileer.ts).
          // It is another repo's surface and is not reachable in-process, so it
          // cannot be verified here -- smoke-prod.sh is where those get checked.
          !h.startsWith("/missileer/"),
      );
      expect(internal.length).toBeGreaterThan(0);
      for (const href of internal) {
        const res = await call(new Request(`https://proxy.test${href}`));
        expect(res.status, `${path} links to ${href}`).toBe(200);
      }
    });

    it(`every in-page anchor on ${path} points at a real id`, () => {
      // A jump nav whose targets were renamed still renders perfectly and simply
      // does nothing when clicked.
      for (const href of hrefs(html).filter((h) => h.startsWith("#"))) {
        expect(html, `${path} anchor ${href}`).toContain(`id="${href.slice(1)}"`);
      }
    });

    it(`${path} links to no deprecated path`, () => {
      // The unprefixed paths still 301, so linking one would "work" and never be
      // noticed -- while costing every visitor a redirect and re-teaching the old
      // URL to anyone who copies it. New links use the namespaced form.
      for (const href of hrefs(html)) {
        expect(href, `${path} links to deprecated ${href}`).not.toMatch(
          /^\/(leaderboard(\.json)?|v1\/)/,
        );
      }
    });

    it(`${path} requests only fonts that exist`, async () => {
      // Same reasoning as the leaderboard's font check: a renamed font 404s
      // silently and drops the page to system type, which reads as a styling
      // opinion rather than a bug.
      const wanted = [...html.matchAll(/url\(\/fonts\/([^)]+)\)/g)].map((m) => m[1] as string);
      expect(wanted.length).toBe(3);
      for (const name of wanted) {
        const res = await call(new Request(`https://proxy.test/fonts/${name}`));
        expect(res.status, `${path} wants /fonts/${name}`).toBe(200);
      }
    });
  }
});

describe("a trimmed or slash-suffixed URL still lands somewhere", () => {
  // These are the ways a printed URL actually gets mistyped. /blipscope/support
  // is the destination of a QR code that cannot be reprinted, so the cost of a
  // JSON 404 here is not symmetric with the cost of one extra redirect.
  // The STATUS is part of the contract, not an implementation detail -- see the
  // 302 case below, where caching semantics are the whole point.
  it.each([
    ["/blipscope/support/", "/blipscope/support", 301],
    ["/blipscope/leaderboard/", "/blipscope/leaderboard", 301],
    ["/credits/", "/credits", 301],
    // Trimming one surface off the end of a page path lands on SUPPORT, not
    // the hub. Changed 2026-08-25: "/blipscope" is the short URL going on the
    // printed quick-start card, and a support QR must answer the question it
    // was scanned to ask rather than hand back a product index. Trimming
    // further, to "/", still reaches the hub -- covered below.
    // 302: this destination is deliberately re-pointable after the cards print.
    ["/blipscope", "/blipscope/support", 302],
  ])("%s redirects to %s (%i)", async (from, to, status) => {
    const res = await call(new Request(`https://proxy.test${from}`));
    expect(res.status).toBe(status);
    expect(new URL(res.headers.get("Location") as string).pathname).toBe(to);
  });

  it("the printed short URL redirects TEMPORARILY, so it can be re-pointed", async () => {
    // The printed QR resolves through /blipscope. A 301 is cached by browsers
    // indefinitely, so every customer who already scanned would keep the old
    // destination forever -- which negates the entire reason for routing the QR
    // through this Worker rather than printing the support URL directly.
    //
    // Asserted as its own test, not folded into the table above, because this is
    // a PRODUCT commitment (the card is unreprintable) rather than a routing
    // detail, and it should fail with a message that says so.
    const res = await call(new Request("https://proxy.test/blipscope"));
    expect(res.status, "/blipscope must be 302 -- the card cannot be reprinted").toBe(302);
    expect(res.status).not.toBe(301);
  });

  it("a mistyped PAGE path gets a page, not the API error envelope", async () => {
    // The print-gate check. A customer who mistypes the URL on the card, or
    // scans a QR that has aged badly, previously got
    // `{"v":1,"error":"not_found"}` -- which reads as a broken device rather
    // than a wrong address, and sends them to support about the wrong thing.
    const res = await call(new Request("https://proxy.test/blipscope/typo"));
    expect(res.status).toBe(404);
    expect(res.headers.get("Content-Type")).toContain("text/html");
    const body = await res.text();
    expect(body).not.toContain(String.fromCharCode(34) + "error" + String.fromCharCode(34));
    // It must route them somewhere, or it is just a prettier dead end.
    expect(body).toContain("/blipscope/support");
  });

  it("a mistyped API path never gets HTML -- machines get machine errors", async () => {
    // The other half, and the reason the human 404 sits where it does: an
    // API-shaped path resolves `api` non-null, so it goes THROUGH the auth gate
    // and can never be handed an HTML page a device would try to parse.
    //
    // Asserted on the CONTENT TYPE rather than the status, deliberately. The
    // first version of this test expected 404 and got 401 -- the auth gate
    // firing first, which is correct and load-bearing. Pinning 404 would have
    // pinned the wrong thing: what must hold is that a device never receives
    // markup, whatever the reason for the refusal.
    const res = await call(new Request("https://proxy.test/api/v1/blipscope/bogus", {
      headers: { "X-Blip-Key": "test-key" },
    }));
    expect(res.headers.get("Content-Type")).not.toContain("text/html");
    expect(res.status).toBeGreaterThanOrEqual(400);
  });

  it("the hub is still reachable, at the root", async () => {
    // The edition root now goes to support, so this is the check that the hub
    // did not become unreachable as a side effect of pointing the card at a
    // more useful page.
    const res = await call(new Request("https://proxy.test/"));
    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toContain("text/html");
  });

  it("never redirects an API path, however it is spelled", async () => {
    // A 301 on an API path would break deployed firmware -- HTTPClient is not
    // guaranteed to follow one, and on the leaderboard POST following it would
    // mean re-sending the body. The slash normaliser above must never reach here.
    for (const p of ["/v1/blips/", "/api/v1/blipscope/blips/", "/v1/leaderboard/"]) {
      const res = await call(new Request(`https://proxy.test${p}`, {
        headers: { "X-Blip-Key": "test-key" },
      }));
      expect(res.status, `${p} must not redirect`).not.toBe(301);
    }
  });
});

describe("the hub belongs to no single edition", () => {
  // The root is shared. A Blipscope-only root would mean a Missileer owner who
  // trimmed the path landed on another product's support page -- which is the
  // reason the hub exists rather than serving Blipscope support at "/".
  it("links onward to both editions that have a surface here", () => {
    const links = hrefs(indexHtml);
    expect(links).toContain("/blipscope/support");
    expect(links.some((h) => h.startsWith("/missileer/"))).toBe(true);
  });

  it("does not link a Missileer support page before one exists", () => {
    // /missileer/support has to ship in valar-eam-feed; linking it from here
    // first would point customers at a 404 generated by the origin. Delete this
    // test in the change that publishes that page and adds the link.
    expect(hrefs(indexHtml)).not.toContain("/missileer/support");
  });
});
