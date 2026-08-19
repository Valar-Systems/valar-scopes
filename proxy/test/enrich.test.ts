import { env, fetchMock } from "cloudflare:test";
import { afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { __resetBreakersForTests } from "../src/upstreams/types";
import { apiRequest, call, hexBody, makeAc } from "./helpers";

beforeAll(() => {
  fetchMock.activate();
  fetchMock.disableNetConnect();
});

beforeEach(() => __resetBreakersForTests());
afterEach(() => fetchMock.assertNoPendingInterceptors());

const LOL = "https://api.adsb.lol";
const ADSBDB = "https://api.adsbdb.com";

describe("/v1/enrich/{hex}", () => {
  it("joins aircraft metadata and route into one compact response, then serves from KV", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/4b1817" })
      .reply(200, hexBody([makeAc({ desc: "Airbus A340-313", ownOp: "Swiss International Air Lines" })]));
    fetchMock
      .get(LOL)
      .intercept({ path: "/api/0/routeset", method: "POST" })
      .reply(200, JSON.stringify([{ callsign: "SWR123", _airport_codes_iata: "ZRH-JFK", plausible: true }]));

    // Mixed-case hex + callsign normalize; live position feeds plausibility.
    const res = await call(apiRequest("/v1/enrich/4B1817?cs=swr123&lat=47.4&lon=8.55"));
    expect(res.status).toBe(200);
    const text = await res.text();
    expect(text.length).toBeLessThan(512); // acceptance: enrich responses stay tiny
    expect(JSON.parse(text)).toEqual({
      v: 1,
      r: "HB-JMB",
      t: "A343",
      tn: "Airbus A340-313",
      op: "Swiss International Air Lines",
      o: "ZRH",
      d: "JFK",
    });

    // Second tap: both lookups served from KV -- no interceptors remain, so any
    // real fetch would throw.
    const res2 = await call(apiRequest("/v1/enrich/4b1817?cs=SWR123&lat=47.4&lon=8.55"));
    expect(JSON.parse(await res2.text())).toEqual(JSON.parse(text));
  });

  it("routes enrichment through relay-a, failing over to relay-b", async () => {
    // Which relay LEADS is decided per hex now (partitionOrder in chain.ts), so this
    // test pins a hex that partitions to relay-a in order to stay a test of
    // "primary fails -> secondary answers". e30005 leads with relay-a; its failure is
    // consumed and the chain falls over to relay-b (terminal, so the breaker never
    // skips it), which carries the X-Relay-Key the nginx checks. The relay-b-leading
    // direction is covered by the next test.
    const RA = "https://relay-a.valarsystems.com";
    const RB = "https://relay-b.valarsystems.com";
    const relayEnv = {
      UPSTREAM_ADSB_LOL_BASE: RA,
      UPSTREAM_ADSB_LOL_BASE_B: RB,
      RELAY_KEY: "test-relay-key",
    };
    fetchMock.get(RA).intercept({ path: "/v2/hex/e30005" }).replyWithError(new Error("relay-a down"));
    fetchMock
      .get(RB)
      .intercept({ path: "/v2/hex/e30005", headers: { "x-relay-key": "test-relay-key" } })
      .reply(200, hexBody([{ hex: "e30005", r: "N737XX", t: "B738" }]));

    const res = await call(apiRequest("/v1/enrich/e30005"), relayEnv);
    expect(res.status).toBe(200);
    const body = (await res.json()) as { r: string; tn: string };
    expect(body.r).toBe("N737XX");
    expect(body.tn).toBe("Boeing 737-800"); // relay-b's data, reached via failover
  });

  it("partitions hex lookups across the relay pair, not just tiles", async () => {
    // Hex volume is small, but it is the path that actually takes 429s: a card open
    // fires a lookup with no tile cadence to smooth it, so the BURST is what matters.
    // e30001 partitions to relay-b.
    //
    // RELAY-A IS HEALTHY HERE, for the same reason as the tile version of this test in
    // blips.test.ts: omitting its interceptor makes the test pass with partitioning
    // disabled, because the chain would try relay-a, miss, and fail over to relay-b
    // anyway -- asserting the destination while the failover supplies it.
    const RA = "https://relay-a.valarsystems.com";
    const RB = "https://relay-b.valarsystems.com";
    const relayEnv = {
      UPSTREAM_ADSB_LOL_BASE: RA,
      UPSTREAM_ADSB_LOL_BASE_B: RB,
      RELAY_KEY: "test-relay-key",
    };
    fetchMock.get(RA).intercept({ path: "/v2/hex/e30001" }).reply(200, hexBody([{ hex: "e30001", r: "NOPE", t: "B738" }]));
    fetchMock
      .get(RB)
      .intercept({ path: "/v2/hex/e30001", headers: { "x-relay-key": "test-relay-key" } })
      .reply(200, hexBody([{ hex: "e30001", r: "N747XX", t: "B744" }]));

    const res = await call(apiRequest("/v1/enrich/e30001"), relayEnv);
    expect(res.status).toBe(200);
    expect(((await res.json()) as { r: string }).r).toBe("N747XX"); // relay-b's, not relay-a's

    // relay-a never consulted -- the actual assertion. See the tile version for why
    // this is checked before the interceptor is retired.
    expect(fetchMock.pendingInterceptors().length).toBe(1);
    await fetch(`${RA}/v2/hex/e30001`);
  });

  it("holds a hex down fleet-wide after a failed lookup, instead of re-firing it", async () => {
    // The storm: a 429'd hex cached nothing, so every poll re-fired it. Now a failed
    // lookup writes a brief empty KV marker so the next lookup is a quiet KV hit.
    fetchMock.get(LOL).intercept({ path: "/v2/hex/f40001" }).reply(429, "").times(3); // chain exhausts its 3 retries

    const r1 = await call(apiRequest("/v1/enrich/f40001"));
    expect(r1.status).toBe(200);
    expect(((await r1.json()) as { t: string }).t).toBe(""); // empty this time

    // Second lookup: served from the hold-down marker. NO upstream interceptor is
    // registered, so a re-fire would throw on an unmatched request (assertNoPending...).
    const r2 = await call(apiRequest("/v1/enrich/f40001"));
    expect(r2.status).toBe(200);
    expect(((await r2.json()) as { t: string }).t).toBe("");
  });

  it("falls back to the baked type-name table when the upstream has no desc", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/bbbbbb" })
      .reply(200, hexBody([{ hex: "bbbbbb", r: "N123AB", t: "B738" }]));

    const res = await call(apiRequest("/v1/enrich/bbbbbb"));
    const body = (await res.json()) as { tn: string; o: string };
    expect(body.tn).toBe("Boeing 737-800");
    expect(body.o).toBe(""); // no callsign given -> no route lookup
  });

  it("reports only the ROOT enrichment gap, so a work list built from it is actionable", async () => {
    // A type with a name but no stock photo is a photo-library gap, not three
    // separate gaps -- reporting name+photo+type for one unknown airframe would
    // triple-count it and skew whatever we prioritise off these points.
    const seen: string[] = [];
    const orig = console.log;
    console.log = (...a: unknown[]) => { seen.push(String(a[0])); };
    try {
      fetchMock
        .get(LOL)
        .intercept({ path: "/v2/hex/dddddd" })
        .reply(200, hexBody([{ hex: "dddddd", r: "N1234Z", t: "ZZZZ" }]));
      await call(apiRequest("/v1/enrich/dddddd"));
    } finally {
      console.log = orig;
    }
    const gaps = seen.filter((l) => l.includes('"evt":"enrich_gap"'));
    expect(gaps).toHaveLength(1);
    // ZZZZ is not in TYPE_NAMES, so the name is the root gap -- not the photo.
    expect(gaps[0]).toContain('"gap":"name"');
    expect(gaps[0]).toContain('"t":"ZZZZ"');
  });

  it("strips the upstream 'unconfirmed type' marker so name + photo joins still match", async () => {
    // Mictronics/tar1090 (via adsb.lol) suffix an unconfirmed type with " ?"
    // ("P8 ?"). Passed through verbatim it broke the name lookup AND the
    // type-keyed photo join, and showed "Type: P8 ?" on the card (bug: military
    // P-8 with no name/photo). The code must be normalized to the bare "P8".
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/ae6813" })
      .reply(200, hexBody([{ hex: "ae6813", t: "P8 ?" }]));

    const res = await call(apiRequest("/v1/enrich/ae6813"));
    const body = (await res.json()) as { t: string; tn: string };
    expect(body.t).toBe("P8"); // marker stripped
    expect(body.tn).toBe("Boeing P-8 Poseidon"); // name lookup now matches
  });

  it("prefers a tn:<CODE> KV row over the baked table, and the feed's desc over both", async () => {
    // Precedence is load-bearing for the backfill: scripts/ingest-typenames.ts
    // bulk-loads tn:<CODE> rows, and KV WINS over TYPE_NAMES. That is why the
    // script skips any code already curated -- a blind load would replace
    // "Cessna 140" with the raw aggregate "140".
    await env.ENRICH_KV.put("tn:ZZZZ", "Backfilled Type Name");
    fetchMock.get(LOL).intercept({ path: "/v2/hex/bb0001" }).reply(200, hexBody([{ hex: "bb0001", t: "ZZZZ" }]));
    const a = (await (await call(apiRequest("/v1/enrich/bb0001"))).json()) as { tn: string };
    expect(a.tn).toBe("Backfilled Type Name");

    // ...but the feed's own desc still outranks KV.
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/bb0002" })
      .reply(200, hexBody([{ hex: "bb0002", t: "ZZZZ", desc: "Feed Description" }]));
    const b = (await (await call(apiRequest("/v1/enrich/bb0002"))).json()) as { tn: string };
    expect(b.tn).toBe("Feed Description");
  });

  it("reports tn_miss when no source can name the type", async () => {
    // The discovery signal for the long tail: feed has no desc, KV has no row,
    // the baked table has no entry -> the card shows a raw designator, and we
    // want the fleet to tell us rather than waiting for someone to notice.
    const logged: string[] = [];
    const orig = console.log;
    console.log = (...a: unknown[]) => void logged.push(String(a[0]));
    try {
      fetchMock.get(LOL).intercept({ path: "/v2/hex/bb0003" }).reply(200, hexBody([{ hex: "bb0003", t: "QQQQ" }]));
      const res = await call(apiRequest("/v1/enrich/bb0003"));
      expect((await res.json() as { tn: string }).tn).toBe("");
    } finally {
      console.log = orig;
    }
    expect(logged.some((l) => l.includes('"evt":"tn_miss"') && l.includes('"t":"QQQQ"'))).toBe(true);
  });

  it("backfills the type from adsbdb when the feed has a hex+reg but no type (airplanes.live failover)", async () => {
    // airplanes.live's failover shape: registration present, ICAO type MISSING.
    // Losing the type would also lose the type-keyed stock photo -- the backfill
    // recovers it from adsbdb (a different host, not subject to adsb.lol's 429).
    fetchMock.get(LOL).intercept({ path: "/v2/hex/a34878" }).reply(200, hexBody([{ hex: "a34878", r: "N310SY" }]));
    fetchMock
      .get(ADSBDB)
      .intercept({ path: "/v0/aircraft/a34878" })
      .reply(200, JSON.stringify({ response: { aircraft: { registration: "N310SY", icao_type: "e75l", type: "ERJ 170-200 LR" } } }));

    const res = await call(apiRequest("/v1/enrich/a34878"));
    const body = (await res.json()) as { r: string; t: string; tn: string };
    expect(body.t).toBe("E75L"); // uppercased
    expect(body.r).toBe("N310SY");
    expect(body.tn).toBe("Embraer E175 (long wing)"); // our table, not adsbdb's verbose string

    // Backfilled meta is cached: the repeat needs no interceptors (any real fetch throws).
    const res2 = await call(apiRequest("/v1/enrich/a34878"));
    expect((await res2.json() as { t: string }).t).toBe("E75L");
  });

  it("caches negative aircraft lookups", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/cccccc" }).reply(200, hexBody([]));

    const res = await call(apiRequest("/v1/enrich/cccccc"));
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({ v: 1, r: "", t: "", tn: "", op: "", o: "", d: "" });

    // Cached negative: no interceptor left, KV must answer.
    const res2 = await call(apiRequest("/v1/enrich/cccccc"));
    expect(res2.status).toBe(200);
  });

  it("blanks implausible routes when a live position was provided", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/dddd01" }).reply(200, hexBody([{ hex: "dddd01" }]));
    fetchMock
      .get(LOL)
      .intercept({ path: "/api/0/routeset", method: "POST" })
      .reply(200, JSON.stringify([{ callsign: "ABC1", _airport_codes_iata: "AMS-LHR", plausible: false }]));

    const res = await call(apiRequest("/v1/enrich/dddd01?cs=ABC1&lat=1&lon=1"));
    const body = (await res.json()) as { o: string; d: string };
    expect(body.o).toBe("");
    expect(body.d).toBe("");
  });

  it("accepts route codes without plausibility when no position was provided", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/dddd02" }).reply(200, hexBody([{ hex: "dddd02" }]));
    fetchMock
      .get(LOL)
      .intercept({ path: "/api/0/routeset", method: "POST" })
      .reply(200, JSON.stringify([{ callsign: "ABC2", airport_codes: "AMS-LHR", plausible: false }]));

    const res = await call(apiRequest("/v1/enrich/dddd02?cs=ABC2"));
    const body = (await res.json()) as { o: string; d: string };
    expect(body.o).toBe("AMS");
    expect(body.d).toBe("LHR");
  });

  it("treats 'unknown' route strings as no route (adsbdb fallback disabled)", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/dddd03" }).reply(200, hexBody([{ hex: "dddd03" }]));
    fetchMock
      .get(LOL)
      .intercept({ path: "/api/0/routeset", method: "POST" })
      .reply(200, JSON.stringify([{ callsign: "ABC3", airport_codes: "unknown", plausible: false }]));

    const res = await call(apiRequest("/v1/enrich/dddd03?cs=ABC3"), { ROUTE_ADSBDB_ENABLED: "false" });
    const body = (await res.json()) as { o: string; d: string };
    expect(body.o).toBe("");
    expect(body.d).toBe("");
  });

  it("falls back to adsbdb when routeset returns its empty 201 (observed live)", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/dddd04" }).reply(200, hexBody([{ hex: "dddd04" }]));
    // The real-world behaviour that broke enrich on staging: 201, empty body.
    fetchMock.get(LOL).intercept({ path: "/api/0/routeset", method: "POST" }).reply(201, "");
    fetchMock
      .get(ADSBDB)
      .intercept({ path: "/v0/callsign/ABC4" })
      .reply(
        200,
        JSON.stringify({
          response: {
            flightroute: {
              origin: { iata_code: "ZRH", icao_code: "LSZH" },
              destination: { iata_code: "JFK", icao_code: "KJFK" },
            },
          },
        }),
      );

    const res = await call(apiRequest("/v1/enrich/dddd04?cs=ABC4&lat=47&lon=8"));
    const body = (await res.json()) as { o: string; d: string };
    expect(body.o).toBe("ZRH");
    expect(body.d).toBe("JFK");

    // Definitive adsbdb answer was cached: the repeat needs no interceptors.
    const res2 = await call(apiRequest("/v1/enrich/dddd04?cs=ABC4&lat=47&lon=8"));
    const body2 = (await res2.json()) as { o: string; d: string };
    expect(body2.o).toBe("ZRH");
  });

  it("caches adsbdb's 404 'unknown callsign' as a definitive negative", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/dddd05" }).reply(200, hexBody([{ hex: "dddd05" }]));
    fetchMock.get(LOL).intercept({ path: "/api/0/routeset", method: "POST" }).reply(201, "");
    fetchMock
      .get(ADSBDB)
      .intercept({ path: "/v0/callsign/ABC5" })
      .reply(404, JSON.stringify({ response: "unknown callsign" }));

    const res = await call(apiRequest("/v1/enrich/dddd05?cs=ABC5"));
    expect(((await res.json()) as { o: string }).o).toBe("");

    // Negative cached: repeat resolves from KV with no interceptors left.
    const res2 = await call(apiRequest("/v1/enrich/dddd05?cs=ABC5"));
    expect(res2.status).toBe(200);
  });

  it("survives hex-lookup 429s via retries (shared-egress throttle)", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/eeee01" }).reply(429, "slow down");
    fetchMock.get(LOL).intercept({ path: "/v2/hex/eeee01" }).reply(429, "slow down");
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/eeee01" })
      .reply(200, hexBody([{ hex: "eeee01", r: "N429RT", t: "B738" }]));

    const res = await call(apiRequest("/v1/enrich/eeee01"));
    const body = (await res.json()) as { r: string; tn: string };
    expect(body.r).toBe("N429RT");
    expect(body.tn).toBe("Boeing 737-800");
  });

  it("answers within the serve deadline and caches the slow lookup in the background", async () => {
    const fastEnv = { ENRICH_SERVE_DEADLINE_MS: "50", UPSTREAM_TIMEOUT_MS: "5000" };
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/eeee02" })
      .reply(200, hexBody([{ hex: "eeee02", r: "N123SL", t: "SR22" }]))
      .delay(300); // slower than the 50 ms serve deadline

    // Cold tap: deadline wins, fields come back empty -- but the lookup keeps
    // running via waitUntil (call() waits on it) and lands in KV.
    const r1 = await call(apiRequest("/v1/enrich/eeee02"), fastEnv);
    expect(r1.status).toBe(200);
    expect(((await r1.json()) as { r: string }).r).toBe("");

    // The card's next request is a warm KV hit with the full data.
    const r2 = await call(apiRequest("/v1/enrich/eeee02"), fastEnv);
    const body2 = (await r2.json()) as { r: string; tn: string };
    expect(body2.r).toBe("N123SL");
    expect(body2.tn).toBe("Cirrus SR22");
  });

  it("rejects malformed hexes", async () => {
    expect((await call(apiRequest("/v1/enrich/zzz"))).status).toBe(400);
    expect((await call(apiRequest("/v1/enrich/12345"))).status).toBe(400);
  });
});

describe("military enrichment deepening", () => {
  it("caches an all-empty (found) meta at the negative TTL, full metas at 30 d", async () => {
    // The military-card failure mode: the upstream returns a live position and
    // an empty DB record. Pre-fix this positive-cached for 30 d, keeping a
    // later-appearing record blank for a month.
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5001" }).reply(200, hexBody([{ hex: "ae5001" }]));
    await call(apiRequest("/v1/enrich/ae5001"));
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/bbbb01" })
      .reply(200, hexBody([{ hex: "bbbb01", r: "N100XX", t: "B738" }]));
    await call(apiRequest("/v1/enrich/bbbb01"));

    const list = await env.ENRICH_KV.list({ prefix: "ac:" });
    const empty = list.keys.find((k) => k.name === "ac:ae5001");
    const full = list.keys.find((k) => k.name === "ac:bbbb01");
    const nowS = Date.now() / 1000;
    expect(empty?.expiration).toBeDefined();
    expect(empty!.expiration!).toBeLessThanOrEqual(nowS + 86400 + 300); // ~1 d
    expect(full?.expiration).toBeDefined();
    expect(full!.expiration!).toBeGreaterThan(nowS + 29 * 86400); // ~30 d
  });

  it("floors op for a mil-block hex whose DB record resolved empty", async () => {
    // 0xae5002 sits in the US allocation (0xadf7c8-0xafffff).
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5002" }).reply(200, hexBody([{ hex: "ae5002" }]));
    const res = await call(apiRequest("/v1/enrich/ae5002"));
    const body = (await res.json()) as { r: string; t: string; op: string };
    expect(body.op).toBe("US military");
    expect(body.r).toBe(""); // the floor never invents registrations or types
    expect(body.t).toBe("");
  });

  it("keeps a resolved operator over the floor", async () => {
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/ae5003" })
      .reply(200, hexBody([{ hex: "ae5003", t: "C17", ownOp: "United States Air Force" }]));
    const res = await call(apiRequest("/v1/enrich/ae5003"));
    expect(((await res.json()) as { op: string }).op).toBe("United States Air Force");
  });

  it("prefers ovr: over pa: over mil:, and a curated operator over the block floor", async () => {
    // PRECEDENCE IS THE THING UNDER TEST, and it is a read-path property rather
    // than a run-order one -- so all three side tables are written FIRST, with
    // conflicting values, and only the read decides. If someone later collapses
    // them onto one key and relies on loader order, this fails.
    await env.ENRICH_KV.put("mil:ae6842", JSON.stringify({ r: "MICTRONICS", t: "C17", tn: "Wrong" }));
    await env.ENRICH_KV.put(
      "pa:ae6842",
      JSON.stringify({ t: "P8", tn: "Boeing P-8A Poseidon", op: "United States Navy" }),
    );
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae6842" }).reply(200, hexBody([{ hex: "ae6842" }]));

    const res = await call(apiRequest("/v1/enrich/ae6842"), { ROUTE_ADSBDB_ENABLED: "false" });
    const body = (await res.json()) as { r: string; t: string; tn: string; op: string };
    expect(body.t).toBe("P8");                       // plane-alert beats Mictronics
    expect(body.tn).toBe("Boeing P-8A Poseidon");
    // The CURATED operator must beat militaryOperator()'s block floor, which
    // would otherwise answer the truthful-but-vaguer "US military" for this hex.
    expect(body.op).toBe("United States Navy");
    expect(body.r).toBe("");                         // no registration carried, none invented
  });

  it("a hand override (ovr:) beats the curated table", async () => {
    await env.ENRICH_KV.put("pa:ae6847", JSON.stringify({ t: "P8", op: "United States Navy" }));
    await env.ENRICH_KV.put("ovr:ae6847", JSON.stringify({ t: "P8", tn: "Boeing P-8A Poseidon", op: "US Navy" }));
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae6847" }).reply(200, hexBody([{ hex: "ae6847" }]));

    const res = await call(apiRequest("/v1/enrich/ae6847"), { ROUTE_ADSBDB_ENABLED: "false" });
    expect(((await res.json()) as { op: string }).op).toBe("US Navy");
  });

  it("NEGATIVE CONTROL: with no side-table row, the block floor answers and nothing is guessed", async () => {
    // The control that makes the two tests above mean anything. If this hex
    // resolved a type or a specific operator with every side table empty, those
    // assertions would pass whether or not the tables were consulted at all.
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae6848" }).reply(200, hexBody([{ hex: "ae6848" }]));

    const res = await call(apiRequest("/v1/enrich/ae6848"), { ROUTE_ADSBDB_ENABLED: "false" });
    const body = (await res.json()) as { t: string; op: string };
    expect(body.op).toBe("US military"); // the address-block floor, not a side table
    expect(body.t).toBe("");             // no side table means no type
  });

  it("labels dbFlags-military hexes outside the block table", async () => {
    // 0x3c6444 is a German civil-range hex; dbFlags bit 0 marks it military.
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/3c6444" })
      .reply(200, hexBody([{ hex: "3c6444", dbFlags: 1 }]));
    const res = await call(apiRequest("/v1/enrich/3c6444"));
    expect(((await res.json()) as { op: string }).op).toBe("Military");
  });

  it("applies the floor to cache hits too (pre-floor cached entries heal)", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5004" }).reply(200, hexBody([{ hex: "ae5004" }]));
    await call(apiRequest("/v1/enrich/ae5004"));
    // Second request is a pure KV hit (no interceptors left) -- the floor is
    // serve-time, so it labels the cached empty meta as well.
    const res = await call(apiRequest("/v1/enrich/ae5004"));
    expect(((await res.json()) as { op: string }).op).toBe("US military");
  });

  it("fills op from a military callsign designator, beating the block floor (P3)", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5009" }).reply(200, hexBody([{ hex: "ae5009" }]));
    fetchMock.get(LOL).intercept({ path: "/api/0/routeset", method: "POST" }).reply(201, "");

    const res = await call(apiRequest("/v1/enrich/ae5009?cs=RCH4571"), { ROUTE_ADSBDB_ENABLED: "false" });
    const body = (await res.json()) as { op: string };
    // The designator is more specific than the allocation's "US military".
    expect(body.op).toBe("Air Mobility Command");
  });

  it("fills reg/type/name from the mil side table when the live record is empty (P2)", async () => {
    await env.ENRICH_KV.put(
      "mil:ae5005",
      JSON.stringify({ r: "06-6162", t: "C17", tn: "Boeing C17A Globemaster III" }),
    );
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5005" }).reply(200, hexBody([{ hex: "ae5005" }]));

    const res = await call(apiRequest("/v1/enrich/ae5005"));
    const body = (await res.json()) as { r: string; t: string; tn: string; op: string };
    expect(body.r).toBe("06-6162");
    expect(body.t).toBe("C17");
    expect(body.tn).toBe("Boeing C17A Globemaster III");
    expect(body.op).toBe("US military"); // the floor still owns the operator
  });

  it("mil side-table type unlocks the generic stock photo join", async () => {
    await env.ENRICH_KV.put("mil:ae5006", JSON.stringify({ t: "C17" }));
    await env.ENRICH_KV.put("pptr:t:C17", "photo:C17-0123abcd");
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5006" }).reply(200, hexBody([{ hex: "ae5006" }]));

    const res = await call(apiRequest("/v1/enrich/ae5006"));
    const body = (await res.json()) as { t: string; tn: string; p?: string; pk?: string };
    expect(body.t).toBe("C17");
    expect(body.tn).toBe("Boeing C-17 Globemaster III"); // baked TYPE_NAMES fallback (row had no tn)
    expect(body.p).toBe("/api/v1/blipscope/photo/photo:C17-0123abcd");
    expect(body.pk).toBe("type");
  });

  it("never lets the mil side table override a resolved live record", async () => {
    await env.ENRICH_KV.put("mil:ae5007", JSON.stringify({ r: "STALE-1", t: "K35R" }));
    fetchMock
      .get(LOL)
      .intercept({ path: "/v2/hex/ae5007" })
      .reply(200, hexBody([{ hex: "ae5007", r: "10-0220", t: "C17", ownOp: "United States Air Force" }]));

    const res = await call(apiRequest("/v1/enrich/ae5007"));
    const body = (await res.json()) as { r: string; t: string };
    expect(body.r).toBe("10-0220");
    expect(body.t).toBe("C17");
  });

  it("fills from the mil side table on cached-empty entries too (serve-time)", async () => {
    fetchMock.get(LOL).intercept({ path: "/v2/hex/ae5008" }).reply(200, hexBody([{ hex: "ae5008" }]));
    await call(apiRequest("/v1/enrich/ae5008")); // caches the empty meta
    // The side table is ingested AFTER the empty meta was cached -- the next
    // serve still picks it up, no TTL wait.
    await env.ENRICH_KV.put("mil:ae5008", JSON.stringify({ r: "60-0057", t: "B52" }));

    const res = await call(apiRequest("/v1/enrich/ae5008"));
    const body = (await res.json()) as { r: string; t: string };
    expect(body.r).toBe("60-0057");
    expect(body.t).toBe("B52");
  });
});
