import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { env } from "cloudflare:test";
import fixture from "./fixtures/mint-equivalence.json";
import { deviceIdFromMac, handleProvision, normalizeMac, PROVISION_DAILY_CAP } from "../src/provision";
import { REVOKED_KEY, resetRevocationCache } from "../src/revocation";
import { call, testEnv, TEST_DEVICE_SECRET } from "./helpers";

/* ===========================================================================
 * WORKER-MINTED PROVISIONING KEYS (docs/provisioning-mint.md)
 *
 *   1. SAME KEY AS THE BENCH USED TO DERIVE -- asserted against a fixture that
 *      provision-device.py's own functions generate (scripts/mint-fixture.py).
 *   2. NO TOKEN, NO KEY -- wrong, missing, or unset PROVISION_TOKEN is a 403,
 *      and the token never reaches a response or a log line.
 *   3. BOUNDED -- 120 mints a day, then a clear refusal.
 * ======================================================================== */

const TOKEN = "test-provision-token-not-a-real-one";
const today = () => new Date().toISOString().slice(0, 10);

function mintReq(body: unknown, token: string | null = TOKEN, method = "POST"): Request {
  const headers: Record<string, string> = { "Content-Type": "application/json" };
  if (token !== null) headers["X-Provision-Token"] = token;
  return new Request("https://proxy.test/blipscope/provision", {
    method, headers, body: method === "POST" ? JSON.stringify(body) : undefined,
  });
}
const mint = (body: unknown, token: string | null = TOKEN, overrides = {}) =>
  call(mintReq(body, token), { PROVISION_TOKEN: TOKEN, ...overrides });
const sha256 = async (s: string) =>
  [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s)))]
    .map((b) => b.toString(16).padStart(2, "0")).join("");

let logs: string[] = [];
beforeEach(async () => {
  logs = [];
  vi.spyOn(console, "log").mockImplementation((...a: unknown[]) => { logs.push(a.map(String).join(" ")); });
  await env.ENRICH_KV.delete(`prov:day:${today()}`);
});
afterEach(async () => {
  vi.restoreAllMocks();
  await env.ENRICH_KV.delete(REVOKED_KEY);
  resetRevocationCache();
});

describe("equivalence: the Worker mints what provision-device.py derived", () => {
  it("CONTROL: the fixture was generated with the suite's device secret", () => {
    expect(fixture.secret).toBe(TEST_DEVICE_SECRET);
    expect(fixture.cases.length).toBeGreaterThanOrEqual(4);
  });
  it("every fixture MAC mints the same device id and the same key", async () => {
    for (const c of fixture.cases) {
      const res = await mint({ mac: c.mac });
      expect(res.status, c.mac).toBe(200);
      const j = (await res.json()) as { deviceId: string; key: string };
      expect(await sha256(j.deviceId), c.mac).toBe(c.deviceIdSha256);
      expect(j.key, c.mac).toBe(c.key);
    }
  });
  it("the minted key authenticates as that device", async () => {
    const res = await mint({ mac: fixture.cases[1]!.mac });
    const { deviceId, key } = (await res.json()) as { deviceId: string; key: string };
    const authed = await call(new Request("https://proxy.test/api/v1/blipscope/config",
      { headers: { "X-Blip-Key": key, "X-Blip-Device": deviceId } }));
    expect(authed.status).toBe(200);
    // CONTROL: the same id with a wrong key is refused, so the 200 above is the key's doing.
    const bad = await call(new Request("https://proxy.test/api/v1/blipscope/config",
      { headers: { "X-Blip-Key": "0".repeat(64), "X-Blip-Device": deviceId } }));
    expect(bad.status).toBe(401);
  });
  it("MAC spellings normalise to one address; bad MACs are refused", () => {
    expect(normalizeMac("02:12:34:56:78:9A")).toBe("02:12:34:56:78:9a");
    expect(normalizeMac("021234-56789a")).toBe("02:12:34:56:78:9a");
    for (const bad of ["", "02:12:34:56:78", "01:00:5e:00:00:01", "00:00:00:00:00:00", "ff:ff:ff:ff:ff:ff"]) {
      expect(normalizeMac(bad), bad).toBe(null);
    }
  });
});

describe("auth: no token, no key", () => {
  const MAC = fixture.cases[0]!.mac;
  it("a wrong token is 403 and mints nothing", async () => {
    const res = await mint({ mac: MAC }, "not-the-token");
    expect(res.status).toBe(403);
    expect(await res.json()).toEqual({ error: "forbidden" });
    expect(await env.ENRICH_KV.get(`prov:day:${today()}`)).toBe(null);
  });
  it("a missing token is 403", async () => {
    expect((await mint({ mac: MAC }, null)).status).toBe(403);
  });
  it("an unset PROVISION_TOKEN refuses everything, even an empty token (fails closed)", async () => {
    for (const t of ["", TOKEN, "anything"]) {
      expect((await call(mintReq({ mac: MAC }, t), { PROVISION_TOKEN: undefined })).status, t).toBe(403);
    }
  });
  it("a token of a different length is 403 like any other wrong token", async () => {
    expect((await mint({ mac: MAC }, TOKEN + "x")).status).toBe(403);
    expect((await mint({ mac: MAC }, TOKEN.slice(0, 5))).status).toBe(403);
  });
  it("CONTROL: the right token mints", async () => {
    expect((await mint({ mac: MAC })).status).toBe(200);
  });
  it("the token never appears in a response or a log line; the key never in a log line", async () => {
    const ok = await mint({ mac: MAC });
    const { key } = (await ok.json()) as { key: string };
    await mint({ mac: MAC }, "wrong-" + TOKEN);
    const all = logs.join("\n");
    expect(all).not.toContain(TOKEN);
    expect(all).not.toContain(key);
    expect(logs.length).toBeGreaterThanOrEqual(2); // CONTROL: something WAS logged
  });
  it("GET is a 405, never a page", async () => {
    expect((await call(mintReq(null, TOKEN, "GET"), { PROVISION_TOKEN: TOKEN })).status).toBe(405);
  });
});

describe("refusals and the record", () => {
  const MAC = fixture.cases[2]!.mac;
  it("a bad MAC is 400", async () => {
    expect((await mint({ mac: "01:00:5e:00:00:01" })).status).toBe(400);
    expect((await mint({})).status).toBe(400);
  });
  it("a revoked id gets no key", async () => {
    await env.ENRICH_KV.put(REVOKED_KEY, await deviceIdFromMac(MAC));
    resetRevocationCache();
    const res = await mint({ mac: MAC });
    expect(res.status).toBe(403);
    expect(await res.json()).toEqual({ error: "revoked" });
  });
  it("a fake-id-allowlist id gets no key (the same guard as every route)", async () => {
    const res = await handleProvision(mintReq({ mac: MAC }), testEnv({ PROVISION_TOKEN: TOKEN }), () => true);
    expect(res.status).toBe(403);
    expect(await res.json()).toEqual({ error: "fake_device_id" });
  });
  it("every mint is logged: structured, operator=bench, keyed 32-hex MAC hash, the count", async () => {
    await mint({ mac: MAC });
    const line = logs.map((l) => { try { return JSON.parse(l); } catch { return null; } })
      .find((o) => o?.evt === "provision_mint");
    expect(line).toMatchObject({ evt: "provision_mint", operator: "bench", mints_today: 1 });
    expect(line.mac_hash).toMatch(/^[0-9a-f]{32}$/);
    expect(line.mac_hash).not.toContain(MAC.replace(/:/g, ""));
  });
});

describe(`daily cap: ${PROVISION_DAILY_CAP}`, () => {
  const MAC = fixture.cases[3]!.mac;
  it("the last mint under the cap succeeds and lands the counter on the cap", async () => {
    await env.ENRICH_KV.put(`prov:day:${today()}`, String(PROVISION_DAILY_CAP - 1));
    const res = await mint({ mac: MAC });
    expect(res.status).toBe(200);
    expect(((await res.json()) as { mintsToday: number }).mintsToday).toBe(PROVISION_DAILY_CAP);
  });
  it("at the cap: a clear refusal, no key, and when it resets", async () => {
    await env.ENRICH_KV.put(`prov:day:${today()}`, String(PROVISION_DAILY_CAP));
    const res = await mint({ mac: MAC });
    expect(res.status).toBe(429);
    const j = (await res.json()) as { error: string; cap: number; resetsAt: string; key?: string };
    expect(j.error).toBe("daily_cap");
    expect(j.cap).toBe(120);
    expect(j.key).toBeUndefined();
    expect(Date.parse(j.resetsAt) - Date.now()).toBeGreaterThan(0);
    expect(j.resetsAt.endsWith("T00:00:00.000Z")).toBe(true);
  });
});
