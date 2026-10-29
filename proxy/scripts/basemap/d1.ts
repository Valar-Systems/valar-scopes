// D1: the store to R2 (docs/basemap-route.md; ruled: wrangler under the existing OAuth session, 8 at a time, then
// verify -> repair -> verify -> seal). The cycle itself is ingest-lib's publishStore, unchanged (MI7, S-ISIZE,
// S-ISEAL); this file supplies the object list and a bucket that runs wrangler concurrently behind publishStore's
// one-at-a-time interface.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { CURRENT_KEY, type Region, type StoreIndex, storeKeys } from "../../src/basemap/store";
import type { StoreBucket, StoreObject } from "./ingest-lib";

export const BUCKET = "blipscope-basemap";
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

/** The objects D1 writes, from a FULL dry run (never a PARTIAL one), and `current`'s body, written last. Every tile's
 *  bytes are checked against the sha256 its manifest entry pins, so what is uploaded is what `store:` names. */
export function assembleStore(cache: string, cachePath: (cache: string, url: string) => string): { storeId: string; objects: StoreObject[]; current: Uint8Array } {
  const dir = join(cache, "dry-run", "store");
  if (existsSync(join(dir, "manifest.PARTIAL.json")) && !existsSync(join(dir, "manifest.json"))) throw new Error("only a PARTIAL manifest: run the full dry run first");
  const manifestBytes = readFileSync(join(dir, "manifest.json"));
  const storeId = sha(manifestBytes).slice(0, 12), k = storeKeys(storeId);
  const manifest = JSON.parse(manifestBytes.toString("utf8")) as { files: Array<{ layer: string; url: string; sha256: string; bytes: number }> };
  const cellsIndex = JSON.parse(readFileSync(join(dir, "index.json"), "utf8")) as { cells: StoreIndex["cells"] };
  const regions = JSON.parse(readFileSync(join(dir, "regions.json"), "utf8")) as Region[];
  if (!Array.isArray(regions) || regions.some((r) => !Array.isArray(r.files))) throw new Error("regions.json is not in index.json's form: re-run `coverage`");
  const listed = new Set(manifest.files.map((f) => f.url));
  for (const r of regions) for (const u of r.files) if (!listed.has(u)) throw new Error(`a region names ${u}, which the manifest does not list`);
  const objects: StoreObject[] = [
    { key: k.manifest, bytes: manifestBytes },
    { key: k.index, bytes: new TextEncoder().encode(JSON.stringify({ cells: cellsIndex.cells, regions } satisfies StoreIndex) + "\n") },
    { key: k.coverage, bytes: readFileSync(join(dir, "coverage.bin")) },
    { key: k.terrainSources, bytes: readFileSync(join(dir, "terrain-sources.bin")) },
  ];
  for (const c of cellsIndex.cells) {
    const b = readFileSync(join(dir, "cells", ...c.key.split("/")) + ".bin");
    if (b.length !== c.bytes) throw new Error(`cell ${c.key} is ${b.length} B, its index says ${c.bytes}`);
    objects.push({ key: k.cell(c.key), bytes: b });
  }
  for (const f of manifest.files.filter((x) => x.layer === "terrain")) {
    const m = /\/terrarium\/9\/(\d+)\/(\d+)\.png$/.exec(f.url);
    if (!m) throw new Error(`a terrain file with no z9 tile in its URL: ${f.url}`);
    const b = readFileSync(cachePath(cache, f.url));
    if (b.length !== f.bytes || sha(b) !== f.sha256) throw new Error(`tile ${m[1]}/${m[2]} does not match its manifest entry`);
    objects.push({ key: k.tile(Number(m[1]), Number(m[2])), bytes: b });
  }
  return { storeId, objects, current: new TextEncoder().encode(JSON.stringify({ storeId })) };
}

/** One wrangler call: stdout as bytes; a non-zero exit is an error carrying stderr. Never a shell. */
function wrangler(proxyDir: string, args: string[], stdin?: Uint8Array): Promise<Uint8Array> {
  return new Promise((resolve, reject) => {
    const p = spawn(process.execPath, [join(proxyDir, "node_modules", "wrangler", "bin", "wrangler.js"), ...args], { cwd: proxyDir, stdio: ["pipe", "pipe", "pipe"] });
    const out: Buffer[] = [], err: Buffer[] = [];
    p.stdout.on("data", (d: Buffer) => out.push(d));
    p.stderr.on("data", (d: Buffer) => err.push(d));
    p.on("error", reject);
    p.on("close", (code) => (code === 0 ? resolve(new Uint8Array(Buffer.concat(out))) : reject(new Error(`wrangler ${args.slice(0, 3).join(" ")} exit ${code}: ${Buffer.concat(err).toString("utf8").slice(-600)}`))));
    if (stdin) p.stdin.end(Buffer.from(stdin)); else p.stdin.end();
  });
}

/**
 * A StoreBucket over wrangler, `concurrency` calls at a time, behind publishStore's sequential awaits:
 * - put() queues and returns as soon as the queue has room; a failed put is left for verify to find and repair;
 * - get() first waits for every queued put, then reads ahead in PUT order (verifyStore reads in that order) through a
 *   window of `window` objects, so at most that many sit in memory. A put after a read discards the read-ahead.
 * A missing object reads as null; any other wrangler failure is retried once, then thrown, so publishStore stops
 * unsealed rather than treating an outage as a store full of missing objects.
 */
export class WranglerBucket implements StoreBucket {
  private inflight = new Set<Promise<void>>();
  private order: string[] = [];
  private seen = new Set<string>();
  private ahead = new Map<string, Promise<Uint8Array | null>>();
  private next = 0;
  puts = 0; gets = 0; putFailures: string[] = [];
  constructor(private proxyDir: string, private bucket = BUCKET, private concurrency = 8, private window = 16) {}

  async put(key: string, bytes: Uint8Array): Promise<void> {
    this.ahead.clear();
    while (this.inflight.size >= this.concurrency) await Promise.race(this.inflight);
    if (!this.seen.has(key)) { this.seen.add(key); this.order.push(key); }
    this.puts++;
    const p: Promise<void> = wrangler(this.proxyDir, ["r2", "object", "put", `${this.bucket}/${key}`, "--pipe", "--remote"], bytes)
      .then(() => undefined, (e: Error) => { this.putFailures.push(`${key}: ${e.message.slice(0, 200)}`); })
      .finally(() => { this.inflight.delete(p); });
    this.inflight.add(p);
  }

  private fetchOne(key: string): Promise<Uint8Array | null> {
    this.gets++;
    const once = () => wrangler(this.proxyDir, ["r2", "object", "get", `${this.bucket}/${key}`, "--pipe", "--remote"]);
    return once().catch((e: Error) => {
      if (/does not exist|NoSuchKey|not found|404/i.test(e.message)) return null;
      return once().catch((e2: Error) => { if (/does not exist|NoSuchKey|not found|404/i.test(e2.message)) return null; throw e2; });
    });
  }

  async get(key: string): Promise<Uint8Array | null> {
    while (this.inflight.size) await Promise.all(this.inflight);
    if (!this.ahead.has(key)) { this.ahead.clear(); const i = this.order.indexOf(key); this.next = i < 0 ? this.order.length : i; if (i < 0) return this.fetchOne(key); }
    while (this.ahead.size < this.window && this.next < this.order.length) {
      const k = this.order[this.next++]!, p = this.fetchOne(k);
      p.catch(() => undefined); // a read-ahead discarded before it settles must not crash the run; an awaited one still throws
      this.ahead.set(k, p);
    }
    const r = this.ahead.get(key)!;
    this.ahead.delete(key);
    return r;
  }
}
