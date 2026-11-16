// SCRATCH (map-server): M-3's expectation, taken from the OTHER side of the contract: the dry-clone's own sources.ts at #696's
// head (e81e794908a6), imported, so the checker grades the manifest against the code that wrote it, not against a transcription.
// Writes mirror/m3-26-expect.json: { note: TIGER2025_NOTE, urls: [TIGER2025_URLS, sorted] }. Prints counts only.
//   (from dry-clone/proxy) node --import tsx ../../mirror/m3_26_expect.mts
import { writeFileSync } from "node:fs";
import { TIGER2025_NOTE, TIGER2025_URLS } from "../dry-clone/proxy/scripts/basemap/sources.ts";

const urls = [...TIGER2025_URLS].sort();
writeFileSync(new URL("./m3-26-expect.json", import.meta.url), JSON.stringify({ note: TIGER2025_NOTE, urls }, null, 1) + "\n");
console.log(`expect: note ${TIGER2025_NOTE.length} chars; urls ${urls.length}`);
