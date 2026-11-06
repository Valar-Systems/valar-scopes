// ME2 and MD1 (docs/basemap-route.md A1.7): build.ts over ME2's 1,000 seeded synthetic stores (basemap-synth.ts,
// committed before build.ts). Each test is named by its case id.
import { describe, expect, it } from "vitest";
import { build } from "../src/basemap/build";
import { decode, validate } from "../src/basemap/format";
import { COMMIT, me2Store } from "./basemap-synth";

const sha = async (b: Uint8Array) => [...new Uint8Array(await crypto.subtle.digest("SHA-256", b))].map((x) => x.toString(16).padStart(2, "0")).join("");

describe("ME2 and MD1, over the synthetic stores", () => {
  it("ME2: 1,000 builds, 0 validator refusals, PROVENANCE <= 3,072 B in every one", async () => {
    const tally = { built: 0, tooLarge: 0, other: [] as string[], refused: [] as string[], provOver: [] as number[], maxProv: 0 };
    for (let i = 0; i < 1000; i++) {
      const s = await me2Store(i);
      const r = await build(s.store, s.query, { commit: COMMIT });
      await new Promise((res) => setTimeout(res, 0)); // yield, or workerd drops the pool's connection after ~5 s (A2.26)
      if (!r.ok) { if (r.status === 422) tally.tooLarge++; else tally.other.push(`#${i} ${r.status} ${r.code}`); continue; }
      tally.built++;
      const v = validate(r.bytes);
      if (v.status !== "kOk") { tally.refused.push(`#${i} ${v.status}`); continue; }
      const p = decode(r.bytes).sections.find((x) => x.type === 7) as { text: Uint8Array };
      tally.maxProv = Math.max(tally.maxProv, p.text.length);
      if (p.text.length > 3072) tally.provOver.push(i);
    }
    console.log(`ME2: ${tally.built} built, ${tally.tooLarge} too_large, ${tally.other.length} other; largest PROVENANCE ${tally.maxProv} B`);
    // every store must be READ: a store answered as anything but a package or too_large is BLIND for this row
    expect(tally.other).toEqual([]);
    expect(tally.refused).toEqual([]);
    expect(tally.provOver).toEqual([]);
    // CONTROL: the row graded real packages, not a run of too_large
    expect(tally.built).toBeGreaterThan(900);
  }, 900_000);

  it("MD1: the same request twice in one isolate gives the same bytes, on ME2's first 50 stores", async () => {
    let same = 0;
    for (let i = 0; i < 50; i++) {
      const s = await me2Store(i);
      const a = await build(s.store, s.query, { commit: COMMIT }), b = await build(s.store, s.query, { commit: COMMIT });
      await new Promise((res) => setTimeout(res, 0));
      expect(a.ok, `#${i}`).toBe(b.ok);
      if (a.ok && b.ok && (await sha(a.bytes)) === (await sha(b.bytes))) same++;
      else if (!a.ok && !b.ok && a.status === b.status) same++;
    }
    expect(same).toBe(50);
  }, 300_000);
});
