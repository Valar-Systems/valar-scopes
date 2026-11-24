// Every route the router dispatches has its own Analytics Engine label (docs/route-labels.md).
//
// The input is taken from the router's OWN source, not from a list typed here: a route added to the dispatch without a label
// fails this test, under both prefixes. That is the guard /basemap went round (added 2026-10-29 by #486, recorded as "/other"
// ever since), and Follow's airport calls and the legacy /v1/wx paths with it.
import { describe, expect, it, vi } from "vitest";
import { record, routeTemplate } from "../src/metrics";
import type { Env } from "../src/types";
import { PRODUCTS } from "../src/wx/products";
import { ROUTES as WEATHERSCOPE_ROUTES } from "../src/weatherscope/handler";
import indexSrc from "../src/index.ts?raw";

const edition = /const EDITION = "([a-z]+)";/.exec(indexSrc)?.[1];
const legacy = /const API_LEGACY_PREFIX = "([^"]+)";/.exec(indexSrc)?.[1];
const weatherscopePrefix = /const WEATHERSCOPE_API_PREFIX = "([^"]+)";/.exec(indexSrc)?.[1];
const prefixes = edition && legacy ? [`/api/v1/${edition}/`, legacy] : [];

// The dispatch's exact suffixes and its one-segment patterns, as the router writes them.
const exact = [...new Set([...indexSrc.matchAll(/api\.suffix === "([a-z0-9_]+)"/g)].map((m) => m[1] as string))].sort();
const patterns = [...new Set([...indexSrc.matchAll(/api\.suffix\.match\(\/\^([a-z0-9_]+)\\\/\(\[\^\/\]\+\)\$\/\)/g)]
  .map((m) => m[1] as string))].sort();
// One example per pattern; wx takes every product, the same list KR2 reads.
const examples = (p: string): string[] => (p === "wx" ? PRODUCTS.map((x) => `wx/${x.id}`) : [`${p}/EXAMPLE1`]);
const suffixes = [...exact, ...patterns.flatMap(examples)];
const paths = [
  ...prefixes.flatMap((pre) => suffixes.map((s) => `${pre}${s}`)),
  ...(weatherscopePrefix ? WEATHERSCOPE_ROUTES.map((s) => `${weatherscopePrefix}${s}`) : []),
];

describe("route labels: the router's own dispatch, read from its source", () => {
  it("RL0 the parse found the dispatch (a control: a test over nothing passes nothing)", () => {
    expect(edition).toBe("blipscope");
    expect(prefixes).toEqual(["/api/v1/blipscope/", "/v1/"]);
    expect(weatherscopePrefix).toBe("/api/v1/weatherscope/");
    for (const s of ["blips", "config", "airports", "leaderboard", "basemap"]) expect(exact, s).toContain(s);
    for (const p of ["airport", "enrich", "photo", "wx"]) expect(patterns, p).toContain(p);
    expect(PRODUCTS.length).toBeGreaterThanOrEqual(12);
  });

  it.each(paths)("RL1 %s has its own label, not /other", (path) => {
    expect(routeTemplate(path)).not.toBe("/other");
  });
});

describe("route labels: an airport request stores the template, never the code", () => {
  it.each(["/api/v1/blipscope/airport/KBOS", "/v1/airport/KBOS"])("RL2 %s", (ep) => {
    const writeDataPoint = vi.fn();
    record({ METRICS: { writeDataPoint } } as unknown as Env,
      { ep, status: 200, ms: 3, model: "s3-128", colo: "SEA", cache: "MISS", dev: "0123456789abcdef", fw: "19" });
    const point = writeDataPoint.mock.calls[0]?.[0];
    const template = ep.startsWith("/v1/") ? "/v1/airport/:code" : "/api/v1/blipscope/airport/:code";
    expect(point.blobs[0]).toBe(template);
    expect(point.indexes).toEqual([template]);
    for (const v of [...point.blobs, ...point.indexes]) expect(String(v)).not.toContain("KBOS");
  });
});
