# Blipscope web renderer

The radar firmware compiled to WebAssembly, for the website. The radar's own translation units run
unmodified, including its fetch and enrich tasks and its parsers. Every draw call is recorded as a vector
command and replayed on a `<canvas>` at the screen's resolution. The design, the stubs and the predictions are
in [docs/web-renderer.md](../../docs/web-renderer.md).

## Using a release

Each firmware release `vN` carries the web renderer for its published models. These are release assets:

| asset | what |
|---|---|
| `blipscope-web-s3-128-vN.wasm`, `blipscope-web-s3-128-vN.js` | the 1.28 in model and its loader |
| `blipscope-web-render-vN.js` | the page module: `mount()` |
| `blipscope-web-mono-vN.woff2`, `blipscope-web-fonts-LICENSE.txt` | the smooth-text font (JetBrains Mono, SIL OFL 1.1) |
| `blipscope-web-vN.sha256` | the set's checksums |

```js
import { mount } from "./blipscope-web-render-v19.js";
const radar = await mount(document.querySelector("canvas"), {
  loaderUrl: "./blipscope-web-s3-128-v19.js",
  wasmUrl: "./blipscope-web-s3-128-v19.wasm",
  fontUrl: "./blipscope-web-mono-v19.woff2",   // optional; enables textMode "smooth"
  config: { latitude: "44.06", longitude: "-121.32", radius: "40", "radius-unit": "km" },
  apiBase: "",                                   // "" = this page's origin
});
radar.setTextMode("smooth");                    // or "device" (the panel's glcd font, the default)
```

**What the page's origin must serve:**
- `GET /api/v1/blipscope/blips?lat=&lon=&r=&limit=40`, in the device's response shape.
- Optionally `GET /api/v1/blipscope/enrich/<hex>`. Without it, labels show no type or operator.

**What the renderer never does:**
- It requests nothing else from anywhere, including `/config`, `/airports` and photos.
- It sends no header.
- It posts nothing.
- It uses no eval (it is linked with `-sDYNAMIC_EXECUTION=0`), no storage of any kind and no cookies, and it has no
  inline script. Every file comes from a URL the page passes.

These are checked on the built files in CI by `check_site_rules.mjs`, which plants each pattern in a copy and must
catch it.

**Cadence, the firmware's own:**
- every 5 s for 10 minutes after load or a touch;
- then every 15 s;
- every 60 s during solar night at the configured location.

## Building

```sh
pio pkg install -e blipscope-s3-128                    # the firmware's libraries
tools/web-renderer/build.sh s3-128                     # dist/s3-128/blipscope-web-s3-128-v<N>.{js,wasm}, render, font
tools/web-renderer/build.sh s3-21                      # the 2.1 in (built; published when announced)
node tools/web-renderer/smoke.mjs tools/web-renderer/dist/s3-128      # the headless checks, R3-R6
node tools/web-renderer/check_site_rules.mjs tools/web-renderer/dist  # the site's content rules (--selftest too)
cd tools/web-renderer/dist && python -m http.server 8087               # the preview page (synthetic scene)
```

**Requirements:**
- Emscripten on `PATH`, or `EMSDK=~/emsdk`. Built with 6.0.13.
- `SRC_ROOT=<a checkout>` builds another tree's firmware, which is how a release tag is built.
- The preview page answers the firmware's fetches from `site/scene.js`, a synthetic scene. `?live=1` makes it
  fetch its own origin instead.

The release workflow is `.github/workflows/web-renderer.yml`. It is separate from the firmware's and writes only
`blipscope-web-*` assets.

## Outside the device build

PlatformIO compiles `src/` and `lib/`. Nothing here is on any env's path, so a device build with or without this
directory is the same image.
