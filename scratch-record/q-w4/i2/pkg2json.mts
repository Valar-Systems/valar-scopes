// I2 (docs/basemap-dense-code.md, registered at 2891a937): a package (.bmpk) to the JSON p2-render.py's kb5 mode reads -- every water
// ring, and the header's origin and radius. seaIdx is left empty: kb5 mode writes ALL water rings, and I2 compares the whole drawn water.
//   (from <clone>/proxy) node --import tsx <scratchpad>/i2/pkg2json.mts <clone proxy dir> <in.bmpk> <out.json>
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [proxyDir, inp, outp] = process.argv.slice(2) as [string, string, string];
const { decode, validate } = await import(pathToFileURL(resolve(proxyDir, "src/basemap/format.ts")).href);
const bytes = new Uint8Array(readFileSync(inp));
if (validate(bytes).status !== "kOk") throw new Error(`${inp}: not kOk`);
const pkg = decode(bytes);
const water = (pkg.sections.find((s: { type: number }) => s.type === 1)?.rings ?? []) as number[][][];
writeFileSync(outp, JSON.stringify({ water, seaIdx: [], lat0Micro: pkg.lat0Micro, lon0Micro: pkg.lon0Micro, radiusMm: pkg.radiusMm }));
console.log(JSON.stringify({ rings: water.length, lat0Micro: pkg.lat0Micro, lon0Micro: pkg.lon0Micro, radiusMm: pkg.radiusMm }));
