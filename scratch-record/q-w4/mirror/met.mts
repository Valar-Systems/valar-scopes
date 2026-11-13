// SCRATCH, read-only: which of the 16 points' squares MEET a region naming one of the 8 refused census files (build.ts's RD-P6
// test, with the code PR's own squareBox / makeFrame / boxMeets / parseQuery), on bm-obj's index.json. Nothing fetched.
//   (from wt-main/proxy) node --import tsx ../../mirror/met.mts
import { readFileSync } from "node:fs";
const T = new URL("../wt-code/proxy/src/basemap/", import.meta.url);
const B = await import(new URL("build.ts", T).href), FR = await import(new URL("frame.ts", T).href);
const S = await import(new URL("store.ts", T).href), Q = await import(new URL("query.ts", T).href);
const AIR: Record<string, [string, string]> = {
  KBOS: ["42.36", "-71.01"], KJFK: ["40.64", "-73.78"], KLGA: ["40.78", "-73.87"], KEWR: ["40.69", "-74.17"], MIDTOWN: ["40.75", "-73.99"], KMIA: ["25.79", "-80.29"], KSFO: ["37.62", "-122.38"],
  KSEA: ["47.45", "-122.31"], KORD: ["41.98", "-87.90"], KDTW: ["42.21", "-83.35"], KMSP: ["44.88", "-93.22"], KIND: ["39.72", "-86.29"], KJAC: ["43.61", "-110.74"],
  BUFFALO: ["42.886", "-78.879"], ROUSES: ["44.994", "-73.365"], ZAPATA: ["26.91", "-99.27"],
};
const QUERY = "r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242";
const EIGHT = readFileSync(new URL("../seastore/logs/2-fetch.err", import.meta.url), "utf8").split("\n").filter((l) => l.startsWith("FAILED ")).map((l) => l.split(" ")[1]!.replace(/:$/, ""));
if (EIGHT.length !== 8) throw new Error(`expected 8 refused urls, got ${EIGHT.length}`);
const idx = JSON.parse(readFileSync(new URL("../bm-obj/index.json", import.meta.url), "utf8")) as { regions: Array<{ box: number[]; files: string[] }> };
const named = new Set(idx.regions.flatMap((r) => r.files));
console.log(`regions ${idx.regions.length}; the 8 named by a region: ${EIGHT.filter((u) => named.has(u)).length}`);
// CONTROL: a point placed inside each county's own region box must meet it (the test can say yes)
for (const u of EIGHT) {
  const r = idx.regions.find((x) => x.files.includes(u))!;
  console.log(`  control ${u.split("/").pop()}: region box [${r.box.map((v) => v.toFixed(3)).join(", ")}]`);
}
for (const [ap, [lat, lon]] of Object.entries(AIR)) {
  const p = Q.parseQuery(new URL(`https://x/api/v1/blipscope/basemap?lat=${lat}&lon=${lon}&${QUERY}`));
  if (!p.ok) throw new Error(`${ap}: ${p.code}`);
  const rMm = p.q.rKm * 1_000_000, f1 = FR.makeFrame(p.q.lat0Micro, p.q.lon0Micro, rMm, p.q.epsMm), box = B.squareBox(f1, rMm);
  const met = new Set(idx.regions.filter((r) => S.boxMeets(r.box, box)).flatMap((r) => r.files));
  const hit = EIGHT.filter((u) => met.has(u)).map((u) => u.split("/").pop());
  console.log(`${ap}: square [${box.map((v: number) => v.toFixed(3)).join(", ")}]; files met ${met.size}; of the 8: ${hit.length ? hit.join(" ") : "none"}`);
}
