// DL (docs/workers-logs-invocation-off.md, section 8): our own request line carries `devl`, the device LABEL (the first 8
// hex of SHA-256 of the normalised device id), never `dev`. The Analytics Engine point still carries `dev`.
// EXAMPLE ids only: 0000000000000000 and 0123456789abcdef are example data, never a device; TEST_DEVICE is the test
// suite's own fake id (test/helpers.ts). The expected labels were computed outside this code, with Python's hashlib.
import { describe, expect, it } from "vitest";
import { deviceLabel, normDeviceId } from "../src/devlabel";
import { record, type RequestMetric } from "../src/metrics";
import type { Env } from "../src/types";
import { TEST_DEVICE, apiRequest, call } from "./helpers";

const EX0 = "0000000000000000"; // EXAMPLE id
const EX1 = "0123456789abcdef"; // EXAMPLE id

/** Every console.log line written while f runs. */
async function linesOf<T>(f: () => Promise<T> | T): Promise<{ result: T; lines: string[] }> {
  const lines: string[] = [];
  const orig = console.log;
  console.log = (...a: unknown[]) => { lines.push(a.map(String).join(" ")); };
  try {
    return { result: await f(), lines };
  } finally {
    console.log = orig;
  }
}
const reqLines = (lines: string[]) =>
  lines.filter((l) => l.startsWith('{"evt":"req"')).map((l) => JSON.parse(l) as Record<string, unknown>);

describe("DL, the device label", () => {
  it("DL1: the label of each example id is the first 8 hex of its SHA-256", async () => {
    expect([await deviceLabel(EX0), await deviceLabel(EX1), await deviceLabel(TEST_DEVICE)])
      .toEqual(["fcdb4b42", "9f9f5111", "b82efc46"]);
  });

  it("DL2: a label is 8 lowercase hex, and two ids give two labels", async () => {
    const a = await deviceLabel(EX0), b = await deviceLabel(EX1);
    expect([/^[0-9a-f]{8}$/.test(a), /^[0-9a-f]{8}$/.test(b), a !== b]).toEqual([true, true, true]);
  });

  it("DL3: one normalisation rule: trimmed and lowercased 8-32 hex, anything else is null", () => {
    expect([
      normDeviceId(`  ${EX1.toUpperCase()} \r\n`),
      normDeviceId("zz"),
      normDeviceId("0".repeat(7)),
      normDeviceId("0".repeat(33)),
      normDeviceId(`${EX0}\n${EX1}`),
      normDeviceId("0".repeat(8)),
    ]).toEqual([EX1, null, null, null, null, "0".repeat(8)]);
  });

  it("DL4: record() writes devl and no dev on the line, and dev stays in blob5 of the Analytics Engine point", async () => {
    const points: Array<{ blobs: string[] }> = [];
    const env = { METRICS: { writeDataPoint: (p: { blobs: string[] }) => points.push(p) } } as unknown as Env;
    const m: RequestMetric & { devl?: string } = {
      ep: "/v1/config", status: 200, ms: 3, model: "s3-128", colo: "", dev: EX1, devl: "9f9f5111", fw: "19",
    };
    const { lines } = await linesOf(() => record(env, m));
    const line = reqLines(lines)[0]!;
    expect([Object.keys(line).sort(), line.devl, points[0]?.blobs[4]])
      .toEqual([["colo", "devl", "ep", "evt", "fw", "model", "ms", "status"], "9f9f5111", EX1]);
  });

  it("DL5: an authenticated request's line is EXACTLY these keys, with the label of its device and no dev", async () => {
    const { result: res, lines } = await linesOf(() => call(apiRequest("/v1/config")));
    const all = reqLines(lines);
    expect(all.length, "request lines for one request").toBe(1);
    expect([res.status, Object.keys(all[0]!).sort(), all[0]!.devl])
      .toEqual([200, ["authPath", "colo", "devl", "ep", "evt", "model", "ms", "status"], "b82efc46"]);
  });

  it("DL6 (control): an anonymous /healthz line is EXACTLY these keys, with no dev and no devl", async () => {
    const { result: res, lines } = await linesOf(() => call(new Request("https://proxy.test/healthz")));
    const all = reqLines(lines);
    expect(all.length, "request lines for one request").toBe(1);
    expect([res.status, Object.keys(all[0]!).sort()]).toEqual([200, ["colo", "ep", "evt", "model", "ms", "status"]]);
  });
});
