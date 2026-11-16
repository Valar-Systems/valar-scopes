import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import type { Env } from "../src/types";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call, hexBody, makeAc } from "./helpers";

// docs/adsbdb-off-by-default.md. adsbdb is OFF unless a variable says exactly "true", and its TYPE backfill has a
// switch of its own. The owner's ruling (2026-11-16): adsbdb's aircraft data is PlaneBase-derived, and PlaneBase's terms
// are personal use only, so the type backfill must not run under the default config -- and must not come back on just
// because the ROUTE source is re-enabled one day.

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});
beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

const LOL = "https://api.adsb.lol";
const ADSBDB = "https://api.adsbdb.com";
// Both variables ABSENT: the "an environment without the var" case, which used to mean ON.
const ABSENT = { ROUTE_ADSBDB_ENABLED: undefined, TYPE_ADSBDB_ENABLED: undefined } as Partial<Env>;

const typedAircraft = (reg: string) =>
  JSON.stringify({ response: { aircraft: { registration: reg, icao_type: "E75L", type: "ERJ 170-200 LR" } } });
const zrhJfk = JSON.stringify({
  response: { flightroute: { origin: { iata_code: "ZRH", icao_code: "LSZH" }, destination: { iata_code: "JFK", icao_code: "KJFK" } } },
});

describe("adsbdb is off by default (docs/adsbdb-off-by-default.md)", () => {
  it("AD1: with both variables absent the TYPE backfill does not run", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ad0001" }).reply(200, hexBody([makeAc({ hex: "ad0001", r: "N900AD", t: "", desc: "" })]));
    fetchMock.get(ADSBDB).intercept({ path: "/v0/aircraft/ad0001" }).reply(200, typedAircraft("N900AD"));

    const body = (await (await call(apiRequest("/v1/enrich/ad0001"), ABSENT)).json()) as { t: string };
    expect(body.t).toBe("");
    // The adsbdb interceptor must still be pending: a backfill that ran would have consumed it, and this fetch
    // would then throw (net connect is disabled).
    expect((await fetch(`${ADSBDB}/v0/aircraft/ad0001`)).status).toBe(200);
  });

  it("AD2: with the route variable absent the adsbdb ROUTE fallback does not run", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ad0002" }).reply(200, hexBody([makeAc({ hex: "ad0002", t: "A320" })]));
    fetchMock.get(LOL).intercept({ path: "/api/0/routeset", method: "POST" }).reply(201, "");
    fetchMock.get(ADSBDB).intercept({ path: "/v0/callsign/ADB2" }).reply(200, zrhJfk);

    const body = (await (await call(apiRequest("/v1/enrich/ad0002?cs=ADB2"), ABSENT)).json()) as { o: string; d: string };
    expect(body.o).toBe("");
    expect(body.d).toBe("");
    expect((await fetch(`${ADSBDB}/v0/callsign/ADB2`)).status).toBe(200);
  });

  it("AD3: the pool's default env (the top-level [vars]) sets both switches to \"false\"", () => {
    const vars = env as unknown as Record<string, unknown>;
    expect(vars.ROUTE_ADSBDB_ENABLED).toBe("false");
    expect(vars.TYPE_ADSBDB_ENABLED).toBe("false");
  });

  it("AD4: DECOUPLED -- TYPE_ADSBDB_ENABLED=\"true\" backfills even with ROUTE_ADSBDB_ENABLED=\"false\"", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ad0004" }).reply(200, hexBody([makeAc({ hex: "ad0004", r: "N904AD", t: "", desc: "" })]));
    fetchMock.get(ADSBDB).intercept({ path: "/v0/aircraft/ad0004" }).reply(200, typedAircraft("N904AD"));

    const res = await call(apiRequest("/v1/enrich/ad0004"), { TYPE_ADSBDB_ENABLED: "true", ROUTE_ADSBDB_ENABLED: "false" } as Partial<Env>);
    expect(((await res.json()) as { t: string }).t).toBe("E75L");
  });

  it("AD6: /healthz shows the type backfill OFF, with the licence reason, where a person already looks", async () => {
    const res = await call(new Request("https://proxy.test/healthz"));
    expect(res.status).toBe(200);
    const ups = ((await res.json()) as { upstreams: { id: string; enabled: boolean; reason?: string }[] }).upstreams;
    const ac = ups.filter((u) => u.id === "adsbdb_ac");
    expect(ac).toHaveLength(1);
    expect(ac[0]).toMatchObject({ enabled: false, reason: "licence" });
  });

  it("AD5 (control): ROUTE_ADSBDB_ENABLED=\"true\" still falls back for the route", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ad0005" }).reply(200, hexBody([makeAc({ hex: "ad0005", t: "A320" })]));
    fetchMock.get(LOL).intercept({ path: "/api/0/routeset", method: "POST" }).reply(201, "");
    fetchMock.get(ADSBDB).intercept({ path: "/v0/callsign/ADB5" }).reply(200, zrhJfk);

    const body = (await (await call(apiRequest("/v1/enrich/ad0005?cs=ADB5"), { ROUTE_ADSBDB_ENABLED: "true" } as Partial<Env>)).json()) as { o: string; d: string };
    expect(body.o).toBe("ZRH");
    expect(body.d).toBe("JFK");
  });
});
