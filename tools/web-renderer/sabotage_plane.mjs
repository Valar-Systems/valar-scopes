// R7''s sabotage (_handoffs/p4/web-weather/REG-web-weather.txt, D6, A9.1): the weather plane's PRESENTATION removed must
// turn smoke's R7' red. A check that has never been seen to fail is unproven; this makes it fail, and makes its absence
// fail.
//
//   node tools/web-renderer/sabotage_plane.mjs <the real build's dist/s3-128>
//
// In a COPY of tools/web-renderer (tools/.web-renderer-sabotage, the same depth, so build.sh resolves this repo), the
// present's condition for the plane, `if (isPlane() && planeOut())` in web/web_gfx.cpp, is made false: the plane is still
// handed out and written into its own pixels, so nothing is corrupted, but no format-1 OP_RASTER reaches the frame.
// (Not the hand-out itself: with getBuffer() returning the token, DrawWeatherUnder's memcpy would write 115,200 B over
// the sprite object -- a crash, never a clean red. A9.1.) The anchor is asserted to occur once, and the copy asserted to
// differ. s3-128 is built from the copy into a temporary dist, and smoke.mjs --r7p-only is run on it: it must exit 1 with
// R7' its only red and nothing thrown. The control is the same run on the real build, which must exit 0. The real file's
// sha256 is compared before and after, and the copy and the temporary dist are removed.
// Exit 0 as predicted; 1 not; 2 the rig (a build or an instrument that could not run, or a crash).
import { spawnSync } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");
const GFX = path.join(HERE, "web", "web_gfx.cpp");
const ANCHOR = "    if (isPlane() && planeOut()) {";
const PLANTED = "    if (false && isPlane() && planeOut()) {   // SABOTAGE: the plane's presentation removed";

// The plant, on web_gfx.cpp's text: exactly one anchor, replaced; anything else is the rig's.
export function plant(text) {
  const n = text.split(ANCHOR).length - 1;
  if (n !== 1) throw new Error(`the anchor occurs ${n} times, need exactly 1`);
  const out = text.replace(ANCHOR, PLANTED);
  if (out === text) throw new Error("the copy is identical to the original: the plant did not apply");
  return out;
}

function sha(p) { return crypto.createHash("sha256").update(fs.readFileSync(p)).digest("hex"); }

function smokeR7p(dist) {
  const r = spawnSync(process.execPath, [path.join(HERE, "smoke.mjs"), dist, "--r7p-only"], { encoding: "utf8" });
  const out = (r.stdout || "") + (r.stderr || "");
  const red = [...out.matchAll(/^\s*FAIL\s+(\S+)/gm)].map((m) => m[1]);
  const thrown = !/thrown nothing/.test(out);
  return { status: r.status, red, thrown, out };
}

function main(realDist) {
  const before = sha(GFX);
  const copy = path.join(ROOT, "tools", ".web-renderer-sabotage");
  const dist = fs.mkdtempSync(path.join(os.tmpdir(), "plane-sabotage-"));
  let bad = 0;
  try {
    fs.rmSync(copy, { recursive: true, force: true });
    fs.cpSync(HERE, copy, { recursive: true,
      filter: (src) => !/^(\.obj|dist|node_modules)(\/|\\|$)/.test(path.relative(HERE, src)) });
    const gfx = path.join(copy, "web", "web_gfx.cpp");
    const text = fs.readFileSync(gfx, "utf8");
    fs.writeFileSync(gfx, plant(text));
    if (fs.readFileSync(gfx, "utf8") === text) { console.log("RIG: the planted copy reads back unchanged"); return 2; }
    console.log("plant: the plane's presentation removed (web_gfx.cpp), in the copy only");

    const b = spawnSync("bash", [path.join(copy, "build.sh"), "s3-128", dist], { stdio: "inherit", env: process.env });
    if (b.status !== 0) { console.log(`RIG: the planted copy did not build (exit ${b.status})`); return 2; }

    const cases = [["sabotaged: the presentation removed", path.join(dist, "s3-128"), 1, ["R7'"]],
                   ["control: the real build", realDist, 0, []]];
    for (const [label, d, wantStatus, wantRed] of cases) {
      const r = smokeR7p(d);
      for (const l of r.out.trimEnd().split("\n")) console.log(`    | ${l}`);
      if (r.status === 2 || r.status === null || r.thrown) {
        console.log(`RIG: smoke --r7p-only could not run cleanly on ${label} (exit ${r.status}, thrown ${r.thrown})`);
        return 2;
      }
      const ok = r.status === wantStatus && JSON.stringify(r.red) === JSON.stringify(wantRed);
      bad += ok ? 0 : 1;
      console.log(`  ${ok ? "ok  " : "FAIL"} ${label}: exit ${r.status}, red ${JSON.stringify(r.red)} ` +
                  `(want exit ${wantStatus}, red ${JSON.stringify(wantRed)})`);
    }
  } finally {
    fs.rmSync(copy, { recursive: true, force: true });
    fs.rmSync(dist, { recursive: true, force: true });
  }
  const after = sha(GFX);
  console.log(`the real web_gfx.cpp unchanged: ${before === after} (${after.slice(0, 12)})`);
  if (before !== after) return 2;
  console.log(bad ? `PLANE SABOTAGE: ${bad} NOT AS PREDICTED` : "PLANE SABOTAGE AS PREDICTED");
  return bad ? 1 : 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const realDist = process.argv[2];
  if (!realDist) { console.log("usage: node tools/web-renderer/sabotage_plane.mjs <the real build's dist/s3-128>"); process.exitCode = 2; }
  else {
    try {
      process.exitCode = main(path.resolve(realDist));
    } catch (e) {
      console.log("RIG: " + (e && e.message || e));
      process.exitCode = 2;
    }
  }
}
