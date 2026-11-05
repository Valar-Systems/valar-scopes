// The opuk split (docs/wx-opuk-split.md): the cref Worker checks only cref, the opuk Worker only opuk (and nothing
// when stopped), and no two deploy targets across the two production tomls check the same product. Each test runs a
// deploy entry's REAL scheduled handler: scheduledTime 31 s in the past (no wait), a global fetch that answers 404,
// console.log captured. FT1, FT2a, FT2b, FX; their sabotages are proxy/scripts/sabotage-wx-split.py.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import tomls from "virtual:wx-ingest-tomls";
import * as opukEntry from "../src/wx/ingest-opuk-worker";
import * as crefEntry from "../src/wx/ingest-worker";
import { PRODUCTS } from "../src/wx/products";

type Entry = { default: { scheduled(c: ScheduledController, e: never): Promise<void> } };
/** Each toml `main` the tests know, to the module it names. An unknown main fails FX. */
const ENTRIES: Record<string, Entry> = {
  "src/wx/ingest-worker.ts": crefEntry as unknown as Entry,
  "src/wx/ingest-opuk-worker.ts": opukEntry as unknown as Entry,
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

interface Target { id: string; main: string | null; vars: Record<string, string> }
/** A toml's deploy targets -- the top level and every [env.*] -- with wrangler's rules: main inherits, vars do not. */
function targets(file: string, text: string): Target[] {
  const top: Target = { id: `${file}:(top level)`, main: null, vars: {} };
  const envs = new Map<string, Target>();
  let section = "";
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/#.*$/, "").trim();
    if (!line) continue;
    const head = /^\[\[?([^\]]+)\]\]?$/.exec(line);
    if (head) {
      section = head[1]!.trim();
      const e = /^env\.([^.]+)/.exec(section);
      if (e && !envs.has(e[1]!)) envs.set(e[1]!, { id: `${file}:${e[1]}`, main: null, vars: {} });
      continue;
    }
    const kv = /^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"/.exec(line);
    if (!kv) continue;
    const [, k, v] = kv as unknown as [string, string, string];
    const e = /^env\.([^.]+)(?:\.(.+))?$/.exec(section);
    if (section === "" && k === "main") top.main = v;
    else if (section === "vars") top.vars[k] = v;
    else if (e && !e[2] && k === "main") envs.get(e[1]!)!.main = v;
    else if (e && e[2] === "vars") envs.get(e[1]!)!.vars[k] = v;
  }
  for (const t of envs.values()) t.main ??= top.main;
  return [top, ...envs.values()];
}

describe("the opuk split: one product per Worker", () => {
  it("FT1 the cref Worker's entry checks cref, and only cref", async () => {
    const { lines } = await run(crefEntry as unknown as Entry, {});
    expect(productsOf(lines)).toEqual(["cref", "cref"]);
  });

  it("FT2a the opuk Worker's entry, WX_INGEST on, checks opuk, and only opuk", async () => {
    const { lines } = await run(opukEntry as unknown as Entry, { WX_INGEST: "on" });
    expect(productsOf(lines)).toEqual(["opuk", "opuk"]);
  });

  it("FT2b the opuk Worker's entry, WX_INGEST off or absent, logs its stop and touches nothing", async () => {
    for (const vars of [{ WX_INGEST: "off" }, {}] as Record<string, string>[]) {
      const { lines, touched } = await run(opukEntry as unknown as Entry, vars, true);
      expect(lines.map((l) => JSON.parse(l).evt)).toEqual(["wx_ingest_off", "wx_ingest_off"]);
      expect(touched).toBe(0);
    }
  });

  it("FX no two deploy targets across the production ingest tomls check the same product", async () => {
    const all = Object.entries(tomls).flatMap(([file, text]) => targets(file, text));
    // the parse found every target (a whole-list compare, so a missed [env.*] cannot pass quietly)
    expect(all.map((t) => t.id)).toEqual([
      "wx-ingest.wrangler.toml:(top level)",
      "wx-ingest-opuk.wrangler.toml:(top level)",
      "wx-ingest-opuk.wrangler.toml:inert",
    ]);
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
