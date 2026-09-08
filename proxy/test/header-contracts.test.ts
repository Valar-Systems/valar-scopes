import { describe, expect, it, vi } from "vitest";
import { apiRequest, call } from "./helpers";
import type { Env } from "../src/types";

/* ============================================================================
 * EVERY TELEMETRY HEADER, ASSERTED AT THE WIRE.
 *
 * WHY THIS FILE EXISTS. The unit tests for these three call recordUsage(),
 * recordOtaMem() and recordBoot() DIRECTLY. All of them pass if the header name
 * in index.ts is misspelled, or if the call is deleted outright — the feature is
 * dead in production and the suite is green. That is the enrolment-404 shape:
 * sixteen passing tests around a URL the tests themselves had chosen.
 *
 * Rehearsed rather than assumed, on 2026-09-08: renaming X-Blip-Boot to
 * X-Blip-Reset in index.ts left eleven of twelve tests passing and failed only
 * the one that went through the Worker.
 *
 * X-Blip-Boot got this check when it was written. X-Blip-Usage and
 * X-Blip-OTA-Mem have been shipping WITHOUT it — their names have never been
 * asserted anywhere outside the source that emits them.
 *
 * ONE FILE, NOT THREE. Each of these could live beside its own unit tests, and
 * then there would be three places that know the header names and two of them
 * would be stale. Same rule as the single allowlist shared by the pre-commit
 * hook and the CI check.
 *
 * THIS IS STILL THE WEAK FORM AND SAYS SO. It TRANSCRIBES the names rather than
 * deriving them from the firmware that sends them. The strong form is
 * smoke-prod.sh grepping them out of the firmware source the way it already
 * greps the enrol URLs out of ConfigurationWebServer.cpp — and that is only
 * possible once firmware exists that sends X-Blip-Boot at all. Tracked in
 * docs/v11-quiet-hour-plan.md as part of the first-contact gate.
 * ========================================================================= */

/** Run one authenticated request with `headers` and return the AE points it wrote. */
async function pointsFor(headers: Record<string, string>): Promise<Record<string, any>[]> {
  const writeDataPoint = vi.fn();
  await call(apiRequest("/v1/config", headers), {
    METRICS: { writeDataPoint } as unknown as Env["METRICS"],
  });
  return writeDataPoint.mock.calls.map((c) => c[0]);
}

const indexOf = (points: Record<string, any>[], idx: string) =>
  points.filter((p) => p.indexes?.[0] === idx);

describe("the telemetry header names are what the Worker actually reads", () => {
  // Each case is (header, a payload the parser accepts, the AE index it lands
  // in, the blob that should carry a recognisable value). A control with the
  // header ABSENT accompanies every one, because "a point appeared" means
  // nothing unless "no point appears without it" also holds — otherwise a
  // parser that fired on every request would pass.
  const cases: { header: string; value: string; index: string; blob: number; expect: string }[] = [
    { header: "X-Blip-Boot", value: "SW_NETWD", index: "boot", blob: 1, expect: "SW_NETWD" },
    { header: "X-Blip-Usage", value: "1,2,3,4,5,6,1,72", index: "usage", blob: 0, expect: "usage" },
    { header: "X-Blip-OTA-Mem", value: "9,10,151540,52212,ok,SW", index: "ota", blob: 1, expect: "ok" },
  ];

  for (const c of cases) {
    it(`${c.header} is read, and lands in the "${c.index}" index`, async () => {
      const pts = indexOf(await pointsFor({ [c.header]: c.value }), c.index);
      expect(pts).toHaveLength(1);
      expect(pts[0]?.blobs?.[c.blob]).toBe(c.expect);
    });

    it(`CONTROL: without ${c.header} nothing lands in "${c.index}"`, async () => {
      expect(indexOf(await pointsFor({}), c.index)).toHaveLength(0);
    });
  }

  it("all three on one request write three separate points", async () => {
    // The transition state: a board that updated, reports usage, and reports its
    // boot reason on the same check-in. None may suppress another, and none may
    // share an index — the OTA reset suffix and the boot reason overlap here on
    // purpose while both paths are live.
    const pts = await pointsFor({
      "X-Blip-Boot": "SW_NETWD",
      "X-Blip-Usage": "1,2,3,4,5,6,1,72",
      "X-Blip-OTA-Mem": "9,10,151540,52212,ok,SW_NETWD",
    });
    expect(indexOf(pts, "boot")).toHaveLength(1);
    expect(indexOf(pts, "usage")).toHaveLength(1);
    expect(indexOf(pts, "ota")).toHaveLength(1);
  });
});
