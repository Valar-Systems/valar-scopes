// The OPERA backup fallback (docs/wx-opera-backup-fallback.md, section 3): FB1-FB7, HZ1, UA1-UA3, written BEFORE the
// code and run red against main first. Run this file alone (the vitest port rule).
//
// The backup host is the REGISTERED contract (section 1, from ORD's own docs), transcribed here on purpose: the test
// states where the requests must go, and the code has to meet it. Each FB row asserts the EXACT set of (host, kind) its
// requests reached, so the registered sabotage sets are determinable. No coordinate is printed.
import { env } from "cloudflare:test";
import fixturePy from "virtual:opera-fixture-py";
import { describe, expect, it } from "vitest";
import { wxHealth } from "../src/wx/handler";
import { type Fetch, type IngestOpts, type IngestResult, OPERA_BUCKET, OPERA_UA, ingestOnce } from "../src/wx/ingest";
import { HEAD_BYTES } from "../src/wx/opera";
import { OPUK_FIXTURE_GRID, PRODUCTS, type Product, frameKey, framesPrefix, latestKey } from "../src/wx/products";
import dryBin from "./fixtures/wx/opera/20261029T0130.ranges.bin";
import dryRanges from "./fixtures/wx/opera/20261029T0130.ranges.json";

const BACKUP = "https://object-store.os-api.cci1.ecmwf.int/openradar-24h-backup";
const SLOW = 180_000;
const opuk = PRODUCTS.find((p) => p.id === "opuk")!;
const opukFx: Product = { ...opuk, grid: OPUK_FIXTURE_GRID };
const FAST: IngestOpts = { list: { timeoutMs: 8_000, attempts: 3, waitsMs: [0, 0] }, get: { timeoutMs: 20_000, attempts: 2, waitsMs: [0] } };

// ---- the fixture: the DRY frame's exact byte ranges, served for any stamp under either host ---------------------------
const BIN = new Uint8Array(dryBin as ArrayBuffer);
const OFFS = new Map<string, number>();
{ let o = 0; for (const [a, z] of dryRanges.ranges) { OFFS.set(`${a}-${z}`, o); o += z! - a! + 1; } }
const KEY0 = dryRanges.url.slice(OPERA_BUCKET.length + 1);
const E0 = (() => { const m = /@(\d{8})T(\d{4})@/.exec(KEY0)!;
  return Date.parse(`${m[1]!.slice(0, 4)}-${m[1]!.slice(4, 6)}-${m[1]!.slice(6, 8)}T${m[2]!.slice(0, 2)}:${m[2]!.slice(2, 4)}:00Z`) / 1000; })();
const keyAt = (e: number) => {
  const s = new Date(e * 1000).toISOString();
  const stamp = `${s.slice(0, 4)}${s.slice(5, 7)}${s.slice(8, 10)}T${s.slice(11, 13)}${s.slice(14, 16)}`;
  return `${s.slice(0, 4)}/${s.slice(5, 7)}/${s.slice(8, 10)}/OPERA/COMP/OPERA@${stamp}@0@DBZH.tiff`;
};

type Mode = "frame" | "empty" | "fail";
interface Plan { primary: Mode; backup: Mode; primaryReads?: "ok" | "fail"; backupReads?: "ok" | "fail";
                 primaryKeys?: string[]; backupKeys?: string[] }
interface Seen { host: "primary" | "backup" | "other"; kind: "list" | "head" | "tiles"; ua: string | undefined }

function twoHosts(plan: Plan, seen: Seen[]): Fetch {
  return async (u, init) => {
    const h = { ...(init?.headers ?? {}) };
    const host = u.startsWith(`${BACKUP}/`) ? "backup" : u.startsWith(`${OPERA_BUCKET}/`) ? "primary" : "other";
    const kind = u.includes("list-type=2") ? "list" : h.Range === `bytes=0-${HEAD_BYTES - 1}` ? "head" : "tiles";
    seen.push({ host, kind, ua: h["User-Agent"] });
    if (host === "other") return new Response("", { status: 404 });
    const mode = host === "primary" ? plan.primary : plan.backup;
    if (kind === "list") {
      if (mode === "fail") return new Response("", { status: 503 });
      const keys = mode === "empty" ? [] : (host === "primary" ? plan.primaryKeys : plan.backupKeys) ?? [KEY0];
      return new Response(`<ListBucketResult>${keys.map((k) => `<Contents><Key>${k}</Key></Contents>`).join("")}</ListBucketResult>`);
    }
    if (host === "primary" && plan.primaryReads === "fail") return new Response("", { status: 503 });
    if (host === "backup" && plan.backupReads === "fail") return new Response("", { status: 503 });
    const m = /^bytes=(\d+)-(\d+)$/.exec(h.Range ?? "");
    const s = m ? OFFS.get(`${m[1]}-${m[2]}`) : undefined;
    if (s === undefined) return new Response("", { status: 400 });
    return new Response(BIN.slice(s, s + Number(m![2]) - Number(m![1]) + 1), { status: 206 });
  };
}

async function reset(p: Product = opukFx) {
  const l = await env.WX!.list({ prefix: framesPrefix(p) });
  if (l.objects.length) await env.WX!.delete(l.objects.map((o) => o.key));
  await env.WX!.delete(latestKey(p));
}
async function run(plan: Plan, nowMs: number): Promise<{ r?: IngestResult; threw: string; seen: Seen[] }> {
  const seen: Seen[] = [];
  try { return { r: await ingestOnce(env.WX!, opukFx, nowMs, twoHosts(plan, seen), FAST), threw: "", seen }; }
  catch (err) { return { threw: String(err).slice(0, 200), seen }; }
}
const hostKinds = (seen: Seen[]) => [...new Set(seen.map((s) => `${s.host}:${s.kind}`))].sort();
const frameSource = async (e: number) => (await env.WX!.head(frameKey(opukFx, e)))?.customMetadata?.source;
const latest = async () => { const o = await env.WX!.get(latestKey(opukFx)); return o ? JSON.parse(await o.text()) as Record<string, unknown> : null; };
const frames = async () => (await env.WX!.list({ prefix: framesPrefix(opukFx) })).objects.length;
const T = (E0 + 300) * 1000;
const PRIMARY_READ = ["primary:head", "primary:list", "primary:tiles"];
const BACKUP_READ = ["backup:head", "backup:list", "backup:tiles", "primary:list"];

describe("FB: one source per check, never mixed (section 2a)", () => {
  it("FB1: primary non-empty -- only the primary is listed and read; the frame's source is primary", async () => {
    await reset();
    const x = await run({ primary: "frame", backup: "frame" }, T);
    const src = await frameSource(E0);
    console.log(`FB1: ${x.r?.status ?? x.threw}; ${JSON.stringify(hostKinds(x.seen))}; source ${src}`);
    expect(x.r?.status).toBe("stored");
    expect(hostKinds(x.seen)).toEqual(PRIMARY_READ);
    expect(src).toBe("primary");
  }, SLOW);

  it("FB2: primary empty -- the backup is listed and every read goes to it; source backup", async () => {
    await reset();
    const x = await run({ primary: "empty", backup: "frame" }, T);
    const src = await frameSource(E0);
    console.log(`FB2: ${x.r?.status ?? x.threw}; ${JSON.stringify(hostKinds(x.seen))}; source ${src}`);
    expect(x.r?.status).toBe("stored");
    expect(hostKinds(x.seen)).toEqual(BACKUP_READ);
    expect(src).toBe("backup");
  }, SLOW);

  it("FB3: the primary's listing fails every attempt -- the backup is listed and used; source backup", async () => {
    await reset();
    const x = await run({ primary: "fail", backup: "frame" }, T);
    const src = await frameSource(E0);
    console.log(`FB3: ${x.r?.status ?? x.threw}; ${JSON.stringify(hostKinds(x.seen))}; source ${src}; primaryListError ${x.r?.primaryListError ?? "none"}`);
    expect(x.threw).toBe("");
    expect(x.r?.status).toBe("stored");
    expect(hostKinds(x.seen)).toEqual(BACKUP_READ);
    expect(src).toBe("backup");
    expect(x.r?.primaryListError ?? "").toMatch(/^OPERA primary list /);   // N3: the primary's failure, named, in the result
  }, SLOW);

  it("FB4: both empty -- no-frame; both listed; no GET; nothing written", async () => {
    await reset();
    const x = await run({ primary: "empty", backup: "empty" }, T);
    console.log(`FB4: ${x.r?.status ?? x.threw}; ${JSON.stringify(hostKinds(x.seen))}; frames ${await frames()}`);
    expect(x.r?.status).toBe("no-frame");
    expect(hostKinds(x.seen)).toEqual(["backup:list", "primary:list"]);
    expect(await frames()).toBe(0);
  }, SLOW);

  // FB4b, FB4c (2a as amended: no-frame ONLY when every host answered empty). They assert the throw, its text, no GET
  // and nothing written -- not the host set -- so main, which never lists the backup, passes them too (regression rows).
  const noGet = (seen: Seen[]) => seen.filter((s) => s.kind !== "list").length;
  it("FB4b: both listings fail every attempt -- the check throws (the primary's OPERA list failure named); no GET; nothing written", async () => {
    await reset();
    const x = await run({ primary: "fail", backup: "fail" }, T);
    console.log(`FB4b: threw ${x.threw !== ""} (primary ${x.threw.includes("OPERA primary list") ? "named" : "NOT named"}, backup ${x.threw.includes("OPERA backup list") ? "named" : "NOT named"}); GETs ${noGet(x.seen)}; frames ${await frames()}`);
    expect(x.threw).toContain("OPERA primary list");   // N3: both hosts' texts
    expect(x.threw).toContain("OPERA backup list");
    expect(noGet(x.seen)).toBe(0);
    expect(await frames()).toBe(0);
    expect(await latest()).toBeNull();
  }, SLOW);
  it("FB4c: the primary fails every attempt, the backup lists empty -- throws the primary's OPERA list error; no GET; nothing written", async () => {
    await reset();
    const x = await run({ primary: "fail", backup: "empty" }, T);
    console.log(`FB4c: threw ${x.threw !== ""} (${x.threw.includes("OPERA primary list") ? "names OPERA primary list" : "does NOT name OPERA primary list"}); GETs ${noGet(x.seen)}; frames ${await frames()}`);
    expect(x.threw).toContain("OPERA primary list");
    expect(noGet(x.seen)).toBe(0);
    expect(await frames()).toBe(0);
    expect(await latest()).toBeNull();
  }, SLOW);
  // FB4d (B1, the review): production's state now -- the primary lists EMPTY and the backup's listing FAILS. That is
  // a throw (the backup's error), never a no-frame: an unreachable backup must not read as "OPERA has no frame".
  it("FB4d: primary empty, the backup's listing fails every attempt -- throws the backup's error; no GET; nothing written", async () => {
    await reset();
    const x = await run({ primary: "empty", backup: "fail" }, T);
    console.log(`FB4d: ${x.r?.status ?? "threw"} (an OPERA error: ${x.threw.includes("OPERA") ? "yes" : "no"}); GETs ${noGet(x.seen)}; frames ${await frames()}`);
    // ONE property (the reviewer): it throws -- any OPERA error. Which host the text names is ERR1's row, not this one.
    expect(x.threw).toContain("OPERA");
    expect(noGet(x.seen)).toBe(0);
    expect(await frames()).toBe(0);
    expect(await latest()).toBeNull();
  }, SLOW);

  it("FB5: primary non-empty but its reads fail -- the check throws; NO backup request; no frame; latest untouched", async () => {
    await reset();
    const x = await run({ primary: "frame", backup: "frame", primaryReads: "fail" }, T);
    const toBackup = x.seen.filter((s) => s.host === "backup").length;
    console.log(`FB5: threw ${x.threw !== ""}; ${JSON.stringify(hostKinds(x.seen))}; backup requests ${toBackup}; frames ${await frames()}`);
    expect(x.threw).not.toBe("");
    expect(toBackup).toBe(0);
    expect(hostKinds(x.seen)).toEqual(["primary:head", "primary:list"]);
    expect(await frames()).toBe(0);
    expect(await latest()).toBeNull();
  }, SLOW);
});

describe("FB6: latest carries the stored frame's source", () => {
  it("FB6: a fresh backup frame gives latest source backup; a frame stored without it gives a latest with no source", async () => {
    await reset();
    await run({ primary: "empty", backup: "frame" }, T);
    const a = await latest();
    // the frame as one stored before this change: the same bytes, its metadata without `source`
    const o = await env.WX!.get(frameKey(opukFx, E0));
    const md = { ...(o?.customMetadata ?? {}) };
    delete md.source;
    if (o) await env.WX!.put(frameKey(opukFx, E0), await o.arrayBuffer(), { customMetadata: md });
    await env.WX!.delete(latestKey(opukFx));
    const y = await run({ primary: "empty", backup: "frame" }, T);
    const b = await latest();
    console.log(`FB6: fresh ${a?.source}; old frame: ${y.r?.status ?? y.threw}, latest ${b ? ("source" in b ? `source ${b.source}` : "no source") : "absent"}`);
    expect(a?.source).toBe("backup");
    expect(y.r?.status).toBe("already-stored");
    expect(b).not.toBeNull();
    expect(b && "source" in b).toBe(false);
  }, SLOW);
});

describe("FB7: the primary's return (unit level)", () => {
  const [E1, E2, E3, E4] = [E0, E0 + 300, E0 + 600, E0 + 900];
  const T7 = (E4 + 60) * 1000;
  const backupThree = async () => {
    await reset();
    for (let i = 0; i < 3; i++) await run({ primary: "empty", backup: "frame", backupKeys: [keyAt(E1), keyAt(E2), keyAt(E3)] }, T7);
  };
  it("FB7: backup E1-E3, then the primary lists E2-E4: E4 from the primary, E2/E3 never rewritten, latest E3 -> E4 primary", async () => {
    await backupThree();
    const before = await Promise.all([E2, E3].map(async (e) => (await env.WX!.head(frameKey(opukFx, e)))?.etag));
    const l0 = await latest();
    const x = await run({ primary: "frame", backup: "frame", primaryKeys: [keyAt(E2), keyAt(E3), keyAt(E4)] }, T7);
    const after = await Promise.all([E2, E3].map(async (e) => (await env.WX!.head(frameKey(opukFx, e)))?.etag));
    const l1 = await latest();
    const srcs = await Promise.all([E1, E2, E3, E4].map(frameSource));
    console.log(`FB7: latest ${l0?.epoch === E3 ? "E3" : l0?.epoch} ${l0?.source} -> ${l1?.epoch === E4 ? "E4" : l1?.epoch} ${l1?.source}; `
      + `${x.r?.status ?? x.threw} ${JSON.stringify(hostKinds(x.seen))}; sources ${JSON.stringify(srcs)}; E2/E3 kept ${JSON.stringify(before) === JSON.stringify(after)}`);
    expect([l0?.epoch, l0?.source]).toEqual([E3, "backup"]);
    expect(x.r?.status).toBe("stored");
    expect(hostKinds(x.seen)).toEqual(PRIMARY_READ);
    expect(after).toEqual(before);
    expect(srcs).toEqual(["backup", "backup", "backup", "primary"]);
    expect([l1?.epoch, l1?.source]).toEqual([E4, "primary"]);
  }, SLOW);
  it("FB7 (lagging): the primary lists only E1-E2 -- already-stored, kept-newer, latest stays E3 backup", async () => {
    await backupThree();
    const x = await run({ primary: "frame", backup: "frame", primaryKeys: [keyAt(E1), keyAt(E2)] }, T7);
    const l = await latest();
    console.log(`FB7 lag: ${x.r?.status ?? x.threw} ${x.r?.latest}; latest ${l?.epoch === E3 ? "E3" : l?.epoch} ${l?.source}`);
    expect([x.r?.status, x.r?.latest]).toEqual(["already-stored", "kept-newer"]);
    expect([l?.epoch, l?.source]).toEqual([E3, "backup"]);
  }, SLOW);
});

describe("HZ1: /healthz shows each OPERA product's source", () => {
  it("HZ1: primary, backup, null with no frame, null for an old latest; no source key on a NOAA product", async () => {
    const [p1, p2, p3, p4] = ["opuk", "opis", "opib", "opmt"].map((id) => PRODUCTS.find((p) => p.id === id)!);
    const cref = PRODUCTS.find((p) => p.id === "cref")!;
    const now = Math.floor(Date.now() / 1000);
    const rec = (p: Product, extra: Record<string, unknown> = {}) => JSON.stringify({ product: p.id, epoch: now - 100, key: frameKey(p, now - 100),
      ingestedAt: Date.now(), ni: 10, nj: 10, la1: 0, lo1: 0, di: 0.01, dj: 0.01, tile: 64, ...extra });
    await env.WX!.put(latestKey(p1!), rec(p1!, { source: "backup" }));
    await env.WX!.put(latestKey(p2!), rec(p2!, { source: "primary" }));
    await env.WX!.delete(latestKey(p3!));
    await env.WX!.put(latestKey(p4!), rec(p4!));
    await env.WX!.put(latestKey(cref), rec(cref));
    const out = await wxHealth(env, [p1!, p2!, p3!, p4!, cref], new Set()) as Record<string, Record<string, unknown>>;
    const got = [out.opuk?.source, out.opis?.source, out.opib?.source, out.opmt?.source, "source" in (out.cref ?? {})];
    console.log(`HZ1: ${JSON.stringify(got)}`);
    expect(got).toEqual(["backup", "primary", null, null, false]);
    for (const p of [p1!, p2!, p4!, cref]) await env.WX!.delete(latestKey(p));
  });
});

describe("ERR1: every OPERA error names its host (N1, section 9)", () => {
  // Four paths, one message each. FB4d's scenario is deliberately NOT used, so S-EMPTYFAIL turns exactly {FB4d}.
  it("ERR1: primary list, backup list, primary get and backup get failures each name their host", async () => {
    await reset(); const pl = (await run({ primary: "fail", backup: "empty" }, T)).threw;                          // FB4c
    await reset(); const bl = (await run({ primary: "fail", backup: "fail" }, T)).threw;                           // FB4b
    await reset(); const pg = (await run({ primary: "frame", backup: "frame", primaryReads: "fail" }, T)).threw;   // FB5
    await reset(); const bg = (await run({ primary: "empty", backup: "frame", backupReads: "fail" }, T)).threw;
    const got = [/^Error: OPERA primary list /.test(pl) && pl.includes("the backup listed empty"),
                 bl.includes("OPERA backup list"), /^Error: OPERA primary get /.test(pg), /^Error: OPERA backup get /.test(bg)];
    console.log(`ERR1: primary list ${got[0]}, backup list ${got[1]}, primary get ${got[2]}, backup get ${got[3]}`);
    expect(got).toEqual([true, true, true, true]);
  }, SLOW);
});

describe("UA: one User-Agent on both hosts (section 2b)", () => {
  it("UA1: every request in every scenario's checks (FB1-FB7, FB4b-d, ERR1's), on either host, carries exactly OPERA_UA", async () => {
    const seen: Seen[] = [];
    const E = [E0, E0 + 300, E0 + 600, E0 + 900];
    const T7 = (E[3]! + 60) * 1000;
    const plans: [Plan, number][] = [
      [{ primary: "frame", backup: "frame" }, T], [{ primary: "empty", backup: "frame" }, T], [{ primary: "fail", backup: "frame" }, T],
      [{ primary: "empty", backup: "empty" }, T], [{ primary: "fail", backup: "fail" }, T], [{ primary: "fail", backup: "empty" }, T],
      [{ primary: "empty", backup: "fail" }, T], [{ primary: "frame", backup: "frame", primaryReads: "fail" }, T],
      [{ primary: "empty", backup: "frame", backupReads: "fail" }, T],
      [{ primary: "empty", backup: "frame", backupKeys: E.slice(0, 3).map(keyAt) }, T7],
      [{ primary: "frame", backup: "frame", primaryKeys: E.slice(1).map(keyAt) }, T7],
    ];
    for (const [plan, now] of plans) {
      await reset();
      seen.push(...(await run(plan, now)).seen);
    }
    const ours = seen.filter((s) => s.host !== "other");
    const missing = ours.filter((s) => s.ua !== OPERA_UA).length;
    console.log(`UA1: ${ours.length} requests, ${missing} without OPERA_UA`);
    expect(ours.length).toBeGreaterThan(0);
    expect(missing).toBe(0);
  }, SLOW);
  it("UA2: OPERA_UA names the contact address", () => {
    expect(OPERA_UA).toContain("support@valarsystems.com");
  });
  it("UA3: the one transcribed copy (tools/wx/opera_fixture.py) equals OPERA_UA byte for byte", () => {
    const m = /^UA = \{"User-Agent": "([^"]+)"\}$/m.exec(fixturePy);
    console.log(`UA3: the copy ${m ? "found" : "NOT FOUND"}; equal ${m?.[1] === OPERA_UA}`);
    expect(m).not.toBeNull();
    expect(m?.[1]).toBe(OPERA_UA);
  });
});
