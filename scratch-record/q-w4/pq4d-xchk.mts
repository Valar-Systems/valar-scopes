// SCRATCH, read-only: xisland's repair log line and xisland-tall's stop message, from the pinned prototype on the SEA-CI fixtures.
import { build, q4Out } from "./pq4d/src/basemap/build.ts";
import { lonLatOf, writeSynthStore } from "./pq4d/test/basemap-synth.ts";
import { SEACI, seaciDef, seaciQuery } from "./wt-code/proxy/test/basemap-seaci-fixtures.ts";
for (const n of ["xisland", "xisland-tall"]) {
  const fx = SEACI[n]!(), { store } = await writeSynthStore(seaciDef(fx, lonLatOf) as any);
  const r = await build(store, seaciQuery(fx), { commit: "0".repeat(40) });
  console.log(n, r.ok ? `200 repairLog ${JSON.stringify(q4Out.sea?.repairLog)}` : `${r.status} ${(r as any).code} why=${JSON.stringify((r as any).why)}`);
}
