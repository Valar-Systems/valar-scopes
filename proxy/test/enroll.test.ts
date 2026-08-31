import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { env } from "cloudflare:test";
import { deriveDeviceKey } from "../src/deviceauth";
import { REVOKED_KEY, resetRevocationCache } from "../src/revocation";
import { routeTemplate } from "../src/metrics";
import { AUTH_HEADERS, call, testEnv, TEST_KEY } from "./helpers";

/* ===========================================================================
 * ENROLLMENT — the three properties that must hold, each asserted rather than
 * argued, and each with a control proving the assertion can fail.
 *
 *   1. NO SOLVE, NO KEY. The endpoint is a mint, never an oracle.
 *   2. IDEMPOTENT. A reflashed board re-enrolls to the SAME key, not a wall.
 *   3. REVOCATION SURVIVES A REFLASH. "Reflash to escape revocation" is the
 *      first thing anyone would try, so it is a test and not a paragraph.
 * ======================================================================== */

const SECRET = "test-device-secret";
const SITE_SECRET = "test-turnstile-secret";
const DEVICE_ID = "a1b2c3d4e5f6a7b8";

/** Turnstile's verdict, faked at the network boundary — never in our own code. */
let solveOk = true;
let siteverifyCalls = 0;

beforeEach(() => {
  solveOk = true;
  siteverifyCalls = 0;
  vi.stubGlobal("fetch", async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = String(input instanceof Request ? input.url : input);
    if (url.includes("challenges.cloudflare.com")) {
      siteverifyCalls++;
      return new Response(
        JSON.stringify({ success: solveOk, hostname: "scopes.valarsystems.com" }),
        { headers: { "content-type": "application/json" } },
      );
    }
    return new Response("unexpected upstream", { status: 502 });
  });
});

afterEach(() => {
  vi.unstubAllGlobals();
});

function enrollEnv(overrides: Record<string, unknown> = {}) {
  return { DEVICE_KEY_SECRET: SECRET, TURNSTILE_SECRET_KEY: SITE_SECRET, ...overrides };
}

function enrollReq(body: unknown): Request {
  return new Request("https://proxy.test/blipscope/enroll", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
}

async function enroll(body: unknown, overrides: Record<string, unknown> = {}) {
  const res = await call(enrollReq(body), enrollEnv(overrides));
  return { status: res.status, body: (await res.json()) as Record<string, unknown> };
}

describe("enrollment mints only against a verified solve", () => {
  it("issues the derived key when the challenge verifies", async () => {
    const r = await enroll({ id: DEVICE_ID, token: "good-token" });
    expect(r.status).toBe(200);
    expect(r.body.status).toBe("enrolled");
    // The KEY IS THE DERIVATION, not a stored value — asserted against the same
    // function the auth path uses, so the two cannot drift.
    expect(r.body.key).toBe(await deriveDeviceKey(SECRET, DEVICE_ID));
    expect(siteverifyCalls).toBe(1);
  });

  it("REFUSES when the solve does not verify — the oracle test", async () => {
    // This is the whole design. An endpoint that mints here returns a valid key
    // for any id anyone types, which rate limiting cannot fix (an abuser needs
    // one key, once) and which kills revocation (ids would be attacker-chosen).
    solveOk = false;
    const r = await enroll({ id: DEVICE_ID, token: "bad-token" });
    expect(r.status).toBe(403);
    expect(r.body.error).toBe("unverified");
    expect(r.body.key).toBeUndefined();
  });

  it("refuses with no token at all, and never calls siteverify", async () => {
    const r = await enroll({ id: DEVICE_ID });
    expect(r.status).toBe(400);
    expect(r.body.key).toBeUndefined();
    expect(siteverifyCalls).toBe(0);
  });

  it("FAILS CLOSED when siteverify is unreachable", async () => {
    // A verifier outage must not become the open endpoint this design exists to
    // avoid: an unverified key is indistinguishable from one an attacker asked
    // for, so a Cloudflare blip must cost enrollment, not correctness.
    vi.stubGlobal("fetch", async () => {
      throw new Error("network down");
    });
    const r = await enroll({ id: DEVICE_ID, token: "good-token" });
    expect(r.status).toBe(403);
    expect(r.body.key).toBeUndefined();
  });

  it("refuses to mint when no Turnstile secret is configured", async () => {
    const r = await enroll({ id: DEVICE_ID, token: "good-token" }, { TURNSTILE_SECRET_KEY: undefined });
    expect(r.status).toBe(503);
    expect(r.body.key).toBeUndefined();
  });

  it("rejects a malformed device id", async () => {
    for (const bad of ["", "ZZZZ", "a1b2", "../../etc", "a".repeat(64)]) {
      const r = await enroll({ id: bad, token: "good-token" });
      expect(r.status, `id ${JSON.stringify(bad)} must not mint`).toBe(400);
    }
  });

  it("refuses GET, so the page load can never mint", async () => {
    const res = await call(
      new Request("https://proxy.test/blipscope/enroll"),
      enrollEnv({ TURNSTILE_SITEKEY: "0xSITEKEY" }),
    );
    // A GET is the PAGE, not the mint. It must return HTML and no key.
    expect(res.status).toBe(200);
    const text = await res.text();
    expect(text).toContain("<!doctype html>");
    expect(text).not.toMatch(/[0-9a-f]{64}/);
  });
});

describe("enrollment is idempotent across a reflash", () => {
  it("returns the SAME key and says already_enrolled", async () => {
    // Blipscope -> Missileer -> Blipscope, or a full erase. The id is derived
    // from the efuse MAC, so it is unchanged; the key is a pure function of the
    // id, so re-enrolling is a re-derivation and not a re-issue. A customer who
    // reflashed must not hit a wall.
    const first = await enroll({ id: DEVICE_ID, token: "t1" });
    const second = await enroll({ id: DEVICE_ID, token: "t2" });

    expect(first.status).toBe(200);
    expect(second.status).toBe(200);
    expect(second.body.key).toBe(first.body.key);
    expect(first.body.status).toBe("enrolled");
    expect(second.body.status).toBe("already_enrolled");
    expect(second.body.enrollments).toBe(2);
  });
  // ---- A ROTATED DEVICE MUST GET A NEW KEY, NOT A RECEIPT --------------------
  //
  // Asked on 2026-08-31 with a rotated board sitting on the verify page reading
  // "Already verified": does that path re-derive against the CURRENT secret, or
  // short-circuit on the enrolment record existing and send nothing useful?
  //
  // It re-derives, because `key` is computed BEFORE the ledger read and returned
  // on both branches. This pins that. If the derivation ever moves below the
  // ledger read, or `key` is dropped from the repeat branch as redundant, a
  // rotated device would be told it is fine while still 401ing.
  it("after a SECRET ROTATION, already_enrolled carries a NEW key", async () => {
    const before = await enroll({ id: DEVICE_ID, token: "t1" });
    expect(before.body.status).toBe("enrolled");

    // The rotation: same device, same id, different DEVICE_KEY_SECRET.
    const after = await enroll({ id: DEVICE_ID, token: "t2" },
                               { DEVICE_KEY_SECRET: "a-rotated-device-secret" });

    expect(after.status).toBe(200);
    expect(after.body.status).toBe("already_enrolled");
    expect(typeof after.body.key).toBe("string");
    expect(after.body.key).toMatch(/^[0-9a-f]{64}$/);
    expect(after.body.key).not.toBe(before.body.key);   // the whole point
  });

  it("CONTROL: without a rotation the repeat key is unchanged", async () => {
    // Without this, the assertion above passes against an endpoint that returns
    // a random string, or one that changes the key on every call -- which would
    // break the reflash case the idempotency test exists for.
    const a = await enroll({ id: DEVICE_ID, token: "t1" });
    const b = await enroll({ id: DEVICE_ID, token: "t2" });
    expect(b.body.key).toBe(a.body.key);
  });



  it("logs the repeat in the ledger rather than blocking it", async () => {
    await enroll({ id: DEVICE_ID, token: "t1" });
    await enroll({ id: DEVICE_ID, token: "t2" });
    await enroll({ id: DEVICE_ID, token: "t3" });
    const row = await env.ENRICH_KV.get<{ enrollments: number; firstAt: string; lastAt: string }>(
      `enr:dev:${DEVICE_ID}`,
      "json",
    );
    expect(row?.enrollments).toBe(3);
    // firstAt is the ORIGINAL enrollment: a reflash must not erase the history
    // that makes a repeat legible as a repeat.
    expect(new Date(row!.lastAt) >= new Date(row!.firstAt)).toBe(true);
  });

  it("counts enrollment volume, which is the only abuse signal we get", async () => {
    // There is no upstream ceiling to hit, so nothing else will ever say
    // "something is wrong". If this counter stops being written, abuse becomes
    // invisible rather than merely unmeasured.
    const day = new Date().toISOString().slice(0, 10);
    await enroll({ id: DEVICE_ID, token: "t1" });
    await enroll({ id: "b2c3d4e5f6a7b8c9", token: "t2" });
    expect(Number(await env.ENRICH_KV.get(`enr:day:${day}`))).toBe(2);
  });
});

describe("revocation survives a reflash", () => {
  const REVOKED = "dead0000beef1111";

  it("refuses to enroll a revoked id, and issues no key", async () => {
    await env.ENRICH_KV.put(REVOKED_KEY, REVOKED);
    resetRevocationCache();
    const r = await enroll({ id: REVOKED, token: "good-token" });
    expect(r.status).toBe(403);
    expect(r.body.error).toBe("revoked");
    expect(r.body.key).toBeUndefined();
    await env.ENRICH_KV.delete(REVOKED_KEY);
    resetRevocationCache();
  });

  it("still refuses the key at the AUTH boundary, not only at enrollment", async () => {
    // Defence in depth, and the assertion that actually matters: even if a
    // revoked board obtained its key before revocation — which it did, that is
    // the normal case — the key must stop working. Enrollment refusal alone
    // would be a door locked next to an open window.
    const key = await deriveDeviceKey(SECRET, REVOKED);
    await env.ENRICH_KV.put(REVOKED_KEY, REVOKED);
    resetRevocationCache();
    const res = await call(
      new Request("https://proxy.test/api/v1/blipscope/config", {
        headers: { "X-Blip-Key": key, "X-Blip-Device": REVOKED },
      }),
      enrollEnv(),
    );
    expect(res.status).toBe(401);
    await env.ENRICH_KV.delete(REVOKED_KEY);
    resetRevocationCache();
  });

  it("CONTROL: the same id authenticates fine when NOT revoked", async () => {
    await env.ENRICH_KV.delete(REVOKED_KEY);
    resetRevocationCache();
    // Without this the test above proves nothing — a 401 for an unrelated reason
    // (bad key shape, missing secret, wrong path) would read as revocation
    // working. This is the check that the check can pass.
    const key = await deriveDeviceKey(SECRET, REVOKED);
    const res = await call(
      new Request("https://proxy.test/api/v1/blipscope/config", {
        headers: { "X-Blip-Key": key, "X-Blip-Device": REVOKED },
      }),
      enrollEnv(),
    );
    expect(res.status).not.toBe(401);
  });
});

describe("which credential was accepted is observable", () => {
  // While BOTH the shared key and per-device keys are valid, "the board still
  // works" cannot tell them apart — an enrolled device could be running on the
  // old shared key and nothing would say so until removal broke it. That is a
  // check that cannot fail, and this is the fix.
  it("reports device auth on a per-device key", async () => {
    const key = await deriveDeviceKey(SECRET, DEVICE_ID);
    const res = await call(
      new Request("https://proxy.test/api/v1/blipscope/config", {
        headers: { "X-Blip-Key": key, "X-Blip-Device": DEVICE_ID },
      }),
      enrollEnv(),
    );
    expect(res.headers.get("X-Blip-Auth")).toBe("device");
  });

  // This used to assert X-Blip-Auth: "shared" — the case the bench had to see
  // BEFORE the enrolled one, so that a passing check proved something. With the
  // shared list removed there is no longer a credential that can produce it, and
  // the header's remaining job is to keep saying "device" out loud. Replaced
  // rather than deleted, because the reason it existed is the reason the cutover
  // was verifiable at all.
  it("cannot report shared auth any more — there is no credential that produces it", async () => {
    const res = await call(
      new Request("https://proxy.test/api/v1/blipscope/config", {
        headers: { "X-Blip-Key": "old-shared-key" }, // what the fleet used to send
      }),
      enrollEnv(),
    );
    expect(res.status).toBe(401);
    expect(res.headers.get("X-Blip-Auth")).toBeNull();
  });

  it("sends no header when nothing authenticated, so absence is meaningful", async () => {
    const res = await call(
      new Request("https://proxy.test/api/v1/blipscope/config", {
        headers: { "X-Blip-Key": "not-a-key" },
      }),
      enrollEnv(),
    );
    expect(res.status).toBe(401);
    expect(res.headers.get("X-Blip-Auth")).toBeNull();
  });
});

/* ===========================================================================
 * REACHABILITY — the property the sixteen tests above could not see.
 *
 * Every one of them requests "/blipscope/enroll" directly. The firmware opened
 * "scopes.valarsystems.com/enroll", which this Worker did not route at all: the
 * feature was a 404 behind a full passing suite, because the tests and the
 * firmware disagreed about the URL and only one of them was being asked.
 *
 * WHAT THIS BLOCK CAN AND CANNOT DO. It pins both paths from the Worker's side,
 * so neither can be dropped silently. It CANNOT read the URL the firmware
 * actually ships -- these tests run inside workerd with no filesystem, so the
 * C++ string is not visible here. That half is checked where it can be checked
 * against the real artifact: scripts/smoke-prod.sh greps the enrol URLs out of
 * src/ConfigurationWebServer.cpp and fetches each one against the live Worker.
 * ======================================================================== */
describe("enrollment is reachable at the URLs that are actually used", () => {
  it("serves the page at the canonical path the firmware popup opens", async () => {
    const res = await call(
      new Request(`https://proxy.test/blipscope/enroll?id=${DEVICE_ID}`),
      enrollEnv({ TURNSTILE_SITEKEY: "0xTESTSITEKEY" }),
    );
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toContain("text/html");
    const html = await res.text();
    // The sitekey reaching the markup is the only thing that proves the binding
    // is wired end to end; an empty one renders the "cannot verify" state, which
    // looks like a blocked network rather than a missing secret.
    expect(html).toContain('data-sitekey="0xTESTSITEKEY"');
  });

  it("301s the short typed URL to the canonical one, query intact", async () => {
    const res = await call(new Request(`https://proxy.test/enroll?id=${DEVICE_ID}`), enrollEnv());
    expect(res.status).toBe(301);
    // The id MUST survive: the fallback tells a customer to type this URL with
    // their device id on the end, and a redirect that dropped it would land them
    // on the "no device id in this link" state having done nothing wrong.
    expect(res.headers.get("Location")).toBe(
      `https://proxy.test/blipscope/enroll?id=${DEVICE_ID}`,
    );
  });

  it("does NOT redirect a POST to the short URL — a body must never be re-sent", async () => {
    const res = await call(
      new Request("https://proxy.test/enroll", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ id: DEVICE_ID, token: "t" }),
      }),
      enrollEnv(),
    );
    expect(res.status).toBe(405);
  });

  it("counts both paths as themselves in metrics, not as /other", () => {
    // Enrollment volume IS the abuse detection, so these must not bucket into
    // the same group as every 404 an internet scanner produces.
    expect(routeTemplate("/blipscope/enroll")).toBe("/blipscope/enroll");
    expect(routeTemplate("/enroll")).toBe("/enroll");
    expect(routeTemplate("/enrollment")).toBe("/other"); // control: /other still exists
  });
});
