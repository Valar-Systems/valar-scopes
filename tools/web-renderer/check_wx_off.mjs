// Blipscope web renderer: the per-pixel weather layer stays OFF on the website (docs/wx-device.md, (A); ruled
// 2026-11-04 ~22:25Z). The shim's LGFX_Sprite has no pixels -- getBuffer() returns the sprite itself as a token --
// and the firmware's weather layer reads and writes raw buffers through that pointer. So the web config must never
// turn "wx-layer" on while getBuffer() is that token.
//
// Since the card's photo (_handoffs/p4/web-photos/REG-web-photos.txt, C3 and AMENDMENT 3): a sprite that receives
// drawJpg holds real pixels, and getBuffer() returns them; every other sprite still returns the token. Only the
// card's photo sprite ever receives drawJpg, so every sprite the weather layer reads or writes is still the token, and
// setBuffer still aborts: the rule above is unchanged. The line below is still compared whole.
//
// Since the weather layer's plane (_handoffs/p4/web-weather/REG-web-weather.txt, D1 and AMENDMENT 3, A3.4): setBuffer
// wraps its caller's buffer, and once it has run the frame holds a real plane. So "no pixels" is no longer why the
// layer is off. What keeps it off is parity: weather reaches the website only in a set attached after the release that
// ships it, by its own re-attach registration (D5). Until then the rule is the same as before: "wx-layer" is "false"
// in writing. It reads the getBuffer() line whole, and refuses any other, #799's included.
//
// D5 (REG-web-weather, A9.3): the premise "no pixels" is RETIRED by name. In its place, a second rule, PAIRING: the map
// may say "true" only where the shim carries the plane -- (a) getBuffer() is the line below, (b) setBuffer wraps its
// caller's buffer (web/web_photo.cpp), and (c) handOut creates the frame's plane. #799's getBuffer() means no plane.
// It is the rule weather's re-attach will lean on when it flips the map; until then PARITY above keeps it "false".
//
// The radar's map (REG-web-terrain, TD5, TD6, A1.3) gets the same two rules for its own web switch, "map-layer": PARITY
// refuses it on, AND refuses it removed (the web build reads an absent key as off, but the ruling is the writing);
// PAIRING refuses it "true" unless the tree carries (a) and (c) above, (d) lgfx::ArmFramePlane's arming body in
// web_photo.cpp, and (e) its one call in src/AircraftManager.cpp, read at the repo's depth from this directory.
//
//   node tools/web-renderer/check_wx_off.mjs [<web-renderer dir>]             PARITY: exit 0 off; 1 on; 2 cannot tell
//   node tools/web-renderer/check_wx_off.mjs --pairing [<web-renderer dir>]   PAIRING: exit 0 paired or off; 1 on
//                                                                             without a plane; 2 cannot tell
//   node tools/web-renderer/check_wx_off.mjs --selftest              its controls and its plants, each in a copy
//
// Read, not guessed: any getBuffer() other than the token below, or a web config whose settings map cannot be
// found, is refused (exit 2). "Off" is exactly one map entry {"wx-layer", "false"}. An ABSENT entry is ON: since v19 the
// firmware reads an unset key as on (include/WxLayerSwitch.h), so the website must say "false" in writing, and any
// other value is not "off" in a way this check can vouch for.
//
// NOT covered here, and not mistaken for coverage: a page can pass "wx-layer" at runtime (web_config_set), and the
// website's page code lives in another repo. Since D1 that path RUNS the layer rather than aborting, so keeping it off
// on a public page is that page's, under the parity rule.
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const STAND_IN = "void* getBuffer() const { return ext_ ? (void*)ext_ : handOut(); }";
// The form before the weather plane (#799's, the photo's): a shim still carrying it must not read as off.
const PHOTO_ONLY = "void* getBuffer() const { return !px_.empty() ? (void*)px_.data() : created_ ? (void*)this : nullptr; }";

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
    return { code: 1, why: "the web config has no wx-layer entry, and the firmware reads an absent key as ON: " +
                           "weather on the website waits for its re-attach registration (REG-web-weather, D5)" };
  const on = wx.filter(([, v]) => v !== "false");
  if (on.length)
    return { code: 1, why: `the web config turns wx-layer on (${on.map(([, v]) => JSON.stringify(v)).join(", ")}): ` +
                           "weather on the website waits for its re-attach registration (REG-web-weather, D5)" };
  // The map's own web switch (REG-web-terrain, TD6, A1.3): "false" in writing, ruled; its removal is refused as wx-layer's
  // is, though the web build reads an absent key as off -- the rule is the writing, not the default.
  const mp = entries.filter(([k]) => k === "map-layer");
  if (mp.length !== 1 || mp[0][1] !== "false")
    return { code: 1, why: `the web config's map-layer is ${mp.length ? JSON.stringify(mp.map(([, v]) => v).join(",")) : "absent"}, ` +
                           "not one \"false\": the map on the website waits for terrain's re-attach (REG-web-terrain, TD6)" };
  return { code: 0, why: `wx-layer and map-layer "false" in the web config's map (${entries.length} entries); ` +
                         "getBuffer() is the form this check reads" };
}

// PAIRING's lines, each compared whole (trimmed, comments stripped), each required exactly once.
const WRAPS = "ext_ = static_cast<uint16_t*>(buffer);";        // (b): setBuffer makes the sprite its caller's buffer
const PLANES = "self->plane_ = true;";                          // (c): handOut makes the frame's plane
const ARMS = "void lgfx::ArmFramePlane() { g_armed = true; }";   // (d): the map's arming path (REG-web-terrain TD2)
const HOOK = "lgfx::ArmFramePlane();";                          // (e): its one call, in src/AircraftManager.cpp

export function pairing(dir) {
  let shim, config, photo, firmware;
  try {
    shim = fs.readFileSync(path.join(dir, "shim", "LovyanGFX.hpp"), "utf8");
    config = fs.readFileSync(path.join(dir, "web", "web_config.cpp"), "utf8");
    photo = fs.readFileSync(path.join(dir, "web", "web_photo.cpp"), "utf8");
    firmware = fs.readFileSync(path.join(dir, "..", "..", "src", "AircraftManager.cpp"), "utf8");
  } catch (e) {
    return { code: 2, why: `cannot read the sources: ${e.message}` };
  }
  const map = config.match(/static std::map<std::string, std::string> m = \{([\s\S]*?)\n\s*\};/);
  if (!map) return { code: 2, why: "the web config's settings map was not found: re-derive" };
  const valuesOf = (key) => [...map[1].matchAll(/\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\}/g)].filter((m) => m[1] === key).map((m) => m[2]);
  const wx = valuesOf("wx-layer"), mp = valuesOf("map-layer");
  const wxOff = wx.length === 1 && wx[0] === "false";
  const mapOff = mp.length === 0 || (mp.length === 1 && mp[0] === "false");   // the web reads an absent map-layer as off
  if (wxOff && mapOff) return { code: 0, why: "wx-layer and map-layer off: nothing to pair" };
  const said = [!wxOff ? (wx.length ? `wx-layer ${JSON.stringify(wx.join(","))}` : "no wx-layer entry (the firmware reads it as ON)") : "",
                !mapOff ? `map-layer ${JSON.stringify(mp.join(","))}` : ""].filter(Boolean).join(" and ");
  const getBuffers = shim.split(/\r?\n/).map((l) => l.replace(/\/\/.*$/, "").trim()).filter((l) => /\bgetBuffer\s*\(/.test(l));
  if (getBuffers.length === 1 && getBuffers[0] === PHOTO_ONLY)
    return { code: 1, why: `${said}, and getBuffer() is #799's form: the shim has no plane` };
  if (getBuffers.length !== 1 || getBuffers[0] !== STAND_IN)
    return { code: 2, why: `the shim's getBuffer() is not a form this check reads (${JSON.stringify(getBuffers)}): re-derive` };
  const linesOf = (s) => s.split(/\r?\n/).map((l) => l.replace(/\/\/.*$/, "").trim());
  const photoLines = linesOf(photo), firmwareLines = linesOf(firmware);
  const count = (lines, s) => lines.filter((l) => l === s).length;
  const need = [[photoLines, PLANES, "(c) handOut planes"]];
  if (!wxOff) need.push([photoLines, WRAPS, "(b) setBuffer wraps"]);
  if (!mapOff) need.push([photoLines, ARMS, "(d) ArmFramePlane arms"], [firmwareLines, HOOK, "(e) the firmware's ArmFramePlane call"]);
  const missing = need.filter(([lines, s]) => count(lines, s) !== 1).map(([, , n]) => n);
  if (missing.length) return { code: 1, why: `${said}, but the tree lacks ${missing.join(" and ")}: no plane for it` };
  return { code: 0, why: `${said}, paired with the plane: (a) getBuffer, ${need.map(([, , n]) => n).join(", ")}` };
}

function sha(p) { return crypto.createHash("sha256").update(fs.readFileSync(p)).digest("hex"); }

function selftest() {
  // The firmware file is read by PAIRING's (e), at the repo's depth from this directory, so each copy keeps that depth.
  const files = [["shim", "LovyanGFX.hpp"], ["web", "web_config.cpp"], ["web", "web_photo.cpp"],
                 ["..", "..", "src", "AircraftManager.cpp"]];
  const before = files.map((f) => sha(path.join(HERE, ...f)));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "check-wx-off-"));
  let bad = 0;
  try {
    const copy = (name) => {
      const d = path.join(tmp, name, "tools", "web-renderer");
      for (const f of files) {
        const dst = path.join(d, ...f);
        fs.mkdirSync(path.dirname(dst), { recursive: true });
        fs.copyFileSync(path.join(HERE, ...f), dst);
      }
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
    // AMENDMENT 3 (photos), and REG-web-weather A3.4: the check binds to the exact getBuffer() line -- any other form,
    // the one before the weather plane included, is CANNOT TELL, never read as off.
    const other = copy("getbuffer");
    const shimO = path.join(other, "shim", "LovyanGFX.hpp");
    const srcO = fs.readFileSync(shimO, "utf8");
    if (srcO.split(STAND_IN).length !== 2) { console.log(`RIG: the plant's anchor occurs ${srcO.split(STAND_IN).length - 1} times, need 1`); return 2; }
    fs.writeFileSync(shimO, srcO.replace(STAND_IN, `${PHOTO_ONLY}   // PLANT`));
    if (fs.readFileSync(shimO, "utf8") === srcO) { console.log("RIG: the plant did not apply"); return 2; }
    cases.push(["plant: the getBuffer() line replaced (the form before the weather plane) in a copy of the shim", other, 2]);

    // PAIRING (REG-web-weather A9.3). A plant edits one file of a copy, asserting its anchor occurs once and the file
    // changed; the map is planted "true" in each, since a "false" map pairs trivially.
    const edit = (dir, rel, from, to) => {
      const f = path.join(dir, ...rel);
      const s = fs.readFileSync(f, "utf8");
      if (s.split(from).length !== 2) throw new Error(`RIG: ${rel.join("/")}'s anchor occurs ${s.split(from).length - 1} times, need 1`);
      fs.writeFileSync(f, s.replace(from, to));
      if (fs.readFileSync(f, "utf8") === s) throw new Error(`RIG: ${rel.join("/")}'s plant did not apply`);
    };
    const mapTrue = (dir) => edit(dir, ["web", "web_config.cpp"], anchor, `${anchor} {"wx-layer", "true"},   // PLANT`);
    const pairCases = [];
    pairCases.push(["pairing control: the real copy (map \"false\")", copy("pair-off"), 0]);
    pairCases.push(["pairing CONTROL: map \"true\" with the real shim (parity says 1 for it, above)", planted, 0]);
    const p1 = copy("pair-getbuffer"); mapTrue(p1);
    edit(p1, ["shim", "LovyanGFX.hpp"], STAND_IN, `${PHOTO_ONLY}   // PLANT`);
    pairCases.push(["pairing plant: map \"true\" with #799's getBuffer()", p1, 1]);
    const p2 = copy("pair-wraps"); mapTrue(p2);
    edit(p2, ["web", "web_photo.cpp"], `    ${WRAPS}\n`, "    /* PLANT: the wrap removed */\n");
    pairCases.push(["pairing plant: map \"true\", (b) setBuffer's wrap removed", p2, 1]);
    const p3 = copy("pair-planes"); mapTrue(p3);
    edit(p3, ["web", "web_photo.cpp"], `        ${PLANES}\n`, "        /* PLANT: the plane removed */\n");
    pairCases.push(["pairing plant: map \"true\", (c) handOut's plane removed", p3, 1]);

    // THE MAP'S OWN SWITCH (REG-web-terrain, TD5, TD6, A1.3): PARITY refuses it on or removed; PAIRING needs, for it,
    // (a), (c), (d) the arming function and (e) the firmware's one call to it.
    const mapEntry = '{"map-layer", "false"},';
    const mlOn = copy("map-on");
    edit(mlOn, ["web", "web_config.cpp"], mapEntry, '{"map-layer", "true"},   // PLANT');
    cases.push(['plant: {"map-layer", "true"} in a copy of the web config', mlOn, 1]);
    const mlGone = copy("map-absent");
    edit(mlGone, ["web", "web_config.cpp"], mapEntry, "/* PLANT: the map-layer entry removed */");
    cases.push(["plant: the map-layer entry removed from a copy of the web config", mlGone, 1]);
    pairCases.push(["pairing CONTROL: map-layer \"true\" with the real tree (parity says 1 for it, above)", mlOn, 0]);
    const m1 = copy("map-arms"); edit(m1, ["web", "web_config.cpp"], mapEntry, '{"map-layer", "true"},   // PLANT');
    edit(m1, ["web", "web_photo.cpp"], ARMS, "/* PLANT: the arming function removed */");
    pairCases.push(["pairing plant: map-layer \"true\", (d) ArmFramePlane's body removed", m1, 1]);
    const m2 = copy("map-hook"); edit(m2, ["web", "web_config.cpp"], mapEntry, '{"map-layer", "true"},   // PLANT');
    edit(m2, ["..", "..", "src", "AircraftManager.cpp"], "        lgfx::ArmFramePlane();", "        /* PLANT: the hook removed */");
    pairCases.push(["pairing plant: map-layer \"true\", (e) the firmware's call removed", m2, 1]);
    const m3 = copy("map-getbuffer"); edit(m3, ["web", "web_config.cpp"], mapEntry, '{"map-layer", "true"},   // PLANT');
    edit(m3, ["shim", "LovyanGFX.hpp"], STAND_IN, `${PHOTO_ONLY}   // PLANT`);
    pairCases.push(["pairing plant: map-layer \"true\" with #799's getBuffer()", m3, 1]);

    for (const [label, dir, want] of cases) {
      const r = check(dir);
      const ok = r.code === want;
      bad += ok ? 0 : 1;
      console.log(`  ${ok ? "ok  " : "FAIL"} ${label}: exit ${r.code} (want ${want}) -- ${r.why}`);
    }
    for (const [label, dir, want] of pairCases) {
      const r = pairing(dir);
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
  if (args[0] === "--selftest") {
    let rc;
    try { rc = selftest(); } catch (e) { console.log(String(e && e.message || e)); rc = 2; }
    process.exit(rc);
  }
  if (args[0] === "--pairing") {
    const r = pairing(path.resolve(args[1] || HERE));
    console.log(`check_wx_off --pairing: ${r.code === 0 ? "PAIRED OR OFF" : r.code === 1 ? "ON WITHOUT A PLANE" : "CANNOT TELL"} -- ${r.why}`);
    process.exit(r.code);
  }
  const r = check(path.resolve(args[0] || HERE));
  console.log(`check_wx_off: ${r.code === 0 ? "OFF" : r.code === 1 ? "ON" : "CANNOT TELL"} -- ${r.why}`);
  process.exit(r.code);
}
