import { describe, expect, it, vi } from "vitest";
import { recordOtaMem } from "../src/metrics";
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
    recordOtaMem(env, "4,5,46068,71668,ok", "s3-128");
    expect(writeDataPoint).toHaveBeenCalledTimes(1);
    expect(writeDataPoint).toHaveBeenCalledWith({
      blobs: ["ota", "ok", "s3-128"],
      doubles: [4, 5, 46068, 71668],
      indexes: ["ota"],
    });
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
