import { afterEach, describe, expect, it, vi } from "vitest";
import { deriveDeviceKey } from "../src/deviceauth";
import { FAKE_DEVICE_IDS } from "../src/fakeids.generated";
import { apiRequest, call } from "./helpers";

// Production refuses ids from the repo's fake-id allowlist; staging does not.
// Every request below carries a VALID key derived for its id, so the only thing
// that can refuse it is the guard -- a 401 would mean the fixture broke, not
// that the guard fired.
const SECRET = "fakeid-test-secret";
const FAKE = "beefbeefbeefbeef"; // the id that enrolled 195 times on 2026-08-28..31
// A real-shaped id that is NOT on the list. 12 hex, not 16: the repo's
// no-real-device-ids guard refuses any 16-hex string that is not allowlisted,
// and the Worker accepts 8-32 hex, so this is valid here and invisible there.
const REAL = "0a0b0c0d0e0f";
const PROD = { DEVICE_KEY_SECRET: SECRET, REFUSE_FAKE_DEVICE_IDS: "true" };
const STAGING = { DEVICE_KEY_SECRET: SECRET }; // the var is absent from staging

async function asDevice(id: string, env: Record<string, string>) {
  const key = await deriveDeviceKey(SECRET, id);
  return call(apiRequest("/api/v1/blipscope/config", { "X-Blip-Key": key, "X-Blip-Device": id }), env);
}

describe("fake-id guard on device-key routes", () => {
  it("the embedded list carries the id that enrolled in production", () => {
    expect(FAKE_DEVICE_IDS.has(FAKE)).toBe(true);
    expect(FAKE_DEVICE_IDS.has(REAL)).toBe(false);
  });

  it("production: a fake id with a VALID key is a 403", async () => {
    const res = await asDevice(FAKE, PROD);
    expect(res.status).toBe(403);
    expect(await res.text()).toContain("fake_device_id");
  });

  it("production: every id on the list is refused, whatever its case", async () => {
    for (const id of FAKE_DEVICE_IDS) expect((await asDevice(id, PROD)).status, id).toBe(403);
    expect((await asDevice(FAKE.toUpperCase(), PROD)).status).toBe(403);
  });

  it("CONTROL production: a real-shaped id not on the list is served", async () => {
    expect((await asDevice(REAL, PROD)).status).toBe(200);
  });

  it("staging (var absent): the same fake id is served", async () => {
    expect((await asDevice(FAKE, STAGING)).status).toBe(200);
  });

  it("the var must be exactly \"true\" -- anything else leaves the guard off", async () => {
    expect((await asDevice(FAKE, { ...STAGING, REFUSE_FAKE_DEVICE_IDS: "false" })).status).toBe(200);
  });
});

describe("fake-id guard on enrol", () => {
  let siteverify = 0;
  afterEach(() => vi.unstubAllGlobals());
  const enroll = (id: string, env: Record<string, string>) => {
    siteverify = 0;
    vi.stubGlobal("fetch", vi.fn(async () => {
      siteverify++;
      return new Response(JSON.stringify({ success: true, hostname: "scopes.valarsystems.com" }));
    }));
    return call(
      new Request("https://proxy.test/blipscope/enroll", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id, token: "good-token" }),
      }),
      { ...env, TURNSTILE_SECRET_KEY: "turnstile-secret" },
    );
  };

  it("production: a fake id cannot enrol, and costs no siteverify call", async () => {
    const res = await enroll(FAKE, PROD);
    expect(res.status).toBe(403);
    expect(await res.text()).toContain("fake_device_id");
    expect(siteverify).toBe(0);
  });

  it("CONTROL production: a real-shaped id is NOT refused by the guard", async () => {
    const res = await enroll(REAL, PROD);
    expect(res.status).not.toBe(403);
    expect(siteverify).toBe(1);
  });

  it("staging: a fake id is not refused by the guard", async () => {
    const res = await enroll(FAKE, STAGING);
    expect(res.status).not.toBe(403);
    expect(siteverify).toBe(1);
  });
});
