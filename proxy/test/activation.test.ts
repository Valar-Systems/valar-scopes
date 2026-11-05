import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { env } from "cloudflare:test";
import { deriveDeviceKey } from "../src/deviceauth";
import { REVOKED_KEY, resetRevocationCache } from "../src/revocation";
import { actKey, MAX_BATCH } from "../src/activation";
import { deviceIdFromMac } from "../src/provision";
import { SUPPORT_EMAIL } from "../src/support";
import fixture from "./fixtures/mint-equivalence.json";
import { call } from "./helpers";

/* ===========================================================================
 * THE ACTIVATION ALLOWLIST (docs/device-activation.md, A1 and A3-A11; A2 is in
 * enrollpage.test.ts, A12 in activation-lib.test.ts). Each check is paired in that
 * doc with a sabotage that must turn it red.
 * ======================================================================== */

const SECRET = "test-device-secret";
const SITE_SECRET = "test-turnstile-secret";
const TOKEN = "test-provision-token-not-a-real-one";
const LISTED = "1111222233334444";   // fake ids from scripts/device-id-allowlist.txt
const OTHER = "aaaabbbbccccdddd";
const HEX16 = /[0-9a-f]{16}/;

let solveOk = true;
let siteverifyCalls = 0;

beforeEach(() => {
  solveOk = true;
  siteverifyCalls = 0;
  vi.stubGlobal("fetch", async (input: RequestInfo | URL) => {
    const url = String(input instanceof Request ? input.url : input);
    if (url.includes("challenges.cloudflare.com")) {
      siteverifyCalls++;
      return new Response(JSON.stringify({ success: solveOk, hostname: "scopes.valarsystems.com" }),
        { headers: { "content-type": "application/json" } });
    }
    return new Response("unexpected upstream", { status: 502 });
  });
});

afterEach(() => vi.unstubAllGlobals());

const baseEnv = (o: Record<string, unknown> = {}) =>
  ({ DEVICE_KEY_SECRET: SECRET, TURNSTILE_SECRET_KEY: SITE_SECRET, PROVISION_TOKEN: TOKEN, ...o });

async function activate(id: string, o: Record<string, unknown> = {}) {
  const res = await call(new Request("https://proxy.test/blipscope/enroll", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ id, token: "a-token" }),
  }), baseEnv(o));
  return { status: res.status, body: (await res.json()) as Record<string, unknown> };
}

async function admin(body: unknown, token: string | null = TOKEN, o: Record<string, unknown> = {}) {
  const headers: Record<string, string> = { "content-type": "application/json" };
  if (token !== null) headers["X-Provision-Token"] = token;
  const res = await call(new Request("https://proxy.test/blipscope/activation", {
    method: "POST", headers, body: JSON.stringify(body),
  }), baseEnv(o));
  const text = await res.text();
  return { status: res.status, text, body: JSON.parse(text) as Record<string, unknown> };
}

const row = async (id: string) => env.ENRICH_KV.get<{ state: string; resets: number }>(actKey(id), "json");

describe("A1: an unlisted id is refused, and the refusal carries the id and the support address", () => {
  it("403 not_listed, no key, and nothing written", async () => {
    const day = new Date().toISOString().slice(0, 10);
    const r = await activate(LISTED);
    expect(r.status).toBe(403);
    expect(r.body.error).toBe("not_listed");
    expect(r.body.id).toBe(LISTED);
    expect(r.body.support).toBe(SUPPORT_EMAIL);
    expect(r.body.key).toBeUndefined();
    expect(await row(LISTED)).toBeNull();
    expect(await env.ENRICH_KV.get(`enr:dev:${LISTED}`)).toBeNull();
    expect(await env.ENRICH_KV.get(`enr:day:${day}`)).toBeNull();
    expect(siteverifyCalls).toBe(1); // it was refused AFTER the solve, not before
  });
});

describe("A3/A4: a listed id activates once; a reset re-opens it", () => {
  it("A3: the first activation gets the derived key; the second is 409 with no key", async () => {
    expect((await admin({ op: "add", ids: [LISTED] })).body.listed).toBe(1);
    const first = await activate(LISTED);
    expect(first.status).toBe(200);
    expect(first.body.status).toBe("enrolled");
    expect(first.body.key).toBe(await deriveDeviceKey(SECRET, LISTED));
    expect((await row(LISTED))?.state).toBe("activated");

    const second = await activate(LISTED);
    expect(second.status).toBe(409);
    expect(second.body.error).toBe("already_activated");
    expect(second.body.id).toBe(LISTED);
    expect(second.body.support).toBe(SUPPORT_EMAIL);
    expect(second.body.key).toBeUndefined();
  });

  it("A4: reset re-opens exactly one more activation", async () => {
    await admin({ op: "add", ids: [LISTED] });
    expect((await activate(LISTED)).status).toBe(200);
    const reset = await admin({ op: "reset", ids: [LISTED] });
    expect(reset.body).toEqual({ op: "reset", reopened: 1, already_open: 0, not_listed: 0 });
    expect((await row(LISTED))?.resets).toBe(1);
    expect((await activate(LISTED)).status).toBe(200);
    expect((await activate(LISTED)).status).toBe(409);
  });

  it("A4: after a SECRET ROTATION, reset + activation returns the key from the NEW secret", async () => {
    // The old idempotent repeat used to carry this property; first-wins retired it, so the
    // recovery path is now a reset, and this pins that it re-derives rather than replays.
    await admin({ op: "add", ids: [LISTED] });
    const before = await activate(LISTED);
    await admin({ op: "reset", ids: [LISTED] });
    const after = await activate(LISTED, { DEVICE_KEY_SECRET: "a-rotated-device-secret" });
    expect(after.status).toBe(200);
    expect(after.body.key).not.toBe(before.body.key);
    expect(after.body.key).toBe(await deriveDeviceKey("a-rotated-device-secret", LISTED));
  });
});

describe("A5: list membership is never answered without a solve", () => {
  it("an unlisted id with a failed solve reads unverified, not not_listed", async () => {
    solveOk = false;
    const r = await activate(OTHER);
    expect(r.status).toBe(403);
    expect(r.body.error).toBe("unverified");
  });

  it("a listed id with a failed solve reads unverified and keeps its activation", async () => {
    await admin({ op: "add", ids: [LISTED] });
    solveOk = false;
    const r = await activate(LISTED);
    expect(r.body.error).toBe("unverified");
    expect((await row(LISTED))?.state).toBe("listed");
  });
});

describe("A6: the existing caps hold", () => {
  it("a revoked listed id is refused as revoked and keeps its activation unused", async () => {
    await admin({ op: "add", ids: ["dead0000beef1111"] });
    await env.ENRICH_KV.put(REVOKED_KEY, "dead0000beef1111");
    resetRevocationCache();
    const r = await activate("dead0000beef1111");
    expect(r.status).toBe(403);
    expect(r.body.error).toBe("revoked");
    expect((await row("dead0000beef1111"))?.state).toBe("listed");
    await env.ENRICH_KV.delete(REVOKED_KEY);
    resetRevocationCache();
  });

  it("in production a listed fake id is refused before any siteverify call", async () => {
    await admin({ op: "add", ids: ["beefbeefbeefbeef"] });
    const r = await activate("beefbeefbeefbeef", { REFUSE_FAKE_DEVICE_IDS: "true" });
    expect(r.status).toBe(403);
    expect(r.body.error).toBe("fake_device_id");
    expect(siteverifyCalls).toBe(0);
  });
});

describe("A7: the admin route takes the bench token and nothing else", () => {
  for (const [name, token, o] of [
    ["missing", null, {}],
    ["wrong", "not-the-token", {}],
    ["unset on the Worker", TOKEN, { PROVISION_TOKEN: undefined }],
  ] as const) {
    it(`a ${name} token is 403 and writes nothing`, async () => {
      const r = await admin({ op: "add", ids: [LISTED] }, token, o);
      expect(r.status).toBe(403);
      expect(r.body.error).toBe("forbidden");
      expect(await row(LISTED)).toBeNull();
    });
  }

  it("CONTROL: the right token writes", async () => {
    expect((await admin({ op: "add", ids: [LISTED] })).status).toBe(200);
    expect((await row(LISTED))?.state).toBe("listed");
  });

  it("a GET is 405, never a page", async () => {
    const res = await call(new Request("https://proxy.test/blipscope/activation"), baseEnv());
    expect(res.status).toBe(405);
  });
});

describe("A8: an admin answer carries no id", () => {
  it("add, reset and status answer in counts or a state", async () => {
    const answers = [
      await admin({ op: "add", ids: [LISTED, OTHER] }),
      await admin({ op: "reset", ids: [LISTED, OTHER] }),
      await admin({ op: "status", ids: [LISTED] }),
      await admin({ op: "add", ids: [LISTED, "not-an-id"] }),
    ];
    for (const a of answers) {
      expect(a.text).not.toContain(LISTED);
      expect(a.text).not.toContain(OTHER);
      expect(a.text).not.toMatch(HEX16);
    }
    // CONTROL: they still say something.
    expect(answers[0]!.body).toEqual({ op: "add", listed: 2, already_listed: 0, already_activated: 0 });
    expect(answers[2]!.body).toEqual({ op: "status", state: "listed" });
    expect(answers[3]!.body).toEqual({ error: "bad_entry", count: 1 });
  });
});

describe("A9: add by MAC lists the id the firmware derives", () => {
  const sha256 = async (s: string) =>
    [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s)))]
      .map((b) => b.toString(16).padStart(2, "0")).join("");

  it("each fixture MAC lists the id whose sha256 the fixture records", async () => {
    const r = await admin({ op: "add", macs: fixture.cases.map((c) => c.mac) });
    expect(r.body.listed).toBe(fixture.cases.length);
    for (const c of fixture.cases) {
      const id = await deviceIdFromMac(c.mac.toLowerCase());
      expect(await sha256(id), c.mac).toBe(c.deviceIdSha256);
      expect((await admin({ op: "status", ids: [id] })).body.state).toBe("listed");
    }
  });
});

describe("A10: add never re-opens, reset never lists", () => {
  it("add on an activated id leaves it activated", async () => {
    await admin({ op: "add", ids: [LISTED] });
    await activate(LISTED);
    const r = await admin({ op: "add", ids: [LISTED] });
    expect(r.body).toEqual({ op: "add", listed: 0, already_listed: 0, already_activated: 1 });
    expect((await row(LISTED))?.state).toBe("activated");
  });

  it("reset on an unlisted id writes no row", async () => {
    const r = await admin({ op: "reset", ids: [OTHER] });
    expect(r.body).toEqual({ op: "reset", reopened: 0, already_open: 0, not_listed: 1 });
    expect(await row(OTHER)).toBeNull();
  });
});

describe("A11: a call is all or nothing, and bounded", () => {
  it(`${MAX_BATCH + 1} entries are refused`, async () => {
    const ids = Array.from({ length: MAX_BATCH + 1 }, (_, i) => i.toString(16).padStart(16, "0"));
    const r = await admin({ op: "add", ids });
    expect(r.status).toBe(400);
    expect(r.body).toEqual({ error: "too_many", max: MAX_BATCH });
  });

  it("one malformed entry refuses the call, and none of the good ones is written", async () => {
    const r = await admin({ op: "add", ids: [LISTED, OTHER, "zz"] });
    expect(r.status).toBe(400);
    expect(r.text).not.toContain("zz");
    expect(await row(LISTED)).toBeNull();
    expect(await row(OTHER)).toBeNull();
  });

  it("status takes exactly one entry", async () => {
    expect((await admin({ op: "status", ids: [LISTED, OTHER] })).body.error).toBe("status_takes_one");
  });
});
