// MD2's workerd half (docs/basemap-route.md A1.7). It runs only under scripts/basemap-md2.ts, which hands it the Node
// list as the MD2_NODE_SHAS binding; anywhere else it is skipped, and the script counts a skip as BLIND, never a pass.
import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { build } from "../src/basemap/build";
import { COMMIT, me2Store } from "./basemap-synth";

const node = (env as unknown as { MD2_NODE_SHAS?: string }).MD2_NODE_SHAS ?? "";
const sha = async (b: Uint8Array) => [...new Uint8Array(await crypto.subtle.digest("SHA-256", b))].map((x) => x.toString(16).padStart(2, "0")).join("");

describe("MD2, the same builds in workerd and in Node", () => {
  it.skipIf(!node)("MD2: ME2's first 50 builds in workerd give the Node list's sha256s, in order", async () => {
    const mine: string[] = [];
    for (let i = 0; i < 50; i++) {
      const s = await me2Store(i);
      const r = await build(s.store, s.query, { commit: COMMIT });
      mine.push(r.ok ? await sha(r.bytes) : `${r.status}:${r.code}`);
      await new Promise((res) => setTimeout(res, 0));
    }
    const theirs = node.split(",");
    expect(theirs.length).toBe(50);
    expect(mine.map((x, i) => (x === theirs[i] ? "=" : `#${i}`)).filter((x) => x !== "=")).toEqual([]);
  }, 300_000);
});
