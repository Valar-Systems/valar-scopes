// W4, MRMS SeamlessHSR (docs/weatherscope-stage4.md S4.12.3-S4.12.5, re-read in S4.25): the rows registered before this
// code. X2 is gen-wx-region.mjs --check; X5 sits beside H1 in wx-endpoint.test.ts; X7 beside FT1 and FT2 in
// wx-ingest-split.test.ts; X11's pins move in their own files. The sabotages are proxy/scripts/sabotage-wx-shsr.mjs.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { routeTemplate } from "../src/metrics";
import { type Fetch, NOAA_BUCKET, noaaKeys } from "../src/wx/ingest";
import * as mrmsEntry from "../src/wx/ingest-mrms-worker";
import { CREF_WORKER } from "../src/wx/ingest-worker";
import { codeOf } from "../src/wx/mrms";
import { type Product, PRODUCTS, codeOfComposite, productById } from "../src/wx/products";
import { servedProduct } from "../src/wx/region";

const OLD_IDS = ["cref", "opuk", "opis", "opib", "opmt", "opwe", "opce", "opse", "opba", "opsw", "opsn"];
const shsr = () => productById("shsr") as Product;

// X7 runs the MRMS entry's REAL scheduled handler the way test/wx-ingest-split.test.ts runs the others (kept out of that
// file so its test count, which proxy/scripts/sabotage-wx-split.py pins at 5, does not move): scheduledTime 31 s in the
// past (no wait), a global fetch that answers 404, console.log captured; `guarded` makes any bucket or fetch touch count.
type Entry = { default: { scheduled(c: ScheduledController, e: never): Promise<void> } };
async function run(entry: Entry, vars: Record<string, string>, guarded = false) {
  const lines: string[] = [];
  let touched = 0;
  const wx = guarded
    ? (new Proxy({}, { get() { touched++; throw new Error("the bucket was touched"); } }) as R2Bucket)
    : env.WX!;
  const realLog = console.log, realFetch = globalThis.fetch;
  console.log = (...a: unknown[]) => { lines.push(String(a[0])); };
  globalThis.fetch = (async () => {
    if (guarded) { touched++; throw new Error("fetch was called"); }
    return new Response("", { status: 404 });
  }) as typeof fetch;
  try {
    const ctl = { scheduledTime: Date.now() - 31_000, cron: "* * * * *", noRetry() {} } as unknown as ScheduledController;
    await entry.default.scheduled(ctl, { WX: wx, ...vars } as never);
  } finally {
    console.log = realLog;
    globalThis.fetch = realFetch;
  }
  return { lines, touched };
}
const productsOf = (lines: string[]) => lines.map((l) => { try { return JSON.parse(l).product; } catch { return undefined; } })
  .filter((p): p is string => typeof p === "string");

describe("W4: shsr", () => {
  it("X1 productById('shsr') is the registered row, appended after the eleven, which keep their places", () => {
    const p = shsr();
    expect(p).not.toBeNull();
    const { codeOf: fn, ...rest } = p;
    expect(rest).toEqual({ id: "shsr", source: "mrms", noaaPrefix: "CONUS/SeamlessHSR_00.00",
                           noaaStem: "MRMS_SeamlessHSR_00.00", cadenceS: 120, maxAgeS: 600 });
    expect(fn).toBe(codeOfComposite);
    expect("deviceMaxAgeS" in p).toBe(false);
    expect(PRODUCTS.map((q) => q.id)).toEqual([...OLD_IDS, "shsr"]);
    const { codeOf: crefFn, ...cref } = productById("cref")!;
    expect(cref).toEqual({ id: "cref", source: "mrms", noaaPrefix: "CONUS/MergedReflectivityQCComposite_00.50",
                           noaaStem: "MRMS_MergedReflectivityQCComposite_00.50", cadenceS: 120, maxAgeS: 600 });
    expect(crefFn).toBe(codeOfComposite);
  });

  it("X3 the row's code mapping equals mrms.ts's codeOf (no missing value) over the sweep", () => {
    const sweep = [-999, -99, -0.5, 0, 14.999, 15, 19.999, 20, 79.999, 80, 200];
    expect(sweep.map((v) => shsr().codeOf(v))).toEqual(sweep.map((v) => codeOf(v)));
    expect(sweep.map((v) => codeOfComposite(v))).toEqual(sweep.map((v) => codeOf(v)));
  });

  it("X4 servedProduct(shsr, ...) is shsr at every test point, by the table and by the window", () => {
    const points: [number, number][] = [[40, -100], [30, -90], [52, -1], [48, 10], [64, -20], [0, 0], [-33, 151]];
    for (const [lat, lon] of points) {
      expect(servedProduct(shsr(), lat, lon).id, `${lat},${lon}`).toBe("shsr");
      expect(servedProduct(shsr(), lat, lon, { byWindow: true }).id, `${lat},${lon} byWindow`).toBe("shsr");
    }
  });

  // W4 (docs/weatherscope-stage4.md S4.12.4, X7): the MRMS entry checks exactly its PRODUCT, refuses an OPERA id or none,
  // and with WX_INGEST not "on" logs its stop and makes 0 fetches; CREF_WORKER stays exactly cref.
  it("X7a the MRMS entry, WX_INGEST on, PRODUCT shsr, checks exactly shsr", async () => {
    const { lines } = await run(mrmsEntry as unknown as Entry, { PRODUCT: "shsr", WX_INGEST: "on" });
    expect(productsOf(lines)).toEqual(["shsr", "shsr"]);
  });
  it("X7b the MRMS entry, WX_INGEST on, with a PRODUCT that names no MRMS product, logs an error and touches nothing", async () => {
    for (const vars of [{ PRODUCT: "opuk", WX_INGEST: "on" }, { PRODUCT: "nope", WX_INGEST: "on" }, { WX_INGEST: "on" }] as Record<string, string>[]) {
      const { lines, touched } = await run(mrmsEntry as unknown as Entry, vars, true);
      expect(lines.map((l) => JSON.parse(l).evt), JSON.stringify(vars)).toEqual(["wx_ingest_error", "wx_ingest_error"]);
      expect(touched).toBe(0);
    }
  });
  it("X7c the MRMS entry, WX_INGEST off or absent, logs wx_ingest_off and makes 0 fetches", async () => {
    for (const vars of [{ PRODUCT: "shsr", WX_INGEST: "off" }, { PRODUCT: "shsr" }] as Record<string, string>[]) {
      const { lines, touched } = await run(mrmsEntry as unknown as Entry, vars, true);
      expect(lines.map((l) => JSON.parse(l).evt), JSON.stringify(vars)).toEqual(["wx_ingest_off", "wx_ingest_off"]);
      expect(touched).toBe(0);
    }
  });
  it("X7d CREF_WORKER is still exactly cref", () => {
    expect(CREF_WORKER.map((p) => p.id)).toEqual(["cref"]);
  });

  it("X6 routeTemplate keeps /api/v1/blipscope/wx/shsr as itself", () => {
    expect(routeTemplate("/api/v1/blipscope/wx/shsr")).toBe("/api/v1/blipscope/wx/shsr");
  });

  describe("X8 the NOAA names come from the row alone", () => {
    const listingUrls = async (p: Product, nowMs: number) => {
      const seen: string[] = [];
      const doFetch: Fetch = async (u) => { seen.push(u); return new Response("<ListBucketResult></ListBucketResult>", { status: 200 }); };
      await noaaKeys(p, nowMs, doFetch);
      return seen;
    };
    const NOW = Date.UTC(2026, 10, 12, 12, 0, 0);   // 20 min back is 11:40:00, the same day: one listing
    it("X8a shsr's listing URL is built from its prefix and stem, exactly", async () => {
      const prefix = "CONUS/SeamlessHSR_00.00/20261112/";
      const after = "CONUS/SeamlessHSR_00.00/20261112/MRMS_SeamlessHSR_00.00_20261112-114000.grib2.gz";
      expect(await listingUrls(shsr(), NOW)).toEqual([
        `${NOAA_BUCKET}/?list-type=2&prefix=${encodeURIComponent(prefix)}&start-after=${encodeURIComponent(after)}`]);
    });
    it("X8b CONTROL: the same row renamed lists the renamed folder, so nothing else names it", async () => {
      const urls = await listingUrls({ ...shsr(), noaaPrefix: "CONUS/Renamed_00.00", noaaStem: "MRMS_Renamed_00.00" }, NOW);
      expect(urls).toHaveLength(1);
      expect(decodeURIComponent(urls[0]!)).toContain("prefix=CONUS/Renamed_00.00/20261112/");
      expect(decodeURIComponent(urls[0]!)).not.toContain("SeamlessHSR");
    });
    it("X8c no SeamlessHSR literal in proxy/src outside products.ts", () => {
      const src = import.meta.glob<string>("../src/**/*.ts", { query: "?raw", import: "default", eager: true });
      // CONTROL: the glob read the source tree, products.ts included, or the scan saw nothing and proves nothing
      expect(Object.keys(src).length).toBeGreaterThan(20);
      expect(Object.keys(src)).toContain("../src/wx/products.ts");
      expect(Object.entries(src).filter(([, t]) => t.includes("SeamlessHSR")).map(([f]) => f)).toEqual(["../src/wx/products.ts"]);
    });
  });
});
