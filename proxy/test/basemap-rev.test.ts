// MD4 (docs/basemap-route.md A1.7, A2.35): the rev guard. The builder's output on three synthetic stores is pinned per
// `rev` in test/fixtures/basemap/rev.json, so a change to what the same inputs build cannot ship without a rev bump --
// which changes every ETag, so no device keeps an old package under a new meaning. When this fails on purpose: bump
// BASEMAP_REV and add the new rev's line; never edit an old one.
import { describe, expect, it } from "vitest";
import { build } from "../src/basemap/build";
import { BASEMAP_REV } from "../src/basemap/handler";
import { COMMIT, mbStore, me2Store, me7Store } from "./basemap-synth";
import rev from "./fixtures/basemap/rev.json";

const sha = async (b: Uint8Array) => [...new Uint8Array(await crypto.subtle.digest("SHA-256", b))].map((x) => x.toString(16).padStart(2, "0")).join("");

describe("MD4, the rev guard", () => {
  it("MD4: at the current rev, the builder's output on 3 synthetic stores is the committed output", async () => {
    const pinned = (rev as Record<string, string[]>)[String(BASEMAP_REV)];
    expect(pinned, `rev.json has no line for rev ${BASEMAP_REV}`).toBeDefined();
    const got: string[] = [];
    for (const s of [await mbStore("A8"), await me7Store(), await me2Store(0)]) {
      const r = await build(s.store, s.query, { commit: COMMIT });
      if (!r.ok) throw new Error(`MD4's fixture did not build: ${r.status} (BLIND)`);
      got.push(await sha(r.bytes));
    }
    expect(got).toEqual(pinned);
  });
});
