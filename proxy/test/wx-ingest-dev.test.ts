// Plan S (docs/wx-plan-s.md): production's checkAll still checks every product, and the staging copy's entry does
// exactly what its MODE says and nothing else. PS1-PS6; each has a sabotage recorded in the doc.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import type { Fetch, IngestOpts } from "../src/wx/ingest";
import { PLANT_MARKER, devCheck, plant } from "../src/wx/ingest-dev-worker";
import { checkAll } from "../src/wx/ingest-worker";
import { PRODUCTS } from "../src/wx/products";

const FAST: IngestOpts = { list: { timeoutMs: 60, attempts: 1, waitsMs: [] }, get: { timeoutMs: 60, attempts: 1, waitsMs: [] } };
/** Every upstream answers a final 404: each product's check ends in one line, with no frame to build. */
const fetch404: Fetch = async () => new Response("", { status: 404 });
/** A fetch and a bucket that fail the test if anything touches them. */
const noFetch: Fetch = () => { throw new Error("the plant fetched"); };
const noBucket = new Proxy({}, { get(_t, prop) { throw new Error(`the plant touched the bucket (${String(prop)})`); } }) as R2Bucket;
const products = (lines: string[]) => lines.map((l) => JSON.parse(l).product as string);
const byId = (id: string) => PRODUCTS.find((p) => p.id === id)!;

describe("Plan S: production's checkAll, and the staging copy's entry", () => {
  it("PS1 production's checkAll, called with no product list, checks every product: the 11", async () => {
    const lines: string[] = [];
    await checkAll({ WX: env.WX! }, ":00", fetch404, (l) => lines.push(l), FAST);
    expect(products(lines).sort()).toEqual(PRODUCTS.map((p) => p.id).sort());
    expect(PRODUCTS).toHaveLength(11);
  });

  it("PS2 checkAll with one product checks only that one", async () => {
    for (const id of ["opuk", "cref"]) {
      const lines: string[] = [];
      await checkAll({ WX: env.WX! }, ":00", fetch404, (l) => lines.push(l), FAST, [byId(id)]);
      expect(products(lines)).toEqual([id]);
    }
  });

  it("PS3 MODE plant runs the loop and touches no bucket and no upstream", async () => {
    const lines: string[] = [];
    await devCheck({ WX: noBucket, MODE: "plant" }, ":00", noFetch, (l) => lines.push(l), FAST, 1000);
    await devCheck({ WX: noBucket, MODE: "plant" }, ":30", noFetch, (l) => lines.push(l), FAST, 1000);
    const got = lines.map((l) => JSON.parse(l));
    expect(got.map((g) => [g.evt, g.check, g.iter, g.result])).toEqual([
      [PLANT_MARKER, ":00", 1000, plant(1000)],
      [PLANT_MARKER, ":30", 1000, plant(1000)],
    ]);
  });

  it("PS4 MODE off, and MODE absent: one line each, nothing touched", async () => {
    const lines: string[] = [];
    await devCheck({ WX: noBucket, MODE: "off" }, ":00", noFetch, (l) => lines.push(l), FAST, 1000);
    await devCheck({ WX: noBucket }, ":00", noFetch, (l) => lines.push(l), FAST, 1000);
    expect(lines.map((l) => JSON.parse(l)).map((g) => [g.evt, g.mode])).toEqual([["wx_dev_off", "off"], ["wx_dev_off", "off"]]);
  });

  it("PS5 an unknown MODE logs one error line and touches nothing", async () => {
    const lines: string[] = [];
    await devCheck({ WX: noBucket, MODE: "bogus" }, ":00", noFetch, (l) => lines.push(l), FAST, 1000);
    expect(lines.map((l) => JSON.parse(l)).map((g) => [g.evt, g.mode])).toEqual([["wx_dev_mode_error", "bogus"]]);
  });

  it("PS6 MODE cref and MODE opuk check only their own product", async () => {
    for (const id of ["cref", "opuk"]) {
      const lines: string[] = [];
      await devCheck({ WX: env.WX!, MODE: id }, ":00", fetch404, (l) => lines.push(l), FAST);
      expect(products(lines)).toEqual([id]);
    }
  });
});
