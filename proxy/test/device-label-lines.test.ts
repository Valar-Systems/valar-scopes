import { env } from "cloudflare:test";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { deviceLabel } from "../src/devlabel";
import { deviceIdFromMac, handleProvision, PROVISION_DAILY_CAP } from "../src/provision";
import { REVOKED_KEY, resetRevocationCache } from "../src/revocation";
import fixture from "./fixtures/mint-equivalence.json";
import { AUTH_HEADERS, call, testEnv } from "./helpers";

/* ===========================================================================
 * "LABEL TOO" (the owner, 2026-11-17): the log lines that carried a device id
 * outside the request line carry the device label instead -- the first 8 hex of
 * the id's SHA-256 (src/devlabel.ts), never the id. Each line's EXACT key (or
 * token) set is asserted, so a line that grows the id back under any name, or
 * loses its label, fails here. The Analytics Engine point keeps the id: it is
 * not a log line, and the ruling is about log lines.
 *
 *   LL1-LL3  enroll.ts: the refused, watch and mint text lines
 *   LL4      provision.ts: record() -- a refusal with no device, the three
 *            refusals that carry one, and the mint; LL4e the AE point keeps the id
 *   LL5-LL7  leaderboard.ts: lb_clamp, incoherent_row, and incoherent_row for a
 *            stored id that is not a device id (no label, and the board still builds)
 * ======================================================================== */

let lines: string[] = [];
beforeEach(() => {
  lines = [];
  vi.spyOn(console, "log").mockImplementation((...a: unknown[]) => { lines.push(a.map(String).join(" ")); });
});
afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

const json = () => lines.map((l) => { try { return JSON.parse(l) as Record<string, unknown>; } catch { return null; } })
  .filter((o): o is Record<string, unknown> => o !== null);
const keys = (o: Record<string, unknown>) => Object.keys(o).sort();
/** The `k=v` tokens of a text line, keys only, sorted. */
const tokenKeys = (line: string) => line.split(" ").filter((t) => t.includes("=")).map((t) => t.split("=")[0]).sort();
const tokenVal = (line: string, k: string) => line.split(" ").find((t) => t.startsWith(`${k}=`))?.slice(k.length + 1);
const LABEL = /^[0-9a-f]{8}$/;

// ---- enroll.ts ---------------------------------------------------------------

describe("LL1-LL3: the enroll lines carry the label, never the id", () => {
  const SECRET = "test-device-secret";
  const TOKEN = "test-provision-token-not-a-real-one";
  const LISTED = "1111222233334444"; // fake ids from scripts/device-id-allowlist.txt
  const OTHER = "aaaabbbbccccdddd";
  const baseEnv = (o: Record<string, unknown> = {}) =>
    ({ DEVICE_KEY_SECRET: SECRET, TURNSTILE_SECRET_KEY: "test-turnstile-secret", PROVISION_TOKEN: TOKEN, ...o });

  beforeEach(() => {
    vi.stubGlobal("fetch", async (input: RequestInfo | URL) => {
      const url = String(input instanceof Request ? input.url : input);
      if (url.includes("challenges.cloudflare.com"))
        return new Response(JSON.stringify({ success: true, hostname: "scopes.valarsystems.com" }),
          { headers: { "content-type": "application/json" } });
      return new Response("unexpected upstream", { status: 502 });
    });
  });
  const activate = (id: string, o: Record<string, unknown> = {}) =>
    call(new Request("https://proxy.test/blipscope/enroll", {
      method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ id, token: "a-token" }),
    }), baseEnv(o));
  const list = (id: string) =>
    call(new Request("https://proxy.test/blipscope/activation", {
      method: "POST", headers: { "content-type": "application/json", "X-Provision-Token": TOKEN },
      body: JSON.stringify({ op: "add", ids: [id] }),
    }), baseEnv());
  const enrollLine = (prefix: string) => {
    const found = lines.filter((l) => l.startsWith(prefix));
    expect(found, `exactly one line starting "${prefix}"`).toHaveLength(1);
    return found[0]!;
  };

  it("LL1: a refused activation (enforce): refused, devl, host, cc -- and no id", async () => {
    expect((await activate(OTHER)).status).toBe(403);
    const line = enrollLine("enroll refused=");
    expect(tokenKeys(line)).toEqual(["cc", "devl", "host", "refused"]);
    expect(tokenVal(line, "devl")).toBe(await deviceLabel(OTHER));
    expect(line).not.toContain(OTHER);
  });

  it("LL2: a would-be refusal under watch: watch, devl, host, cc -- and no id", async () => {
    expect((await activate(OTHER, { ACTIVATION_GATE: "watch" })).status).toBe(200);
    const line = enrollLine("enroll watch=");
    expect(tokenKeys(line)).toEqual(["cc", "devl", "host", "watch"]);
    expect(tokenVal(line, "devl")).toBe(await deviceLabel(OTHER));
    expect(line).not.toContain(OTHER);
  });

  it("LL3: a mint: devl, n, day_total, host, cc, asn -- and no id", async () => {
    expect((await list(LISTED)).status).toBe(200);
    expect((await activate(LISTED)).status).toBe(200);
    const line = enrollLine("enroll new ");
    expect(tokenKeys(line)).toEqual(["asn", "cc", "day_total", "devl", "host", "n"]);
    expect(tokenVal(line, "devl")).toBe(await deviceLabel(LISTED));
    expect(line).not.toContain(LISTED);
  });
});

// ---- provision.ts ------------------------------------------------------------

describe("LL4: provision's record() logs the label; its AE point keeps the id", () => {
  const TOKEN = "test-provision-token-not-a-real-one";
  const today = () => new Date().toISOString().slice(0, 10);
  const mintReq = (mac: string | null, token = TOKEN) => new Request("https://proxy.test/blipscope/provision", {
    method: "POST", headers: { "Content-Type": "application/json", "X-Provision-Token": token },
    body: JSON.stringify(mac === null ? {} : { mac }),
  });
  const evt = (name: string, reason?: string) => {
    const found = json().filter((o) => o.evt === name && (reason === undefined || o.reason === reason));
    expect(found, `exactly one ${name} ${reason ?? ""}`).toHaveLength(1);
    return found[0]!;
  };
  afterEach(async () => {
    await env.ENRICH_KV.delete(`prov:day:${today()}`);
    await env.ENRICH_KV.delete(REVOKED_KEY);
    resetRevocationCache();
  });

  it("LL4a: a refusal that has no device (bad token) logs evt, operator, reason only", async () => {
    expect((await call(mintReq(fixture.cases[0]!.mac, "wrong-token"), { PROVISION_TOKEN: TOKEN })).status).toBe(403);
    expect(keys(evt("provision_refused", "bad_token"))).toEqual(["evt", "operator", "reason"]);
  });

  it("LL4b: the fake-id refusal: devl and mac_hash, no device", async () => {
    const mac = fixture.cases[1]!.mac;
    const res = await handleProvision(mintReq(mac), testEnv({ PROVISION_TOKEN: TOKEN }), () => true);
    expect(res.status).toBe(403);
    const o = evt("provision_refused", "fake_device_id");
    expect(keys(o)).toEqual(["devl", "evt", "mac_hash", "operator", "reason"]);
    expect(o.devl).toBe(await deviceLabel(await deviceIdFromMac(mac)));
  });

  it("LL4c: the revoked refusal: devl and mac_hash, no device", async () => {
    const mac = fixture.cases[2]!.mac;
    await env.ENRICH_KV.put(REVOKED_KEY, await deviceIdFromMac(mac));
    resetRevocationCache();
    expect((await call(mintReq(mac), { PROVISION_TOKEN: TOKEN })).status).toBe(403);
    const o = evt("provision_refused", "revoked");
    expect(keys(o)).toEqual(["devl", "evt", "mac_hash", "operator", "reason"]);
    expect(o.devl).toBe(await deviceLabel(await deviceIdFromMac(mac)));
  });

  it("LL4d: the daily-cap refusal and the mint: devl, mac_hash, mints_today, no device", async () => {
    const mac = fixture.cases[3]!.mac;
    const id = await deviceIdFromMac(mac);
    await env.ENRICH_KV.put(`prov:day:${today()}`, String(PROVISION_DAILY_CAP));
    expect((await call(mintReq(mac), { PROVISION_TOKEN: TOKEN })).status).toBe(429);
    const cap = evt("provision_refused", "daily_cap");
    expect(keys(cap)).toEqual(["devl", "evt", "mac_hash", "mints_today", "operator", "reason"]);
    expect(cap.devl).toBe(await deviceLabel(id));

    await env.ENRICH_KV.put(`prov:day:${today()}`, "0");
    expect((await call(mintReq(mac), { PROVISION_TOKEN: TOKEN })).status).toBe(200);
    const m = evt("provision_mint");
    expect(keys(m)).toEqual(["devl", "evt", "mac_hash", "mints_today", "operator"]);
    expect(m.devl).toBe(await deviceLabel(id));
    expect(lines.join("\n")).not.toContain(id);
  });

  it("LL4e: CONTROL -- the mint's Analytics Engine point still carries the device id", async () => {
    const mac = fixture.cases[0]!.mac;
    const points: { blobs: string[] }[] = [];
    const METRICS = { writeDataPoint: (p: { blobs: string[] }) => void points.push(p) };
    expect((await call(mintReq(mac), { PROVISION_TOKEN: TOKEN, METRICS } as never)).status).toBe(200);
    const p = points.find((x) => x.blobs[0] === "provision");
    expect(p?.blobs[3]).toBe(await deviceIdFromMac(mac));
  });
});

// ---- leaderboard.ts ----------------------------------------------------------

describe("LL5-LL7: the leaderboard lines carry the label, never the id", () => {
  const API = "/api/v1/blipscope";
  const ID_A = "aaaa0001";
  const ID_C = "cccc0003";
  const submit = (body: unknown) => new Request(`https://proxy.test${API}/leaderboard`, {
    method: "POST", headers: { ...AUTH_HEADERS, "Content-Type": "application/json" }, body: JSON.stringify(body),
  });
  const month = () => new Date().toISOString().slice(0, 7);
  /** A stored row whose score has no claim behind it: a claimed type in the map, zero claimed counts. */
  const incoherentRow = (id: string) => ({
    id, name: `Row ${id.slice(0, 4)}`, model: "test", verified: true, radiusKm: 100,
    counts: { types: 0, airlines: 0, countries: 0, airports: 0 },
    claimed: { types: 0, airlines: 0, countries: 0, airports: 0 },
    types: { A320: month() }, seasonMonth: month(),
    monthStartCounts: { types: 0, airlines: 0, countries: 0, airports: 0 },
    streakDays: 0, streakLastDay: "", createdAt: Date.now(), updatedAt: Date.now(), legacy: false,
  });
  afterEach(async () => {
    for (const prefix of ["lb:dev:", "lb:name:", "lb:firsttype:", "lb:board"]) {
      const l = await env.ENRICH_KV.list({ prefix });
      await Promise.all(l.keys.map((k) => env.ENRICH_KV.delete(k.name)));
    }
  });
  const one = (name: string) => {
    const found = json().filter((o) => o.evt === name);
    expect(found, `exactly one ${name}`).toHaveLength(1);
    return found[0]!;
  };

  it("LL5: lb_clamp: devl, model, legacy, clamps -- and no id", async () => {
    await call(submit({ id: ID_A, name: "Drifty", claimed: { types: 2, airlines: 6, countries: 0, airports: 0 }, claimedTypes: ["A320", "B738"] }));
    lines = [];
    await call(submit({ id: ID_A, name: "Drifty", claimed: { types: 2, airlines: 4, countries: 0, airports: 0 }, claimedTypes: ["A320", "B738"] }));
    const o = one("lb_clamp");
    expect(keys(o)).toEqual(["clamps", "devl", "evt", "legacy", "model"]);
    expect(o.devl).toBe(await deviceLabel(ID_A));
    expect(lines.join("\n")).not.toContain(ID_A);
  });

  it("LL6: incoherent_row: devl and points -- and no id", async () => {
    await env.ENRICH_KV.put(`lb:dev:${ID_C}`, JSON.stringify(incoherentRow(ID_C)));
    expect((await call(new Request("https://proxy.test/blipscope/leaderboard.json"))).status).toBe(200);
    const o = one("incoherent_row");
    expect(keys(o)).toEqual(["devl", "evt", "points"]);
    expect(o.devl).toBe(await deviceLabel(ID_C));
    expect(o.devl).toMatch(LABEL);
    expect(lines.join("\n")).not.toContain(ID_C);
  });

  it("LL7: a stored id that is not a device id: no label, no id, and the board still builds", async () => {
    const bad = "zz-not-hex";
    await env.ENRICH_KV.put(`lb:dev:${bad}`, JSON.stringify(incoherentRow(bad)));
    expect((await call(new Request("https://proxy.test/blipscope/leaderboard.json"))).status).toBe(200);
    const o = one("incoherent_row");
    expect(keys(o)).toEqual(["evt", "points"]);
    expect(lines.join("\n")).not.toContain(bad);
  });
});
