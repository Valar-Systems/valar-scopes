import { env } from "cloudflare:test";
import { afterEach, describe, expect, it, vi } from "vitest";
import worker, { flashFor } from "../src/index";
import type { Env } from "../src/types";

const E = env as unknown as Env;

afterEach(() => vi.unstubAllGlobals());

// Access is checked before ANYTHING reads storage or spends an API call.
describe("the front door", () => {
  it("is a flat 403 without a valid Access assertion", async () => {
    const spent = vi.fn();
    vi.stubGlobal("fetch", spent);
    for (const path of ["/", "/firmware", "/ota", "/gaps", "/fleet.json"]) {
      const res = await worker.fetch(new Request(`https://fleet.example${path}`), E);
      expect(res.status).toBe(403);
    }
    // No analytics call, no key fetch: an unauthenticated caller costs nothing.
    expect(spent).not.toHaveBeenCalled();
  });

  it("refuses the revoke POST too, and writes nothing", async () => {
    const before = await E.ENRICH_KV.get("cfg:revoked");
    const res = await worker.fetch(
      new Request("https://fleet.example/revoke", {
        method: "POST",
        body: new URLSearchParams({ dev: "0123456789abcdef", to: "1" }),
      }),
      E,
    );
    expect(res.status).toBe(403);
    expect(await E.ENRICH_KV.get("cfg:revoked")).toBe(before);
  });

  it("never sets a cacheable header on a refusal", async () => {
    const res = await worker.fetch(new Request("https://fleet.example/"), E);
    expect(res.headers.get("Cache-Control")).toBe("no-store");
  });
});

// The banner after a revoke round-trips through a redirect URL. It carries a
// CODE, never markup -- reflecting HTML back would be XSS on the one surface in
// this product that can take a customer's device off the air.
describe("flashFor", () => {
  it("renders the four real outcomes", () => {
    expect(flashFor("revoked", "0123456789abcdef")).toContain("REVOKED");
    expect(flashFor("restored", "0123456789abcdef")).toContain("active again");
    expect(flashFor("nochange", "0123456789abcdef")).toContain("nothing changed");
    expect(flashFor("bad", "whatever")).toContain("not a device id");
  });

  it("renders nothing for an unknown or absent code", () => {
    expect(flashFor(null, "x")).toBe("");
    expect(flashFor("", "x")).toBe("");
    expect(flashFor("<script>alert(1)</script>", "x")).toBe("");
  });

  it("cannot be used to inject markup through the device id", () => {
    const payloads = [
      '"><script>alert(1)</script>',
      "<img src=x onerror=alert(1)>",
      "javascript:alert(1)",
      "0123456789abcdef<script>",
    ];
    for (const p of payloads) {
      const out = flashFor("revoked", p);
      expect(out).not.toContain("<script");
      expect(out).not.toContain("onerror");
      expect(out).toContain("that device"); // fell back rather than echoing
    }
  });

  it("normalises a real id rather than rejecting it on case", () => {
    expect(flashFor("revoked", "0123456789ABCDEF")).toContain("0123456789abcdef");
  });
});
