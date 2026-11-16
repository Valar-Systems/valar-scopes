// The split, one product per Worker (docs/wx-opuk-split.md; one Worker per OPERA product since the code stage,
// docs/wx-opera-code-stage.md §1d): the cref Worker checks only cref, each OPERA env only its PRODUCT, and nothing when it
// is stopped or names no OPERA product; and no two deploy targets across the production tomls check the same product. Each
// test runs a deploy entry's REAL scheduled handler: scheduledTime 31 s in the past (no wait), a global fetch that answers
// 404, console.log captured. FT1, FT2a, FT2b, FT2c, FX; their sabotages are proxy/scripts/sabotage-wx-split.py.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import tomls from "virtual:wx-ingest-tomls";
import * as mrmsEntry from "../src/wx/ingest-mrms-worker";
import * as operaEntry from "../src/wx/ingest-opera-worker";
import * as crefEntry from "../src/wx/ingest-worker";
import { PRODUCTS } from "../src/wx/products";

type Entry = { default: { scheduled(c: ScheduledController, e: never): Promise<void> } };
/** Each toml `main` the tests know, to the module it names. An unknown main fails FX. */
const ENTRIES: Record<string, Entry> = {
  "src/wx/ingest-worker.ts": crefEntry as unknown as Entry,
  "src/wx/ingest-opera-worker.ts": operaEntry as unknown as Entry,
  "src/wx/ingest-mrms-worker.ts": mrmsEntry as unknown as Entry,   // W4: an MRMS product per Worker (shsr)
};

/** Run one entry's scheduled handler; return the lines it logged and how often bucket or fetch were touched. */
async function run(entry: Entry, vars: Record<string, string>, guarded = false) {
  const lines: string[] = [];
  let touched = 0;
  const wx = guarded
    ? (new Proxy({}, { get() { touched++; throw new Error("the bucket was touched"); } }) as R2Bucket)
    : env.WX!;
  const realLog = console.log, realFetch = globalThis.fetch;
  console.log = (...a: unknown[]) => { lines.push(String(a[0])); };
  globalThis.fetch = (async () => {
    if (guarded) { touched++; throw new Error("fetch was called"); }
    return new Response("", { status: 404 });
  }) as typeof fetch;
  try {
    const ctl = { scheduledTime: Date.now() - 31_000, cron: "* * * * *", noRetry() {} } as unknown as ScheduledController;
    await entry.default.scheduled(ctl, { WX: wx, ...vars } as never);
  } finally {
    console.log = realLog;
    globalThis.fetch = realFetch;
  }
  return { lines, touched };
}
const productsOf = (lines: string[]) => lines.map((l) => { try { return JSON.parse(l).product; } catch { return undefined; } })
  .filter((p): p is string => typeof p === "string");

interface Target {
  id: string; main: string | null; name: string | null; crons: string[] | null; cpuMs: number | null;
  buckets: string[]; vars: Record<string, string>;
}
/** A toml's deploy targets -- the top level and every [env.*] -- with wrangler's rules as this repo uses them: main,
 *  name, triggers and limits inherit; vars and bindings do not. */
function targets(file: string, text: string): Target[] {
  const blank = (id: string): Target => ({ id, main: null, name: null, crons: null, cpuMs: null, buckets: [], vars: {} });
  const top = blank(`${file}:(top level)`);
  const envs = new Map<string, Target>();
  let section = "", bucket: { binding?: string; bucket?: string } | null = null;
  const flush = (t: Target) => { if (bucket) t.buckets.push(`${bucket.binding}->${bucket.bucket}`); bucket = null; };
  let cur = top;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/#.*$/, "").trim();
    if (!line) continue;
    const head = /^\[(\[?)([^\]]+)\]\]?$/.exec(line);
    if (head) {
      flush(cur);
      section = head[2]!.trim();
      const e = /^env\.([^.]+)/.exec(section);
      if (e && !envs.has(e[1]!)) envs.set(e[1]!, blank(`${file}:${e[1]}`));
      cur = e ? envs.get(e[1]!)! : top;
      const sub = e ? section.slice(e[0].length + 1) : section;
      if (head[1] === "[" && sub === "r2_buckets") bucket = {};
      section = sub;
      continue;
    }
    const kv = /^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$/.exec(line);
    if (!kv) continue;
    const k = kv[1]!, v = kv[2]!.trim();
    const str = /^"([^"]*)"$/.exec(v)?.[1];
    if (section === "" && k === "main") cur.main = str ?? null;
    else if (section === "" && k === "name") cur.name = str ?? null;
    else if (section === "triggers" && k === "crons") cur.crons = [...v.matchAll(/"([^"]*)"/g)].map((m) => m[1]!);
    else if (section === "limits" && k === "cpu_ms") cur.cpuMs = Number(v);
    else if (section === "vars" && str !== undefined) cur.vars[k] = str;
    else if (section === "r2_buckets" && bucket && str !== undefined) {
      if (k === "binding") bucket.binding = str; else if (k === "bucket_name") bucket.bucket = str;
    }
  }
  flush(cur);
  for (const t of envs.values()) {
    t.main ??= top.main; t.crons ??= top.crons; t.cpuMs ??= top.cpuMs;
    t.name ??= `${top.name}-${t.id.split(":")[1]}`;
  }
  return [top, ...envs.values()];
}

const OPERA_IDS = PRODUCTS.filter((p) => p.source === "opera").map((p) => p.id);

describe("one product per Worker", () => {
  it("FT1 the cref Worker's entry checks cref, and only cref", async () => {
    const { lines } = await run(crefEntry as unknown as Entry, {});
    expect(productsOf(lines)).toEqual(["cref", "cref"]);
  });

  it("FT2a the OPERA entry, WX_INGEST on, checks its PRODUCT and only it, for each of the 10", async () => {
    const got: Record<string, string[]> = {};
    for (const id of OPERA_IDS) got[id] = productsOf((await run(operaEntry as unknown as Entry, { PRODUCT: id, WX_INGEST: "on" })).lines);
    expect(got).toEqual(Object.fromEntries(OPERA_IDS.map((id) => [id, [id, id]])));
  });

  it("FT2b the OPERA entry, WX_INGEST off or absent, logs its stop and touches nothing", async () => {
    for (const vars of [{ PRODUCT: "opuk", WX_INGEST: "off" }, { PRODUCT: "opuk" }, {}] as Record<string, string>[]) {
      const { lines, touched } = await run(operaEntry as unknown as Entry, vars, true);
      expect(lines.map((l) => JSON.parse(l).evt)).toEqual(["wx_ingest_off", "wx_ingest_off"]);
      expect(touched).toBe(0);
    }
  });

  it("FT2c the OPERA entry, WX_INGEST on, with a PRODUCT that names no OPERA product, logs an error and touches nothing", async () => {
    for (const vars of [{ PRODUCT: "nope", WX_INGEST: "on" }, { PRODUCT: "cref", WX_INGEST: "on" }, { WX_INGEST: "on" }] as Record<string, string>[]) {
      const { lines, touched } = await run(operaEntry as unknown as Entry, vars, true);
      expect(lines.map((l) => JSON.parse(l).evt), JSON.stringify(vars)).toEqual(["wx_ingest_error", "wx_ingest_error"]);
      expect(touched).toBe(0);
    }
  });

  it("FX every deploy target, as the tomls' text: whole lists of what each deploys and what it checks", async () => {
    const all = Object.entries(tomls).flatMap(([file, text]) => targets(file, text));
    const O = "wx-ingest-opera.wrangler.toml";
    const env_ = (id: string, on: boolean) => ({ id: `${O}:${on ? id : `${id}-inert`}`, name: `blipscope-wx-ingest-${id}`,
      crons: ["* * * * *"], cpuMs: 12000, buckets: ["WX->blipscope-wx"], vars: { PRODUCT: id, WX_INGEST: on ? "on" : "off" } });
    // the parse found every target, with what it deploys (a whole-list compare, so a missed [env.*] cannot pass quietly)
    expect(all.map(({ main: _m, ...t }) => t)).toEqual([
      { id: "wx-ingest.wrangler.toml:(top level)", name: "blipscope-wx-ingest", crons: ["* * * * *"], cpuMs: 12000,
        buckets: ["WX->blipscope-wx"], vars: {} },
      // W4 (docs/weatherscope-stage4.md S4.12.3): shsr's Worker, on and inert, in cref's toml
      { id: "wx-ingest.wrangler.toml:shsr", name: "blipscope-wx-ingest-shsr", crons: ["* * * * *"], cpuMs: 12000,
        buckets: ["WX->blipscope-wx"], vars: { PRODUCT: "shsr", WX_INGEST: "on" } },
      { id: "wx-ingest.wrangler.toml:shsr-inert", name: "blipscope-wx-ingest-shsr", crons: ["* * * * *"], cpuMs: 12000,
        buckets: ["WX->blipscope-wx"], vars: { PRODUCT: "shsr", WX_INGEST: "off" } },
      { id: `${O}:(top level)`, name: "blipscope-wx-ingest-opera", crons: [], cpuMs: 12000, buckets: ["WX->blipscope-wx"],
        vars: { WX_INGEST: "off" } },
      ...OPERA_IDS.flatMap((id) => [env_(id, true), env_(id, false)]),
    ]);
    expect(all).toHaveLength(24);   // W4: + shsr and shsr-inert
    const checkedBy: Record<string, string[]> = {};
    for (const t of all) {
      const entry = ENTRIES[t.main ?? ""];
      expect(entry, `${t.id}: no known entry for main ${t.main}`).toBeDefined();
      for (const p of new Set(productsOf((await run(entry!, t.vars)).lines))) (checkedBy[p] ??= []).push(t.id);
    }
    // CONTROL: every product is checked somewhere, or this test saw nothing and proves nothing (BLIND)
    expect(Object.keys(checkedBy).sort()).toEqual(PRODUCTS.map((p) => p.id).sort());
    for (const [p, ids] of Object.entries(checkedBy)) expect(ids, `${p} is checked by ${ids.join(", ")}`).toHaveLength(1);
  });
});
