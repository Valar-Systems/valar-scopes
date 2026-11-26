// The builder's CPU limit (S3, which rides D4). Wrangler inherits no `limits` into an env, so basemap.wrangler.toml sets
// cpu_ms in each of its three limits blocks, and staging measures what ships: the three must be EQUAL (L1). L2 pins the value;
// its basis is the comment above [limits] in that file. L3 is the control: exactly the three blocks are found, so a block that
// went missing cannot pass L1 by absence. Read from the file that deploys, never restated.
import { describe, expect, it } from "vitest";
import builderToml from "../basemap.wrangler.toml?raw";

type Block = { section: string; cpuMs: number | null };

/** Every `[...limits]` block of a wrangler toml, in file order, with its cpu_ms (NaN when not a plain integer). */
function limitsBlocks(text: string): Block[] {
  const out: Block[] = [];
  let cur: Block | null = null;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.replace(/#.*$/, "").trim();
    if (!line) continue;
    const head = /^\[(\[?)([^\]]+)\]\]?$/.exec(line);
    if (head) {
      const section = head[2]!.trim();
      cur = /(^|\.)limits$/.test(section) ? { section, cpuMs: null } : null;
      if (cur) out.push(cur);
      continue;
    }
    const kv = /^cpu_ms\s*=\s*(.+)$/.exec(line);
    if (kv && cur) cur.cpuMs = /^\d+$/.test(kv[1]!.trim()) ? Number(kv[1]!.trim()) : NaN;
  }
  return out;
}

describe("the builder's CPU limit (basemap.wrangler.toml)", () => {
  const blocks = limitsBlocks(builderToml);

  it("L3 CONTROL: exactly the three limits blocks are found", () => {
    expect(blocks.map((b) => b.section)).toEqual(["limits", "env.staging.limits", "env.production.limits"]);
  });

  it("L1 each block's cpu_ms is an integer, and the three are equal (staging measures what ships)", () => {
    for (const b of blocks) expect(Number.isInteger(b.cpuMs), `${b.section}: cpu_ms ${b.cpuMs}`).toBe(true);
    expect(new Set(blocks.map((b) => b.cpuMs)).size).toBe(1);
  });

  // One property per row: L2 reads the top-level [limits] block only, and L1 carries its value to the two envs.
  it("L2 the value is 68000 (3 x the labelled 22,655 ms reading, rounded up to 1,000; the basis is in the toml)", () => {
    expect(blocks.find((b) => b.section === "limits")?.cpuMs).toBe(68000);
  });
});
