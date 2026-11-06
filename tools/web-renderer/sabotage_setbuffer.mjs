// R7's sabotage (docs/wx-device.md, AB2): the shim's setBuffer with its abort REMOVED must turn smoke's R7 red.
// A safety net that has never fired is unproven; this makes it fire, and makes its absence fail.
//
//   node tools/web-renderer/sabotage_setbuffer.mjs <the real build's dist/s3-128>
//
// In a COPY of tools/web-renderer (tools/.web-renderer-sabotage, the same depth, so build.sh resolves this repo),
// setBuffer's message and abort are replaced by createSprite's body. The anchor is asserted to occur once, and the
// copy asserted to differ. s3-128 is built from the copy into a temporary dist, and smoke.mjs --r7-only is run on it:
// it must exit 1 with R7 its only red. The control is the same run on the real build, which must exit 0. The real
// shim's sha256 is compared before and after, and the copy and the temporary dist are removed.
// Exit 0 as predicted; 1 not; 2 the rig (a build or an instrument that could not run).
import { spawnSync } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");
const SHIM = path.join(HERE, "shim", "LovyanGFX.hpp");
const SETBUFFER = /\[\[noreturn\]\] void setBuffer\(void\*, int32_t, int32_t, uint8_t = 0\)\r?\n\s*\{[\s\S]*?std::abort\(\);\r?\n\s*\}/g;
const PLANTED = "void setBuffer(void*, int32_t w, int32_t h, uint8_t = 0) { w_ = w; h_ = h; created_ = true; }   // SABOTAGE: the abort removed";

// The plant, on the shim's text: exactly one setBuffer with its abort, replaced; anything else is the rig's.
export function plant(text) {
  const n = (text.match(SETBUFFER) || []).length;
  if (n !== 1) throw new Error(`setBuffer's abort found ${n} times, need exactly 1`);
  const out = text.replace(SETBUFFER, PLANTED);
  if (out === text) throw new Error("the copy is identical to the original: the plant did not apply");
  return out;
}

function sha(p) { return crypto.createHash("sha256").update(fs.readFileSync(p)).digest("hex"); }

function smokeR7(dist) {
  const r = spawnSync(process.execPath, [path.join(HERE, "smoke.mjs"), dist, "--r7-only"], { encoding: "utf8" });
  const out = (r.stdout || "") + (r.stderr || "");
  const red = [...out.matchAll(/^\s*FAIL\s+(\S+)/gm)].map((m) => m[1]);
  return { status: r.status, red, out };
}

function main(realDist) {
  const before = sha(SHIM);
  const copy = path.join(ROOT, "tools", ".web-renderer-sabotage");
  const dist = fs.mkdtempSync(path.join(os.tmpdir(), "setbuffer-sabotage-"));
  let bad = 0;
  try {
    fs.rmSync(copy, { recursive: true, force: true });
    fs.cpSync(HERE, copy, { recursive: true,
      filter: (src) => !/^(\.obj|dist|node_modules)(\/|\\|$)/.test(path.relative(HERE, src)) });
    const shim = path.join(copy, "shim", "LovyanGFX.hpp");
    const text = fs.readFileSync(shim, "utf8");
    fs.writeFileSync(shim, plant(text));
    if (fs.readFileSync(shim, "utf8") === text) { console.log("RIG: the planted copy reads back unchanged"); return 2; }
    console.log("plant: setBuffer's abort replaced by createSprite's body, in the copy only");

    const b = spawnSync("bash", [path.join(copy, "build.sh"), "s3-128", dist], { stdio: "inherit", env: process.env });
    if (b.status !== 0) { console.log(`RIG: the planted copy did not build (exit ${b.status})`); return 2; }

    const cases = [["sabotaged: the abort removed", path.join(dist, "s3-128"), 1, ["R7"]],
                   ["control: the real build", realDist, 0, []]];
    for (const [label, d, wantStatus, wantRed] of cases) {
      const r = smokeR7(d);
      for (const l of r.out.trimEnd().split("\n")) console.log(`    | ${l}`);
      if (r.status === 2 || r.status === null) { console.log(`RIG: smoke --r7-only could not run on ${label} (exit ${r.status})`); return 2; }
      const ok = r.status === wantStatus && JSON.stringify(r.red) === JSON.stringify(wantRed);
      bad += ok ? 0 : 1;
      console.log(`  ${ok ? "ok  " : "FAIL"} ${label}: exit ${r.status}, red ${JSON.stringify(r.red)} ` +
                  `(want exit ${wantStatus}, red ${JSON.stringify(wantRed)})`);
    }
  } finally {
    fs.rmSync(copy, { recursive: true, force: true });
    fs.rmSync(dist, { recursive: true, force: true });
  }
  const after = sha(SHIM);
  console.log(`the real shim unchanged: ${before === after} (${after.slice(0, 12)})`);
  if (before !== after) return 2;
  console.log(bad ? `SETBUFFER SABOTAGE: ${bad} NOT AS PREDICTED` : "SETBUFFER SABOTAGE AS PREDICTED");
  return bad ? 1 : 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const realDist = process.argv[2];
  if (!realDist) { console.log("usage: node tools/web-renderer/sabotage_setbuffer.mjs <the real build's dist/s3-128>"); process.exit(2); }
  try {
    process.exit(main(path.resolve(realDist)));
  } catch (e) {
    console.log("RIG: " + (e && e.message || e));
    process.exit(2);
  }
}
