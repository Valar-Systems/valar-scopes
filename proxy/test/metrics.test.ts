import { describe, expect, it, vi } from "vitest";
import {
  record,
  recordOtaMem,
  routeTemplate,
  setDeviceAttribution,
  type RequestMetric,
} from "../src/metrics";
import type { Env } from "../src/types";

function envWithSpy() {
  const writeDataPoint = vi.fn();
  return { env: { METRICS: { writeDataPoint } } as unknown as Env, writeDataPoint };
}

// A device sends this at most once per OTA attempt, on a check-in it was making
// anyway. It is device-supplied input reaching storage, so the shape is enforced
// here rather than trusted.
describe("recordOtaMem", () => {
  it("records a well-formed report as one Analytics Engine point", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, "4,5,46068,71668,ok", "s3-128", "2aeea64cb4b760b8");
    expect(writeDataPoint).toHaveBeenCalledTimes(1);
    expect(writeDataPoint).toHaveBeenCalledWith({
      blobs: ["ota", "ok", "s3-128", "2aeea64cb4b760b8"],
      doubles: [4, 5, 46068, 71668],
      indexes: ["ota"],
    });
  });

  it("records an empty device when the caller has no attribution", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, "4,5,46068,71668,ok", "s3-128");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[3]).toBe("");
  });

  it("carries the failure and incomplete verdicts through intact", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, "4,5,46068,45120,fail-8", "s3-128");
    recordOtaMem(env, "4,5,46068,73716,incomplete", "s3-128");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[1]).toBe("fail-8");
    expect(writeDataPoint.mock.calls[1]?.[0].blobs[1]).toBe("incomplete");
  });

  it("does nothing when the check-in carries no report", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, null, "s3-128");
    recordOtaMem(env, "", "s3-128");
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  it.each([
    ["wrong arity (short)", "4,5,46068,ok"],
    ["wrong arity (long)", "4,5,1,2,ok,extra"],
    ["non-numeric heap", "4,5,not-a-number,71668,ok"],
    ["negative heap", "4,5,-1,71668,ok"],
    ["past u32", "4,5,4294967296,71668,ok"],
    ["empty result", "4,5,46068,71668,"],
    ["oversized junk", `4,5,46068,71668,${"x".repeat(200)}`],
  ])("drops malformed input: %s", (_label, raw) => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, raw, "s3-128");
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  it("strips anything unexpected out of the result token", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, '4,5,46068,71668,ok"; DROP', "s3-128");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[1]).toBe("okDROP");
  });

  it("never throws when the metrics binding is absent", () => {
    expect(() => recordOtaMem({} as Env, "4,5,46068,71668,ok", "s3-128")).not.toThrow();
  });
});

// Per-device attribution. This is the boundary where a device-supplied header
// stops being a credential and becomes STORED DATA that a fleet dashboard will
// be read as truth, so the rule is "authenticated or nothing".
describe("setDeviceAttribution", () => {
  const req = (h: Record<string, string>) => new Request("https://x/v1/blips", { headers: h });
  const blank = (): RequestMetric => ({ ep: "/v1/blips", status: 200, ms: 1, model: "s3-128", colo: "SEA" });

  it("attributes a device-authed request", () => {
    const m = blank();
    setDeviceAttribution(m, req({ "X-Blip-Device": "2aeea64cb4b760b8", "X-Blip-FW": "5" }), true);
    expect(m.dev).toBe("2aeea64cb4b760b8");
    expect(m.fw).toBe("5");
  });

  it("attributes NOTHING on the shared-key path, where identity is unproven", () => {
    const m = blank();
    setDeviceAttribution(m, req({ "X-Blip-Device": "2aeea64cb4b760b8", "X-Blip-FW": "5" }), false);
    expect(m.dev).toBeUndefined();
    expect(m.fw).toBeUndefined();
  });

  it("drops a malformed id rather than storing it", () => {
    for (const bad of ["", "  ", "ZZZZ", "2aeea6", "../../etc", "2aeea64cb4b760b8x".repeat(4)]) {
      const m = blank();
      setDeviceAttribution(m, req({ "X-Blip-Device": bad }), true);
      expect(m.dev).toBeUndefined();
    }
  });

  it("drops a malformed firmware version rather than storing it", () => {
    for (const bad of ["", "v5", "5.1", "-1", "1234567", "'; DROP"]) {
      const m = blank();
      setDeviceAttribution(m, req({ "X-Blip-Device": "2aeea64cb4b760b8", "X-Blip-FW": bad }), true);
      expect(m.dev).toBe("2aeea64cb4b760b8");
      expect(m.fw).toBeUndefined();
    }
  });

  it("normalises case so one device is one row", () => {
    const m = blank();
    setDeviceAttribution(m, req({ "X-Blip-Device": " 2AEEA64CB4B760B8 " }), true);
    expect(m.dev).toBe("2aeea64cb4b760b8");
  });
});

// The dataset is queried by blob POSITION, and points are retained for three
// months. Inserting a blob rather than appending one silently rewrites the
// meaning of everything already stored, so the order is asserted, not assumed.
describe("record blob layout", () => {
  it("keeps dev and fw in blob5/blob6, after the original four", () => {
    const { env, writeDataPoint } = envWithSpy();
    record(env, {
      ep: "/v1/blips", status: 200, ms: 4, model: "s3-128", colo: "SEA",
      cache: "MISS", upstream: "adsb_lol", upstreamMs: 120,
      dev: "2aeea64cb4b760b8", fw: "5",
    });
    expect(writeDataPoint.mock.calls[0]?.[0].blobs).toEqual([
      "/v1/blips", "MISS", "adsb_lol", "s3-128", "2aeea64cb4b760b8", "5",
    ]);
  });

  it("writes empty strings, not undefined, for an unattributed request", () => {
    const { env, writeDataPoint } = envWithSpy();
    record(env, { ep: "/v1/config", status: 200, ms: 2, model: "s3-146", colo: "ZRH", cache: "MISS" });
    const blobs = writeDataPoint.mock.calls[0]?.[0].blobs;
    expect(blobs[4]).toBe("");
    expect(blobs[5]).toBe("");
  });
});

// index1 is what Analytics Engine samples and groups by. Left as the raw path it
// was effectively unbounded -- one value per airframe, plus one per URL any
// scanner probed -- which both degrades the aggregates and makes "how many
// enrichments did this device do?" unanswerable.
describe("routeTemplate", () => {
  it("collapses the per-resource routes", () => {
    expect(routeTemplate("/v1/enrich/a0e5dd")).toBe("/v1/enrich");
    expect(routeTemplate("/v1/photo/b738-united")).toBe("/v1/photo");
    expect(routeTemplate("/leaderboard/2aeea64cb4b760b8")).toBe("/leaderboard/:id");
  });

  it("leaves the fixed routes exactly as they were, so existing queries still match", () => {
    for (const p of [
      "/v1/blips", "/v1/config", "/v1/airports", "/v1/leaderboard",
      "/healthz", "/credits", "/leaderboard", "/leaderboard.json",
    ]) {
      expect(routeTemplate(p)).toBe(p);
    }
  });

  it("buckets anything unrecognised, so a 404 sweep cannot mint index values", () => {
    for (const p of ["/", "/wp-login.php", "/.env", "/v1/", "/v1/blips/extra", "/" + "x".repeat(4000)]) {
      expect(routeTemplate(p)).toBe("/other");
    }
  });

  it("does not treat a malformed leaderboard id as a profile", () => {
    expect(routeTemplate("/leaderboard/NOTHEX")).toBe("/other");
  });

  it("is what actually lands in blob1 and index1", () => {
    const { env, writeDataPoint } = envWithSpy();
    record(env, { ep: "/v1/enrich/a0e5dd", status: 200, ms: 9, model: "s3-128", colo: "SEA", cache: "MISS" });
    const pt = writeDataPoint.mock.calls[0]?.[0];
    expect(pt.blobs[0]).toBe("/v1/enrich");
    expect(pt.indexes).toEqual(["/v1/enrich"]);
  });
});
