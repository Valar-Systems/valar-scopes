// The 1c redeploy's frame row (docs/wx-opera-map-r2.md section 7): is each stored opuk frame byte for byte what a fresh build
// gives? A frame's LENGTH is fixed by its tile layout, so a hollow frame has the right length; this compares the content.
//
// For each <epoch>.wxg in a folder (frames_get.py), it finds that frame's OPERA key, builds the frame here through
// buildOperaFrame with NO bucket, so the map comes from buildMap and never from R2, and compares every byte.
//
//   node <proxy>/node_modules/tsx/dist/cli.mjs frame_same.mts <proxy dir> <frames dir>          the check
//   ... frame_same.mts <proxy dir> <frames dir> --flip      the sabotage: one nibble of each local build flipped; every frame must differ
//
// Reads OPERA's public cache one request at a time, with K14's User-Agent (buildOperaFrame sets it).

import { createHash } from "node:crypto";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const [proxy, dir, flag] = process.argv.slice(2) as [string, string, string | undefined];
const src = (f: string) => pathToFileURL(join(proxy, "src", "wx", f)).href;
const { DEFAULT_OPTS, buildOperaFrame, epochOfKey, operaKeys } = await import(src("ingest.ts"));
const { PRODUCTS } = await import(src("products.ts"));
const { WXG1_HEADER_BYTES } = await import(src("grid.ts"));
const opuk = PRODUCTS.find((p: { id: string }) => p.id === "opuk");
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex").slice(0, 12);

const epochs = readdirSync(dir).filter((f) => f.endsWith(".wxg")).map((f) => Number(f.replace(".wxg", ""))).sort((a, b) => a - b);
let same = 0, differ = 0, unread = 0;
const rows: string[] = [];
for (const epoch of epochs) {
  const stored = new Uint8Array(readFileSync(join(dir, `${epoch}.wxg`)));
  const { keys } = await operaKeys(opuk, (epoch + 60) * 1000, fetch as never, DEFAULT_OPTS);
  const key = keys.find((k: string) => epochOfKey(opuk, k) === epoch);
  if (!key) { unread++; rows.push(`${epoch} NO SOURCE KEY`); continue; }
  const b = await buildOperaFrame(opuk, key, epoch, fetch as never, DEFAULT_OPTS, { get: 0 }, {}, undefined);
  const local: Uint8Array = b.fb.bytes;
  if (flag === "--flip") local[WXG1_HEADER_BYTES + 1000]! ^= 0x01;
  const eq = stored.length === local.length && stored.every((v, i) => v === local[i]);
  if (eq) same++; else differ++;
  let first = -1;
  if (!eq) for (let i = 0; i < Math.min(stored.length, local.length); i++) if (stored[i] !== local[i]) { first = i; break; }
  rows.push(`${epoch} ${new Date(epoch * 1000).toISOString().slice(11, 19)}Z stored ${sha(stored)} local ${sha(local)} ` +
            `${stored.length} B map ${b.map} ${eq ? "SAME" : `DIFFER at byte ${first}`}`);
}
for (const r of rows) console.log(r);
console.log(JSON.stringify({ frames: epochs.length, same, differ, unread, flip: flag === "--flip" }));
process.exit(differ || unread ? 1 : 0);
