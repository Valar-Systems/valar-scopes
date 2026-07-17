import { describe, expect, it } from "vitest";
import { deriveDeviceKey, verifyDeviceKey } from "../src/deviceauth";
import { apiRequest, call, TEST_KEY } from "./helpers";

const SECRET = "test-device-secret";
const DEVICE_ID = "a1b2c3d4e5f6a7b8";

describe("deviceauth", () => {
  it("derives a stable 64-hex HMAC key", async () => {
    const k1 = await deriveDeviceKey(SECRET, DEVICE_ID);
    const k2 = await deriveDeviceKey(SECRET, DEVICE_ID);
    expect(k1).toBe(k2);
    expect(k1).toMatch(/^[0-9a-f]{64}$/);
    expect(await deriveDeviceKey(SECRET, "different")).not.toBe(k1);
  });

  it("verifies a correct key and rejects everything else", async () => {
    const good = await deriveDeviceKey(SECRET, DEVICE_ID);
    const env = { DEVICE_KEY_SECRET: SECRET } as any;
    expect(await verifyDeviceKey(env, DEVICE_ID, good)).toBe(true);
    expect(await verifyDeviceKey(env, DEVICE_ID, "0".repeat(64))).toBe(false);
    expect(await verifyDeviceKey(env, "wrongdevice", good)).toBe(false);
    expect(await verifyDeviceKey(env, DEVICE_ID, "short")).toBe(false);
    // no secret configured -> never authenticates
    expect(await verifyDeviceKey({} as any, DEVICE_ID, good)).toBe(false);
  });
});

describe("per-device key auth (additive)", () => {
  it("authenticates a device-key request when the secret is configured", async () => {
    const key = await deriveDeviceKey(SECRET, DEVICE_ID);
    const req = apiRequest("/v1/config", { "X-Blip-Key": key, "X-Blip-Device": DEVICE_ID, "X-Blip-Model": "s3-146" });
    const res = await call(req, { DEVICE_KEY_SECRET: SECRET });
    expect(res.status).toBe(200);
  });

  it("rejects a bad device key even with the secret set", async () => {
    const req = apiRequest("/v1/config", { "X-Blip-Key": "f".repeat(64), "X-Blip-Device": DEVICE_ID });
    const res = await call(req, { DEVICE_KEY_SECRET: SECRET });
    expect(res.status).toBe(401);
  });

  it("still accepts the shared key (live fleet unaffected)", async () => {
    // Default env has BLIP_KEYS=test-key and no DEVICE_KEY_SECRET.
    const res = await call(apiRequest("/v1/config", { "X-Blip-Model": "s3-146" }));
    expect(res.status).toBe(200);
    // And the shared key still works even when the device-key path is enabled.
    const res2 = await call(apiRequest("/v1/config", { "X-Blip-Key": TEST_KEY }), { DEVICE_KEY_SECRET: SECRET });
    expect(res2.status).toBe(200);
  });
});
