// One CONUS frame: download, gunzip, decode, quantise to the 4-bit wire codes, pack two cells per byte.
// Shared by the Node meter (a4-node.ts) and the local-workerd meter (ingest-worker/). Web APIs only.
import { decodeMrms, codeOf } from "./mrms.ts";

export interface Ingested { packed: Uint8Array; ni: number; nj: number; la1: number; lo1: number; di: number; dj: number;
                            validTimeUtc: string; gzBytes: number }

export async function ingestFrame(url: string, holdMb = 0, onStage?: (name: string) => void): Promise<{ frame: Ingested; heldBytes: number }> {
  // Meter sabotage: hold an extra block for the whole run, touched so it is really resident.
  const hold = holdMb > 0 ? new Uint8Array(holdMb * 1024 * 1024) : null;
  if (hold) for (let i = 0; i < hold.length; i += 4096) hold[i] = 1;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`fetch ${res.status}`);
  const gz = new Uint8Array(await res.arrayBuffer());
  onStage?.("download");
  let packed: Uint8Array | null = null, ni = 0;
  const grid = await decodeMrms(gz, (row, vals) => {
    if (!packed) { ni = vals.length; packed = new Uint8Array((ni * 3500) >> 1); }
    const base = row * ni;
    for (let j = 0; j < ni; j++) {
      const idx = base + j, c = codeOf(vals[j]);
      if (idx & 1) packed![idx >> 1] |= c << 4; else packed![idx >> 1] = c;
    }
  }, onStage);
  onStage?.("packed");
  let touch = 0; if (hold) for (let i = 0; i < hold.length; i += 4096) touch += hold[i];
  return { frame: { packed: packed!, ni: grid.ni, nj: grid.nj, la1: grid.la1, lo1: grid.lo1, di: grid.di, dj: grid.dj,
                    validTimeUtc: grid.validTimeUtc, gzBytes: gz.length }, heldBytes: touch * 4096 };
}
