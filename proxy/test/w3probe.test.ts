// W3's staging probe (docs/weatherscope-stage4.md S4.4): behind the device auth, 404 off staging, and nothing but status,
// time, size and max-age in what it returns. The NWS calls are a fake: no test reaches the network.
import { describe, expect, it } from "vitest";
import { type W3Fetch, W3_AIRPORTS, W3_KINDS, handleW3Probe } from "../src/weatherscope/w3probe";
import { apiRequest, call, testEnv } from "./helpers";

const PATH = "/api/v1/blipscope/w3-nws-probe?round=1";

/** A fake NWS: answers each call by its path, and records every call's URL and whether it carried a User-Agent. */
function fakeNws(over: { stationStatus?: number } = {}) {
  const seen: Array<{ url: string; ua: boolean }> = [];
  const json = (o: unknown, status = 200) =>
    new Response(JSON.stringify(o), { status, headers: { "Cache-Control": "public, max-age=300" } });
  const f: W3Fetch = async (url, init) => {
    const ua = !!(init.headers as Record<string, string>)["User-Agent"];
    seen.push({ url, ua });
    const u = new URL(url);
    if (!ua) return json({ title: "Forbidden" }, 403);
    if (/^\/stations\/K[A-Z]{3}$/.test(u.pathname)) {
      if (over.stationStatus) return json({}, over.stationStatus);
      return json({ geometry: { coordinates: [-97.0403123, 32.8998456] } });
    }
    if (u.pathname.startsWith("/points/")) {
      return json({ properties: { forecast: "https://api.weather.gov/gridpoints/FWD/80,108/forecast",
        forecastHourly: "https://api.weather.gov/gridpoints/FWD/80,108/forecast/hourly",
        observationStations: "https://api.weather.gov/gridpoints/FWD/80,108/stations" } });
    }
    if (u.pathname.endsWith("/stations")) return json({ features: [{ properties: { stationIdentifier: "KDFW" } }] });
    return json({ ok: true });
  };
  return { f, seen };
}

describe("W3's staging probe", () => {
  it("W3T1: no device auth, no probe: 401, even with the switch on", async () => {
    const res = await call(new Request(`https://proxy.test${PATH}`), { W3_PROBE: "on" });
    expect(res.status).toBe(401);
  });

  it("W3T2: authenticated, but the switch is off (production's config): 404, and no call is made", async () => {
    const viaRouter = await call(apiRequest(PATH));
    expect(viaRouter.status).toBe(404);
    const { f, seen } = fakeNws();
    const direct = await handleW3Probe(apiRequest(PATH), testEnv(), f);
    expect(direct.status).toBe(404);
    expect(seen.length).toBe(0);
  });

  it("W3T3: a round outside 1-9 is refused before any call", async () => {
    const { f, seen } = fakeNws();
    for (const bad of ["0", "10", "1a", "", "-1"]) {
      const res = await handleW3Probe(apiRequest(`/api/v1/blipscope/w3-nws-probe?round=${bad}`), testEnv({ W3_PROBE: "on" }), f);
      expect(res.status).toBe(400);
    }
    expect(seen.length).toBe(0);
  });

  it("W3T4: one round is 35 calls with a User-Agent and 1 control without, in W5's order", async () => {
    const { f, seen } = fakeNws();
    const res = await handleW3Probe(apiRequest(PATH), testEnv({ W3_PROBE: "on" }), f);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { round: number; airports: Record<string, Array<{ kind: string; status: number }>>; control: { status: number } };
    expect(body.round).toBe(1);
    expect(Object.keys(body.airports)).toEqual([...W3_AIRPORTS]);
    for (const id of W3_AIRPORTS) {
      expect(body.airports[id]!.map((r) => r.kind)).toEqual([...W3_KINDS]);
      expect(body.airports[id]!.every((r) => r.status === 200)).toBe(true);
    }
    expect(seen.length).toBe(36);
    expect(seen.filter((s) => s.ua).length).toBe(35);
    expect(seen[35]!.ua).toBe(false);
    expect(body.control.status).toBe(403);
  });

  it("W3T5: no body, no URL and no coordinate leaves it", async () => {
    const { f } = fakeNws();
    const text = await (await handleW3Probe(apiRequest(PATH), testEnv({ W3_PROBE: "on" }), f)).text();
    expect(text).not.toMatch(/https?:/);
    expect(text).not.toMatch(/gridpoints|FWD|coordinates|properties|stationIdentifier/);
    expect(text).not.toMatch(/\d+\.\d{3,}/);   // the fake's coordinates carry 7 decimals: none may survive
    expect(text.length).toBeGreaterThan(100);   // CONTROL: it still says something
  });

  it("W3T6: a refused station call marks the airport's other 6 calls skipped, not failed", async () => {
    const { f, seen } = fakeNws({ stationStatus: 403 });
    const body = (await (await handleW3Probe(apiRequest(PATH), testEnv({ W3_PROBE: "on" }), f)).json()) as {
      airports: Record<string, Array<{ status: number; err: string | null }>>;
    };
    for (const id of W3_AIRPORTS) {
      expect(body.airports[id]![0]!.status).toBe(403);
      expect(body.airports[id]!.slice(1).every((r) => r.status === 0 && r.err === "skipped")).toBe(true);
    }
    expect(seen.length).toBe(6);   // 5 station calls and the control
  });
});
