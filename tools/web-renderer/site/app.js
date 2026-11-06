// Blipscope web renderer: the preview page. Reads the build's manifest, mounts one model with the versioned
// render module, and answers the firmware's fetches from the synthetic scene -- a preview has no Worker.
// ?live=1 lets the firmware fetch this page's own origin instead (what the website does).
import { makeScene } from "./scene.js";

const $ = (id) => document.getElementById(id);
const canvas = $("radar");
const live = new URLSearchParams(location.search).get("live") === "1";
const CONFIG = { latitude: "44.0582", longitude: "-121.3153", radius: "40", "radius-unit": "km", "tz-offset": "-8" };
let manifest = null, render = null, radar = null, sku = "s3-128", textMode = "device", dim = false;

async function select(next) {
  sku = manifest.models[next] ? next : "s3-128";
  for (const s of Object.keys(manifest.models)) {
    const b = $("sku-" + s);
    if (b) b.setAttribute("aria-pressed", String(s === sku));
  }
  if (radar) radar.destroy();
  $("status").textContent = `Loading the ${sku} firmware…`;
  try {
    const m = manifest.models[sku];
    radar = await render.mount(canvas, {
      loaderUrl: m.loader, wasmUrl: m.wasm, fontUrl: manifest.font, config: CONFIG,
      fetch: live ? undefined : makeScene().fetch, textMode, dim,
    });
    const n = radar.module._web_screen_size();
    $("status").textContent = `v${manifest.version} · ${sku} · ${n} × ${n} panel · ${live ? "same-origin data" : "synthetic scene"}`;
    window.blipscopeDemo = radar;   // for tests: pause(), step(), stats()
  } catch (e) {
    $("status").textContent = "The firmware did not load: " + e.message;
  }
}

function setText(mode) {
  textMode = mode;
  $("text-device").setAttribute("aria-pressed", String(mode === "device"));
  $("text-smooth").setAttribute("aria-pressed", String(mode === "smooth"));
  if (radar) radar.setTextMode(mode);
}

$("text-device").addEventListener("click", () => setText("device"));
$("text-smooth").addEventListener("click", () => setText("smooth"));
$("sku-s3-128").addEventListener("click", () => select("s3-128"));
$("sku-s3-21").addEventListener("click", () => select("s3-21"));
$("dim").addEventListener("change", (e) => { dim = e.target.checked; if (radar) radar.setDim(dim); });
$("showstats").addEventListener("change", (e) => { $("stats").hidden = !e.target.checked; });

setInterval(() => {
  if (!radar) return;
  const s = radar.stats();
  $("st-ops").textContent = s.ops.toLocaleString();
  $("st-text").textContent = s.textBytes.toLocaleString();
  $("st-px").textContent = s.pixelDraws.toLocaleString();
  $("st-img").textContent = s.rasterImages.toLocaleString();
  $("st-net").textContent = `${s.net.blips} blips · ${s.net.enrich} enrich`;
  $("st-ms").textContent = `${s.frameMs.toFixed(1)} ms firmware + draw`;
}, 500);

manifest = await (await fetch("web-manifest.json", { cache: "no-store" })).json();
render = await import("./" + manifest.render);
if (!manifest.models["s3-21"]) $("sku-s3-21").disabled = true;
await select("s3-128");
