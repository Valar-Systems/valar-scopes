/// <reference types="vite/client" />
// MH1-MH3 (docs/basemap-deploy.md, the D0 + D2 PR's rows; Q4 as ruled): /healthz's `basemap` field, through the
// proxy's own dispatch. A stub builder answers syn-min's bytes, so no store and no build are involved: these rows are
// about what /healthz reports, and from where. Each test is named by its case id.
import { describe, expect, it } from "vitest";
import { BASEMAP_REV, resetBasemapIsolate } from "../src/basemap/handler";
import { validate } from "../src/basemap/format";
import synMin from "../../test/fixtures/wx/basemap/syn-min.bmpk";
import { apiRequest, call } from "./helpers";

const STORE_ID = "0123456789ab", BUILDER_COMMIT = "fedcba9876543210fedcba9876543210fedcba98"; // synthetic, labelled so
const health = async (env: Parameters<typeof call>[1]) => {
  const res = await call(new Request("https://proxy.test/healthz"), env);
  expect(res.status).toBe(200);
  return (await res.json() as { basemap: unknown }).basemap;
};
/** A stub BASEMAP binding answering syn-min's bytes with the given store id and commit, its calls counted. */
function stub() {
  const n = { calls: 0 };
  const BASEMAP = {
    async fetch() { n.calls++; return new Response(synMin, { headers: { "x-basemap-store": STORE_ID, "x-basemap-commit": BUILDER_COMMIT } }); },
  } as unknown as Fetcher;
  return { BASEMAP, n };
}

describe("MH, /healthz's basemap field", () => {
  it("MH1: no BASEMAP binding is \"not_configured\"", async () => {
    expect(await health({ BASEMAP: undefined })).toBe("not_configured");
  });

  it("MH2: bound, before any /basemap request in the isolate, is {rev, store: null, commit: null}", async () => {
    resetBasemapIsolate();
    const b = stub();
    expect(await health({ BASEMAP: b.BASEMAP })).toEqual({ rev: BASEMAP_REV, store: null, commit: null });
    expect(b.n.calls).toBe(0); // /healthz never calls the builder
  });

  it("MH3: after one /basemap 200, it is the builder's own store id and commit", async () => {
    resetBasemapIsolate();
    expect(validate(synMin).status).toBe("kOk"); // CONTROL: the stub's package passes the handler's validate
    const b = stub();
    const res = await call(apiRequest("/api/v1/blipscope/basemap?lat=0.37&lon=0.59&r=97&eps=100584&layers=water,roads"), { BASEMAP: b.BASEMAP });
    expect(res.status).toBe(200);
    expect(b.n.calls).toBe(1);
    expect(await health({ BASEMAP: b.BASEMAP })).toEqual({ rev: BASEMAP_REV, store: STORE_ID, commit: BUILDER_COMMIT });
  });
});
