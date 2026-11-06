// Blipscope web renderer: the per-pixel weather layer stays OFF on the website (docs/wx-device.md, (A); ruled
// 2026-11-04 ~22:25Z). The shim's LGFX_Sprite has no pixels -- getBuffer() returns the sprite itself as a token --
// and the firmware's weather layer reads and writes raw buffers through that pointer. So the web config must never
// turn "wx-layer" on while getBuffer() is that token.
//
//   node tools/web-renderer/check_wx_off.mjs [<web-renderer dir>]   exit 0 off; 1 turned on; 2 cannot tell
//   node tools/web-renderer/check_wx_off.mjs --selftest              its control and its plant, each in a copy
//
// Read, not guessed: any getBuffer() other than the token below, or a web config whose settings map cannot be
// found, is refused (exit 2). "Off" is exactly one map entry {"wx-layer", "false"}. An ABSENT entry is ON: since v19 the
// firmware reads an unset key as on (include/WxLayerSwitch.h), so the website must say "false" in writing, and any
// other value is not "off" in a way this check can vouch for.
//
// NOT covered here, and not mistaken for coverage: a page can pass "wx-layer" at runtime (web_config_set), and the
// website's page code lives in another repo. That path is the shim's setBuffer, which aborts with a message.
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const STAND_IN = "void* getBuffer() const { return created_ ? (void*)this : nullptr; }";

export function check(dir) {
  let shim, config;
  try {
    shim = fs.readFileSync(path.join(dir, "shim", "LovyanGFX.hpp"), "utf8");
    config = fs.readFileSync(path.join(dir, "web", "web_config.cpp"), "utf8");
  } catch (e) {
    return { code: 2, why: `cannot read the sources: ${e.message}` };
  }
  // Code only: a comment that names getBuffer() is not a definition.
  const getBuffers = shim.split(/\r?\n/).map((l) => l.replace(/\/\/.*$/, "").trim()).filter((l) => /\bgetBuffer\s*\(/.test(l));
  if (getBuffers.length !== 1 || getBuffers[0] !== STAND_IN)
    return { code: 2, why: `the shim's getBuffer() is not the form this check reads (${JSON.stringify(getBuffers)}): re-derive` };
  const map = config.match(/static std::map<std::string, std::string> m = \{([\s\S]*?)\n\s*\};/);
  if (!map) return { code: 2, why: "the web config's settings map was not found: re-derive" };
  const entries = [...map[1].matchAll(/\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}/g)].map((m) => [m[1], m[2]]);
  const wx = entries.filter(([k]) => k === "wx-layer");
  if (wx.length === 0)
    return { code: 1, why: "the web config has no wx-layer entry, and the firmware reads an absent key as ON " +
                           "while getBuffer() is a stand-in: the per-pixel layer cannot run on the website" };
  const on = wx.filter(([, v]) => v !== "false");
  if (on.length)
    return { code: 1, why: `the web config turns wx-layer on (${on.map(([, v]) => JSON.stringify(v)).join(", ")}) ` +
                           "while getBuffer() is a stand-in: the per-pixel layer cannot run on the website" };
  return { code: 0, why: `wx-layer "false" in the web config's map (${entries.length} entries); ` +
                         "getBuffer() is the stand-in" };
}

function sha(p) { return crypto.createHash("sha256").update(fs.readFileSync(p)).digest("hex"); }

function selftest() {
  const files = [["shim", "LovyanGFX.hpp"], ["web", "web_config.cpp"]];
  const before = files.map((f) => sha(path.join(HERE, ...f)));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "check-wx-off-"));
  let bad = 0;
  try {
    const copy = (name) => {
      const d = path.join(tmp, name);
      for (const f of files) { fs.mkdirSync(path.join(d, f[0]), { recursive: true }); fs.copyFileSync(path.join(HERE, ...f), path.join(d, ...f)); }
      return d;
    };
    const cases = [];
    cases.push(["control: an unplanted copy", copy("control"), 0]);
    const planted = copy("plant");
    const cfg = path.join(planted, "web", "web_config.cpp");
    const src = fs.readFileSync(cfg, "utf8");
    const anchor = '{"mqtt", "false"},';
    if (src.split(anchor).length !== 2) { console.log(`RIG: the plant's anchor occurs ${src.split(anchor).length - 1} times, need 1`); return 2; }
    const out = src.replace(anchor, `${anchor} {"wx-layer", "true"},   // PLANT`);
    fs.writeFileSync(cfg, out);
    if (fs.readFileSync(cfg, "utf8") === src) { console.log("RIG: the plant did not apply"); return 2; }
    cases.push(['plant: {"wx-layer", "true"} in a copy of the web config', planted, 1]);
    // The entry removed: since v19 the firmware reads an absent key as ON, so absence must not pass.
    const absent = copy("absent");
    const cfgA = path.join(absent, "web", "web_config.cpp");
    const srcA = fs.readFileSync(cfgA, "utf8");
    const entry = '{"wx-layer", "false"},';
    if (srcA.split(entry).length !== 2) { console.log(`RIG: the plant's anchor occurs ${srcA.split(entry).length - 1} times, need 1`); return 2; }
    fs.writeFileSync(cfgA, srcA.replace(entry, "/* PLANT: the entry removed */"));
    if (fs.readFileSync(cfgA, "utf8") === srcA) { console.log("RIG: the plant did not apply"); return 2; }
    cases.push(['plant: the wx-layer entry removed from a copy of the web config', absent, 1]);
    for (const [label, dir, want] of cases) {
      const r = check(dir);
      const ok = r.code === want;
      bad += ok ? 0 : 1;
      console.log(`  ${ok ? "ok  " : "FAIL"} ${label}: exit ${r.code} (want ${want}) -- ${r.why}`);
    }
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
  const after = files.map((f) => sha(path.join(HERE, ...f)));
  const same = before.every((s, i) => s === after[i]);
  console.log(`  the real files unchanged: ${same} (${after.map((s) => s.slice(0, 12)).join(", ")})`);
  if (!same) return 2;
  console.log(bad ? `CHECK WX OFF SELFTEST: ${bad} NOT AS PREDICTED` : "CHECK WX OFF SELFTEST AS PREDICTED");
  return bad ? 1 : 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args[0] === "--selftest") process.exit(selftest());
  const r = check(path.resolve(args[0] || HERE));
  console.log(`check_wx_off: ${r.code === 0 ? "OFF" : r.code === 1 ? "ON" : "CANNOT TELL"} -- ${r.why}`);
  process.exit(r.code);
}
