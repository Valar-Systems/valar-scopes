import { describe, expect, it, vi } from "vitest";
import { recordBoot, recordOtaMem } from "../src/metrics";
import type { Env } from "../src/types";

function envWithSpy() {
  const writeDataPoint = vi.fn();
  return { env: { METRICS: { writeDataPoint } } as unknown as Env, writeDataPoint };
}

/* ============================================================================
 * THE BOOT-REASON PATH, AND THE CASE THE OLD ONE SILENTLY DROPPED.
 *
 * A reset reason has been reaching Analytics Engine since 2026-09-03, but only
 * inside X-Blip-OTA-Mem -- and that record is written by NoteOtaAttempt(), which
 * runs inside the "newer firmware available" branch. So a board already on the
 * latest firmware reboots, reports nothing, and the fleet cannot tell it
 * happened.
 *
 * That is already wrong for the reachability watchdog, whose entire purpose is
 * to answer "how often does this fire in real homes". It becomes untenable in
 * v11, where the quiet-hour reboot makes a reboot a DAILY event on EVERY board
 * and almost none of those boots have an update to fetch.
 *
 * THE PASS CONDITION, stated as a test rather than a paragraph: a boot with NO
 * update available produces a reason row. That is the exact case the current
 * design drops, so it is the first assertion in this file.
 *
 * BOTH GENERATIONS ARE PINNED, in both directions. The OTA suffix is NOT removed
 * in this change -- one migration at a time, and the Worker must accept old
 * firmware (no header at all) and new firmware (header present) simultaneously
 * for as long as the fleet is mixed. A parser that quietly required the new
 * header would blind us to precisely the devices that have not updated, which is
 * the population this exists to watch.
 * ========================================================================= */

describe("recordBoot -- a reason that does not need an update to ride on", () => {
  it("THE PASS CONDITION: a boot with no update available still records a reason", () => {
    const { env, writeDataPoint } = envWithSpy();

    // No OTA report at all -- this is a board already on the latest firmware,
    // which is the case that produced nothing before this path existed.
    recordOtaMem(env, null, "s3-128", "0123456789abcdef");
    expect(writeDataPoint).toHaveBeenCalledTimes(0);

    // ...and the boot header alone is enough.
    recordBoot(env, "SW_NETWD", "s3-128", "10", "0123456789abcdef");
    expect(writeDataPoint).toHaveBeenCalledTimes(1);
    expect(writeDataPoint).toHaveBeenCalledWith({
      blobs: ["boot", "SW_NETWD", "s3-128", "10", "0123456789abcdef"],
      doubles: [],
      indexes: ["boot"],
    });
  });

  it("OLD GENERATION: no header is not an error, and records nothing", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordBoot(env, null, "s3-128", "9", "0123456789abcdef");
    expect(writeDataPoint).toHaveBeenCalledTimes(0);
  });

  it("BOTH GENERATIONS AT ONCE: an updating boot writes both rows, not one", () => {
    // During the transition a board that updates sends both headers. They must
    // not collide, share an index, or suppress each other -- the overlap is a
    // deliberate cross-check while both paths are live.
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, "9,10,151540,52212,ok,SW_NETWD", "s3-128", "0123456789abcdef");
    recordBoot(env, "SW_NETWD", "s3-128", "10", "0123456789abcdef");
    expect(writeDataPoint).toHaveBeenCalledTimes(2);
    expect(writeDataPoint.mock.calls[0]?.[0].indexes).toEqual(["ota"]);
    expect(writeDataPoint.mock.calls[1]?.[0].indexes).toEqual(["boot"]);
  });

  it("carries every reason the firmware can emit, including the ambiguous pair", () => {
    // UNKNOWN and UNKNOWN_0 are DIFFERENT FACTS and must both survive intact:
    // bare UNKNOWN means the writing firmware predated the field, UNKNOWN_0
    // means the chip genuinely reported ESP_RST_UNKNOWN. A sanitiser that ate
    // the underscore or the digit would merge two different bugs.
    for (const reason of ["POWERON", "SW", "SW_NETWD", "PANIC", "TASK_WDT", "UNKNOWN", "UNKNOWN_0", "BROWNOUT"]) {
      const { env, writeDataPoint } = envWithSpy();
      recordBoot(env, reason, "s3-128", "10", "dev0");
      expect(writeDataPoint.mock.calls[0]?.[0].blobs[1]).toBe(reason);
    }
  });

  it("records an empty device when the caller has no attribution", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordBoot(env, "SW", "s3-128", "10");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[4]).toBe("");
  });

  it("drops device-supplied junk rather than letting it shape a data point", () => {
    const cases: [string, string][] = [
      ["  SW_NETWD  ", "SW_NETWD"],      // trimmed
      ["SW;DROP TABLE", "SWDROPTABLE"],  // punctuation and spaces stripped
      ["SW.NETWD-2", "SW.NETWD-2"],      // . and - are legitimate reason chars
    ];
    for (const [raw, want] of cases) {
      const { env, writeDataPoint } = envWithSpy();
      recordBoot(env, raw, "s3-128", "10", "dev0");
      expect(writeDataPoint.mock.calls[0]?.[0].blobs[1]).toBe(want);
    }
  });

  it("refuses what cannot be a reason at all", () => {
    // Over-length is REJECTED, never truncated: a 17-char prefix stored as a
    // reason would read like a real one. See the note in recordBoot().
    for (const raw of ["", "   ", "!!!!", "x".repeat(17), "x".repeat(33)]) {
      const { env, writeDataPoint } = envWithSpy();
      recordBoot(env, raw, "s3-128", "10", "dev0");
      expect(writeDataPoint).toHaveBeenCalledTimes(0);
    }
    // CONTROL: the refusals above are not merely a broken function refusing
    // everything -- a valid reason of the same shape still gets through.
    const { env, writeDataPoint } = envWithSpy();
    recordBoot(env, "x".repeat(16), "s3-128", "10", "dev0");
    expect(writeDataPoint).toHaveBeenCalledTimes(1);
  });

  it("never lets telemetry break serving", () => {
    const env = {
      METRICS: { writeDataPoint: () => { throw new Error("AE is down"); } },
    } as unknown as Env;
    expect(() => recordBoot(env, "SW", "s3-128", "10", "dev0")).not.toThrow();
  });
});

describe("the OTA suffix is NOT removed in this change", () => {
  // One migration at a time. Until the new path is deployed, soaked and seen
  // working, the suffix is the only reason field that exists in the field --
  // removing it in the same commit would create a window with neither.
  it("still accepts the six-field report with its reset reason", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, "9,10,151540,52212,ok,SW", "s3-128", "dev0");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[4]).toBe("SW");
  });

  it("still accepts the five-field report from firmware that predates it", () => {
    const { env, writeDataPoint } = envWithSpy();
    recordOtaMem(env, "8,9,135156,53236,ok", "s3-128", "dev0");
    expect(writeDataPoint.mock.calls[0]?.[0].blobs[4]).toBe("");
  });
});

/* ----------------------------------------------------------------------------
 * THE HEADER NAME IS A CONTRACT, AND EVERY TEST ABOVE WOULD PASS WITHOUT IT.
 *
 * The cases above call recordBoot() directly, so a typo in the header name in
 * index.ts -- X-Blip-Boot vs X-Blip-Reset vs a missing call entirely -- passes
 * all ten of them while the feature is completely dead in production. That is
 * the enrolment-404 shape: sixteen green tests around a URL the tests chose.
 *
 * So this one goes through the Worker and names the header as the wire does.
 *
 * IT IS STILL THE WEAK FORM, and says so. The firmware half does not exist yet
 * (Worker first, deployed and soaked before anything sends the field), so this
 * TRANSCRIBES the header name rather than deriving it from the other side. When
 * the firmware lands, smoke-prod.sh should grep the header out of
 * OtaUpdater.cpp the way it already greps the enrol URLs out of
 * ConfigurationWebServer.cpp -- that is the strong form, and this is a
 * placeholder for it.
 * ------------------------------------------------------------------------- */
describe("the X-Blip-Boot header is actually read by the Worker", () => {
  it("a request carrying it produces a boot point", async () => {
    const { call, apiRequest } = await import("./helpers");
    const writeDataPoint = vi.fn();
    await call(apiRequest("/v1/config", { "X-Blip-Boot": "SW_NETWD" }), {
      METRICS: { writeDataPoint } as unknown as Env["METRICS"],
    });
    const boot = writeDataPoint.mock.calls.map((c) => c[0]).filter((p) => p.indexes?.[0] === "boot");
    expect(boot).toHaveLength(1);
    expect(boot[0].blobs[1]).toBe("SW_NETWD");
  });

  it("CONTROL: the same request without the header produces none", async () => {
    const { call, apiRequest } = await import("./helpers");
    const writeDataPoint = vi.fn();
    await call(apiRequest("/v1/config"), {
      METRICS: { writeDataPoint } as unknown as Env["METRICS"],
    });
    const boot = writeDataPoint.mock.calls.map((c) => c[0]).filter((p) => p.indexes?.[0] === "boot");
    expect(boot).toHaveLength(0);
  });
});
