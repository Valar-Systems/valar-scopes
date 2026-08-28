import { describe, expect, it, vi } from "vitest";
import {
  record,
  recordOtaMem,
  recordUsage,
  routeTemplate,
  setDeviceAttribution,
  USAGE_FIELD_COUNT,
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
    setDeviceAttribution(m, req({ "X-Blip-Device": "2aeea64cb4b760b8", "X-Blip-FW": "5" }));
    expect(m.dev).toBe("2aeea64cb4b760b8");
    expect(m.fw).toBe("5");
  });

  // This used to pass `false` for the shared-key path, where a caller had proven
  // it held A key but never WHICH device. That path is gone (2026-08-13) and so
  // is the flag -- a permanently-true argument reads like a switch while being
  // wired to nothing, which is a shape this repo has been bitten by before.
  //
  // The guarantee did not disappear, it moved: setDeviceAttribution is called
  // only after authenticate() has returned non-null, and authenticate() now
  // requires a device id whose derived key matches. An unauthenticated request
  // 401s before reaching here, so there is no longer a way in with an unproven
  // identity. That is asserted end-to-end in auth.test.ts ("refuses a key with
  // NO device id" / "refuses a valid key presented with the WRONG device id"),
  // which is the right level for it -- a unit test here could only re-state the
  // precondition it was given.
  it("attributes purely from the headers, the caller having already proven identity", () => {
    const m = blank();
    setDeviceAttribution(m, req({ "X-Blip-Device": "2aeea64cb4b760b8", "X-Blip-FW": "5" }));
    expect(m.dev).toBe("2aeea64cb4b760b8");
    // No headers at all -> nothing invented, even past auth.
    const m2 = blank();
    setDeviceAttribution(m2, req({}));
    expect(m2.dev).toBeUndefined();
    expect(m2.fw).toBeUndefined();
  });

  it("drops a malformed id rather than storing it", () => {
    for (const bad of ["", "  ", "ZZZZ", "2aeea6", "../../etc", "2aeea64cb4b760b8x".repeat(4)]) {
      const m = blank();
      setDeviceAttribution(m, req({ "X-Blip-Device": bad }));
      expect(m.dev).toBeUndefined();
    }
  });

  it("drops a malformed firmware version rather than storing it", () => {
    for (const bad of ["", "v5", "5.1", "-1", "1234567", "'; DROP"]) {
      const m = blank();
      setDeviceAttribution(m, req({ "X-Blip-Device": "2aeea64cb4b760b8", "X-Blip-FW": bad }));
      expect(m.dev).toBe("2aeea64cb4b760b8");
      expect(m.fw).toBeUndefined();
    }
  });

  it("normalises case so one device is one row", () => {
    const m = blank();
    setDeviceAttribution(m, req({ "X-Blip-Device": " 2AEEA64CB4B760B8 " }));
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
      // "/" was an unrecognised path -- an unrouted 404 -- until the edition hub
      // shipped there. It is a real page now, and the root is where store traffic
      // lands, so bucketing it to "/other" would hide the arrivals we most want
      // to see.
      "/", "/blipscope/support",
    ]) {
      expect(routeTemplate(p)).toBe(p);
    }
  });

  it("buckets anything unrecognised, so a 404 sweep cannot mint index values", () => {
    // Deliberately no "/" here any more: it is a routed page. The scanner bait
    // below still carries the point of the test.
    for (const p of ["/wp-login.php", "/.env", "/v1/", "/v1/blips/extra", "/" + "x".repeat(4000)]) {
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

// The device sends this at most once an hour, on a check-in it was making
// anyway. Device-supplied input reaching storage, so the shape is enforced here
// rather than trusted -- and the REJECTIONS are what most of this describes,
// because a malformed value that gets stored is a wrong row nobody can spot in
// an aggregate.
describe("recordUsage", () => {
  it("records a well-formed report as one Analytics Engine point", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, "41,7,3,2,11,5,1,137", "s3-128", "8", "2aeea64cb4b760b8");
    expect(writeDataPoint).toHaveBeenCalledTimes(1);
    expect(writeDataPoint).toHaveBeenCalledWith({
      blobs: ["usage", "s3-128", "8", "2aeea64cb4b760b8"],
      doubles: [41, 7, 3, 2, 11, 5, 1, 137],
      indexes: ["usage"],
    });
  });

  it("keeps the device on the point, because per-device was the ask", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, "1,0,0,0,0,0,0,1", "s3-128", "8", "2aeea64cb4b760b8");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[3]).toBe("2aeea64cb4b760b8");
  });

  it("records an empty device when the caller has no attribution", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, "1,0,0,0,0,0,0,1", "s3-128", "8");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[3]).toBe("");
  });

  it("does nothing when the check-in carries no report", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, null, "s3-128", "8");
    recordUsage(env, "", "s3-128", "8");
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  // THE FIELD COUNT IS A CONTRACT WITH THE FIRMWARE. usage::FIELD_COUNT in
  // include/UsageReport.h is the other side of it, and a parser that quietly
  // accepted a different arity is exactly how the two drift apart.
  it("pins the arity at 8 and rejects anything else", () => {
    const { env, writeDataPoint } = envWithSpy();
    expect(USAGE_FIELD_COUNT).toBe(8);
    recordUsage(env, "1,2,3,4,5,6,1", "s3-128", "8"); // seven
    recordUsage(env, "1,2,3,4,5,6,1,2,3", "s3-128", "8"); // nine
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  // The device emits digits and commas by construction (its builder takes only
  // integers). Anything else did not come from an image we ship, so it is
  // dropped rather than parsed leniently -- and this is the same assertion the
  // firmware's host test makes, from the other side of the wire.
  it("rejects any value that is not digits and commas", () => {
    const { env, writeDataPoint } = envWithSpy();
    for (const bad of [
      "41,7,3,2,11,5,1,N4523K",
      "41,7,3,2,11,5,1,137,BAW117",
      "-1,7,3,2,11,5,1,137",
      "41.5,7,3,2,11,5,1,137",
      "41, 7,3,2,11,5,1,137",
      "41,7,3,2,11,5,1,",
      "hello",
    ]) {
      recordUsage(env, bad, "s3-128", "8");
    }
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  it("rejects a length nothing legitimate approaches", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, "1".repeat(200), "s3-128", "8");
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  it("rejects a value past a u32", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, `1,2,3,4,5,6,1,${0x100000000}`, "s3-128", "8");
    expect(writeDataPoint).not.toHaveBeenCalled();
  });

  // followEnabled is a FLAG. A "2" summed into an adoption ratio is worse than a
  // dropped row, because the ratio stays plausible.
  it("rejects a follow flag that is not 0 or 1", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordUsage(env, "1,2,3,4,5,6,2,10", "s3-128", "8");
    expect(writeDataPoint).not.toHaveBeenCalled();
    // CONTROL: both legal values still land, or the assertion above would pass
    // against a function that rejected everything.
    recordUsage(env, "1,2,3,4,5,6,0,10", "s3-128", "8");
    recordUsage(env, "1,2,3,4,5,6,1,10", "s3-128", "8");
    expect(writeDataPoint).toHaveBeenCalledTimes(2);
  });

  it("never throws, whatever the device sends", () => {
    const env = {} as unknown as Env; // no METRICS binding at all
    expect(() => recordUsage(env, "1,2,3,4,5,6,1,10", "s3-128", "8")).not.toThrow();
  });
});
