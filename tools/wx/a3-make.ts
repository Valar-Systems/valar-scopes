// A3: cut the four placement crops (and their sabotaged twins) from a frame's decoded codes.
//   node --experimental-strip-types tools/wx/a3-make.ts <frame.codes> <frame.json (from decode-cli)> <a3-target.json> <outDir>
// Writes <outDir>/a3-<N|E|S|W>.wxc and a3-<dir>-sab.wxc (origin moved by one cell). Prints sizes only.
import { readFileSync, writeFileSync } from "node:fs";
import { encodeCrop, type CodeGrid } from "./crop.ts";

const [, , codesPath, metaPath, targetPath, outDir] = process.argv;
const meta = JSON.parse(readFileSync(metaPath, "utf8")).grid;
const t = JSON.parse(readFileSync(targetPath, "utf8"));
const codes = new Uint8Array(readFileSync(codesPath));
const g: CodeGrid = { codes, ni: meta.ni, nj: meta.nj, la1: meta.la1, lo1: meta.lo1, di: meta.di, dj: meta.dj,
                      frameEpoch: Math.floor(Date.parse(meta.validTimeUtc) / 1000) };
for (const d of ["N", "E", "S", "W"]) {
  const [lat, lon] = t.centres[d];
  const ok = encodeCrop(g, lat, lon, 80);
  const sab = encodeCrop(g, lat, lon, 80, undefined, 1);
  writeFileSync(`${outDir}/a3-${d}.wxc`, ok.bytes);
  writeFileSync(`${outDir}/a3-${d}-sab.wxc`, sab.bytes);
  console.log(`  ${d}: ${ok.bytes.length} B (coarsen ${ok.coarsen}), sabotaged twin ${sab.bytes.length} B`);
}
