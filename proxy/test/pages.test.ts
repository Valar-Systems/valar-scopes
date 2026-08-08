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
