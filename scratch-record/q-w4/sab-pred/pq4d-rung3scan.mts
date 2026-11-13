// SCRATCH, read-only on pq4d: fixture A (and B) at every cap 640..811, the rung it fits at and its drops -- to find a cap where rung 3
// (secondaries dropped, shortest first) fits with only SOME secondaries dropped, the case S-ROADORDER's plant would change.
import { build, Q4 } from "./pq4d/src/basemap/build.ts";
import { decode } from "./pq4d/src/basemap/format.ts";
import { Q4P, Q4W } from "./pq4d/src/basemap/frame.ts";
import { Q4S } from "./pq4d/src/basemap/sea.ts";
import { COMMIT, mbStore } from "./pq4d/test/basemap-synth.ts";
if (!(Q4.ladder && Q4.d2 && Q4W.tp && Q4W.backstop && Q4P.prov && Q4S.on && !Q4S.plant)) { console.log("not the ruled settings"); process.exit(2); }
for (const which of ["A", "B"] as const) {
  const s = await mbStore(which);
  let prev = "";
  for (let cap = which === "A" ? 811 : 841; cap >= 600; cap--) {
    const r = await build(s.store, s.query, { commit: COMMIT, capBytes: cap });
    if (!r.ok) { console.log(`${which}@${cap}: ${r.status}`); break; }
    const prov = new TextDecoder().decode((decode(r.bytes).sections.find((x) => x.type === 7) as { text: Uint8Array }).text);
    const dw = /dropped\.water: (\d+)/.exec(prov)?.[1], dr = /dropped\.roads: (\d+)/.exec(prov)?.[1];
    const key = `rung ${r.ladder.rung} dropped ${dw} / ${dr}`;
    if (key !== prev) { console.log(`${which}@${cap}: ${key} (${r.bytes.length} B)`); prev = key; }
  }
}
