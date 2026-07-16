#include "ConfigurationWebServer.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "DeviceIdentity.h"
#include "OtaUpdater.h"
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
#include "AircraftInfoFields.h"   // radar-only; filtered out of the FEATURE_EAM/FEATURE_SPACE builds
#endif

#ifdef FEATURE_EAM
#include "eam/EamLogbook.h"        // serves the on-device logbook as a CSV/JSON download
// The EAM build's backend base URL default. Normally injected per-env (-DEAM_FEED_BASE=...);
// guarded so the file still compiles without it. The runtime value ("eam-base-url") overrides.
#ifndef EAM_FEED_BASE
#define EAM_FEED_BASE "https://eam.example.com"
#endif
#endif

#ifdef FEATURE_SPACE
// The Spacescope build's optional backend base URL default. Empty by default: the device talks
// directly to free public space APIs and bakes in no backend. The runtime value ("space-base-url")
// overrides it (Phase-3 valar-space-feed). Guarded so the file compiles without the flag.
#ifndef SPACE_FEED_BASE
#define SPACE_FEED_BASE ""
#endif

// User-toggleable Spacescope screens, in canonical rotation order. Drives the config-page on/off
// checkbox grid (one "scr-<id>" box each) and the save that rebuilds the "space-screens" CSV.
// Ids must match SpaceManager::idToScreen; "splash" is intentionally excluded (it's the internal
// cold-start card, not user-selectable). Keep in sync with SpaceManager's Screen list.
struct SpaceScreenDef { const char* id; const char* label; };
static const SpaceScreenDef SPACE_SCREEN_DEFS[] = {
    {"iss",       "ISS live tracker"},
    {"isspass",   "ISS visible pass"},
    {"launch",    "Rocket launch T-minus"},
    {"kp",        "Geomagnetic Kp index"},
    {"solarwind", "Solar wind"},
    {"scales",    "NOAA space-wx scales"},
    {"flare",     "Solar X-ray flare"},
    {"aurora",    "Aurora forecast (local)"},
    {"dsn",       "Deep Space Network"},
    {"deepspace", "Deep-space probes"},
    {"asteroid",  "Asteroid close approach"},
    {"humans",    "Humans in space"},
    {"moon",      "Moon phase"},
    {"starmap",   "Night-sky star map"},
    {"observing", "Tonight's observing window"},
    {"planets",   "Planets up now"},
    {"algol",     "Algol minima watch"},
    {"dso",       "Deep-sky target tonight"},
    {"orrery",    "Solar-system orrery"},
    {"jupiter",   "Jupiter's moons"},
    {"lunar",     "Lunar terminator & libration"},
    {"eclipse",   "Next eclipse"},
    {"meteor",    "Next meteor shower"},
    {"cosmic",    "Cosmic clocks"},
    {"logbook",   "Spotter's logbook"},
    {"clock",     "UTC clock"},
};
static const size_t SPACE_SCREEN_DEF_COUNT = sizeof(SPACE_SCREEN_DEFS) / sizeof(SPACE_SCREEN_DEFS[0]);
#endif


// ---- Shared config-page chrome (all editions) -------------------------------
// CONFIG_SHELL_CSS / CONFIG_SHELL_JS are spliced into every edition's CONFIG_HTML
// by C string-literal concatenation (only one edition's page compiles per build,
// so nothing is duplicated in flash). The CSS replaces the old Tailwind-CDN
// <script>, which compiled styles in the browser at every load: slow on phones,
// a flash of unstyled content, and a completely unstyled page with no internet --
// exactly the situation (first setup) where the config page matters most.
// HARD RULES for these blocks:
//  - No '%' characters anywhere: the whole page runs through the ESPAsyncWebServer
//    template engine, which owns '%' (see the favicon comment below). Widths come
//    from flex/grid stretch and rem units, never CSS percentages.
//  - Each page sets its palette BEFORE the CSS block via
//    <style>:root{--ink:..;--line:..;--dim:..;--btn:..}</style>
//    (--ink body text, --line borders/frames, --dim hint text, --btn save button).
#define CONFIG_SHELL_CSS \
    R"(<style>)" \
    R"(*{box-sizing:border-box})" \
    R"(body{margin:0;padding:1rem;background:#111827;color:var(--ink);font-family:ui-monospace,Menlo,Consolas,monospace;font-size:1rem;min-height:100vh})" \
    R"(a{color:var(--ink)})" \
    R"(.wrap{max-width:42rem;margin:0 auto;border:1px solid var(--line);padding:1rem})" \
    R"(legend{padding:0 .5rem})" \
    R"(form{display:flex;flex-direction:column;gap:1rem})" \
    R"(fieldset,details{border:1px solid var(--line);padding:.75rem;margin:0;min-width:0})" \
    R"(summary{cursor:pointer;-webkit-user-select:none;user-select:none})" \
    R"(details[open]>summary{margin-bottom:.75rem})" \
    R"(summary input[type=checkbox]{margin-left:.4rem;vertical-align:-.15rem})" \
    R"(.field{display:flex;flex-direction:column;gap:.4rem})" \
    R"(.field>span:first-child{flex:none})" \
    R"(.stack{display:flex;flex-direction:column;gap:.75rem})" \
    R"(.check{display:flex;align-items:center;gap:.5rem})" \
    R"(.row{display:flex;flex-direction:column;gap:1rem})" \
    R"(.grid2{display:grid;grid-template-columns:1fr;gap:.5rem .9rem})" \
    R"(.grid3,.grid4{display:grid;grid-template-columns:repeat(2,1fr);gap:.5rem .9rem})" \
    R"(input,select,textarea,button{font:inherit;color:var(--ink);background:#111827;border:1px solid var(--line);padding:.5rem .6rem;min-width:0})" \
    R"(input[type=checkbox]{width:1.05rem;height:1.05rem;padding:0;margin:0;accent-color:var(--btn);flex:none})" \
    R"(input[type=range]{border:none;padding:0;accent-color:var(--btn);flex:1})" \
    R"(:focus-visible{outline:2px solid var(--ink);outline-offset:1px})" \
    R"(.grow{flex:1})" \
    R"(.w4{width:4.5rem}.w6{width:6rem}.w8{width:8rem})" \
    R"(.mt{margin-top:.75rem})" \
    R"(.hint{display:block;font-size:.78rem;color:var(--dim);line-height:1.45})" \
    R"(.hint a{color:inherit})" \
    R"(.btn{background:var(--btn);color:#000;border:none;padding:.6rem 1.6rem;cursor:pointer})" \
    R"(.btn-line{background:transparent;border:1px solid var(--line);color:var(--ink);padding:.4rem .8rem;cursor:pointer;white-space:nowrap})" \
    R"(.btn-danger{background:transparent;color:#ef4444;border:1px solid #ef4444;padding:.4rem .9rem;font-size:.8rem;cursor:pointer})" \
    R"(.savebar{position:sticky;bottom:0;z-index:5;display:flex;align-items:center;gap:1rem;background:#111827;border-top:1px solid var(--line);padding:.7rem 0 .1rem})" \
    R"(#result{font-size:.8rem})" \
    R"(.status{display:flex;flex-wrap:wrap;gap:.3rem 1.2rem;font-size:.78rem;color:var(--dim);margin-bottom:1rem})" \
    R"(.foot{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:.8rem;font-size:.78rem;color:var(--dim);margin-top:1.1rem})" \
    R"(@media(min-width:640px){body{font-size:.875rem;padding:2.5rem 1rem}.wrap{padding:1.25rem}.field{flex-direction:row;align-items:center;gap:.5rem}.row{flex-direction:row}.row>*{flex:1}.grid2{grid-template-columns:repeat(2,1fr)}.grid3{grid-template-columns:repeat(3,1fr)}.grid4{grid-template-columns:repeat(4,1fr)}input,select,textarea{padding:.25rem .45rem}input[type=checkbox]{width:.95rem;height:.95rem}.btn{padding:.45rem 1.4rem}})" \
    R"(</style>)"

// Shared page behaviour: async save into the sticky bar, the Reset WiFi confirm,
// a live brightness readout, paste-a-"lat, lon"-pair splitting into both fields,
// and the collapsible <details> sections (clicking a summary's master checkbox
// toggles the feature without toggling the section; details.auto sections open
// themselves on load when they already hold configuration). Uses /* */ comments
// only -- the literals concatenate without newlines, so a // comment would eat
// the rest of the script.
#define CONFIG_SHELL_JS \
    R"(<script>)" \
    R"(document.getElementById('cfg').addEventListener('submit',function(e){e.preventDefault();var st=document.getElementById('result');st.textContent='saving...';fetch(this.action,{method:'POST',headers:{'X-Blipscope':'1'},body:new FormData(this)}).then(function(r){return r.text()}).then(function(t){st.textContent=t}).catch(function(){st.textContent='save failed - device unreachable'})});)" \
    R"(document.getElementById('resetwifi').addEventListener('click',function(){if(!confirm('Forget WiFi credentials and restart into setup mode? You will need to reconnect the device to a network.'))return;fetch('/reset-wifi',{method:'POST',headers:{'X-Blipscope':'1'}}).then(function(r){return r.text()}).then(function(t){document.getElementById('result').textContent=t})});)" \
    R"(var shBr=document.querySelector('input[name=brightness]'),shBv=document.getElementById('brival');)" \
    R"(if(shBr&&shBv){var shSync=function(){shBv.textContent=shBr.value};shBr.addEventListener('input',shSync);shSync()})" \
    R"(var shLa=document.querySelector('input[name=latitude]'),shLo=document.querySelector('input[name=longitude]');)" \
    R"(if(shLa&&shLo){shLa.addEventListener('paste',function(e){var t=(e.clipboardData||window.clipboardData).getData('text'),m=t.match(/(-?\d+(?:\.\d+)?)[,;\s]+(-?\d+(?:\.\d+)?)/);if(m){e.preventDefault();shLa.value=m[1];shLo.value=m[2]}})})" \
    R"(document.querySelectorAll('summary input').forEach(function(i){i.addEventListener('click',function(e){e.stopPropagation()})});)" \
    R"(document.querySelectorAll('details.auto').forEach(function(d){if(d.open)return;var m=d.querySelector('summary input[type=checkbox]');if(m){if(m.checked)d.open=true;return}var any=false;d.querySelectorAll('textarea,input[type=password],input[type=text],input:not([type])').forEach(function(i){var v=(i.value||'').trim();if(v&&!/^\*+$/.test(v))any=true});if(any)d.open=true});)" \
    R"(</script>)"

// HTML stored in flash
// %PLACEHOLDER% tokens are substituted at serve time by the template processor.
// The page is feature-specific: the radar build serves the radar settings form below; the
// FEATURE_EAM build serves the EAM monitor form; the FEATURE_SPACE build serves the Spacescope
// form. The ConfigurationWebServer shell (NVS namespace, mDNS, /reset-wifi, save flag) is shared.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope</title>
        <!-- inline SVG favicon (radar blip) so the tab is easy to spot; no extra flash asset / route needed.
             Colors use rgb() not #-hex on purpose: a "#" in a data URI must be percent-encoded, and any
             stray percent sign collides with this page's PLACEHOLDER template engine and shreds the whole
             form (write it as &#37; in visible text - and keep it out of comments too, like this one). -->
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(17,24,39)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(34,197,94)' stroke-width='1'/><circle cx='8' cy='8' r='1.7' fill='rgb(34,197,94)'/></svg>">
        <style>:root{--ink:#22c55e;--line:#22c55e;--dim:#15803d;--btn:#22c55e}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Blipscope</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION%</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Tip: paste a &ldquo;lat, lon&rdquo; pair (right-click a spot in Google Maps and copy it) into the latitude box and both fields fill in.</span>

                <label class="field">
                    <span>Radius:</span>
                    <input id="radius" name="radius" type="number" min="0.1" step="0.1" max="222" value='%RADIUS%' class="grow">
                    <select id="radius-unit" name="radius-unit">
                        <option value="km" %RADIUS_UNIT_KM%>km</option>
                        <option value="mi" %RADIUS_UNIT_MI%>mi</option>
                    </select>
                </label>

                <label class="field">
                    <span>Data source:</span>
                    <select id="data-source" name="data-source" class="grow">
)"
#ifdef FEATURE_CLOUD_FEED
// The cloud option leads and is the default; OpenSky is relabelled as the
// power-user BYO-credentials path it now is.
R"(                        <option value="cloud" %DATASRC_CLOUD%>Blipscope Cloud (recommended)</option>
                        <option value="opensky" %DATASRC_OPENSKY%>OpenSky Network (your own account)</option>
)"
#else
R"(                        <option value="opensky" %DATASRC_OPENSKY%>OpenSky Network (cloud)</option>
)"
#endif
R"(                        <option value="local" %DATASRC_LOCAL%>My own ADS-B receiver</option>
                    </select>
                </label>
)"
#ifdef FEATURE_CLOUD_FEED
// Cloud fields double as the data credit: the ODbL attribution shows whenever
// the adsb.lol-backed source is selected.
R"(
                <div id="cloud-fields" class="stack">
                    <label class="field">
                        <span>Cloud server:</span>
                        <input name="cloud-url" value='%CLOUD_URL%' placeholder="built-in default" class="grow">
                    </label>
                    <label class="field">
                        <span>Access key:</span>
                        <input name="cloud-key" type="password" autocomplete="off" value='%CLOUD_KEY%' placeholder="built-in default" class="grow">
                    </label>
                    <span class="hint">
                        Managed Blipscope feed &mdash; no account needed; leave both fields blank for the
                        built-in defaults. Aircraft data &copy; <a href="https://adsb.lol" target="_blank" rel="noopener">adsb.lol</a>
                        contributors, licensed under <a href="https://opendatacommons.org/licenses/odbl/1-0/" target="_blank" rel="noopener">ODbL 1.0</a>.
                    </span>
                </div>
)"
#endif
R"(
                <div id="opensky-fields" class="stack">
                    <label class="field">
                        <span>OpenSky API client ID:</span>
                        <input name="opensky-id" value='%OPENSKY_ID%' class="grow">
                    </label>
                    <label class="field">
                        <span>OpenSky API client secret:</span>
                        <input name="opensky-secret" type="password" autocomplete="off" value='%OPENSKY_SECRET%' class="grow">
                    </label>
                </div>

                <div id="local-fields" class="stack">
                    <label class="field">
                        <span>Receiver URL:</span>
                        <input name="local-url" value='%LOCAL_URL%' placeholder="http://192.168.1.50/data/aircraft.json" class="grow">
                    </label>
                    <span class="hint">
                        dump1090-fa / readsb / PiAware / tar1090. Enter the device's IP (e.g. 192.168.1.50)
                        or the full aircraft.json URL. No API limits &mdash; the radar updates once a second.
                    </span>
                </div>

                <fieldset>
                    <legend>Display</legend>
                    <div class="grid3">
                        <label class="check"><input name="scanline" type="checkbox" %SCANLINE%><span>Radar sweep</span></label>
                        <label class="check"><input name="fade" type="checkbox" %FADE%><span>Sweep fade</span></label>
                        <label class="check"><input name="triangle" type="checkbox" %TRIANGLE%><span>Directional aircraft</span></label>
                        <label class="check"><input name="trail" type="checkbox" %TRAIL%><span>Flight trails</span></label>
                        <label class="check"><input name="altcolor" type="checkbox" %ALTCOLOR%><span>Altitude colors</span></label>
                        <label class="check"><input name="highlight" type="checkbox" %HIGHLIGHT%><span>Highlights</span></label>
                        <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                    </div>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                    <label class="field mt">
                        <span>Clock UTC offset (hrs):</span>
                        <input name="tz-offset" type="number" min="-12" max="14" step="0.5" value='%TZ_OFFSET%' class="w6">
                    </label>
                    <label class="field mt">
                        <span>Screen-top bearing (window-up, &deg;):</span>
                        <input name="radar-up" type="number" min="0" max="359" step="1" value='%RADAR_UP%' class="w6">
                    </label>
                    <span class="hint">
                        0 = classic north-up. Set it to the compass bearing you face (e.g. 225 for a
                        southwest window) and the radar rotates to match your view &mdash; a blip on the
                        upper-left of the screen is upper-left out the window.
                    </span>
                </fieldset>

                <details class="auto">
                    <summary>Aircraft info text <input name="infotext" type="checkbox" %INFOTEXT%></summary>
                    <div id="info-fields" class="grid3">
                        %INFO_FIELDS%
                    </div>
                </details>

                <details class="auto">
                    <summary>Watchlist &amp; alerts</summary>
                    <label class="stack">
                        <span>Watch (callsign / tail / ICAO / type, comma-separated):</span>
                        <textarea name="watchlist" rows="2">%WATCHLIST%</textarea>
                    </label>
                    <label class="field mt">
                        <span>ntfy.sh topic (phone alerts):</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="grid2 mt">
                        <label class="check"><input name="mil-show" type="checkbox" %MIL_SHOW%><span>Highlight military</span></label>
                        <label class="check"><input name="mil-alert" type="checkbox" %MIL_ALERT%><span>Alert on military (ntfy)</span></label>
                        <label class="check"><input name="heli-show" type="checkbox" %HELI_SHOW%><span>Highlight helicopters</span></label>
                        <label class="check"><input name="spc-show" type="checkbox" %SPC_SHOW%><span>Highlight special flights</span></label>
                    </div>
                    <span class="hint mt">
                        Detected offline from the live feed &mdash; no account or lookup needed. On the radar:
                        military = orange &ldquo;MIL&rdquo;, special flights (rescue / police / NASA / Boeing / Airbus test &hellip;) = blue &ldquo;SPC&rdquo;,
                        helicopters = violet &ldquo;HELI&rdquo;.
                    </span>
                    <div class="row mt">
                        <label class="field">
                            <span>Military visual alert:</span>
                            <select name="mil-visual">
                                <option value="off" %MILVIS_OFF%>Off</option>
                                <option value="ring" %MILVIS_RING%>Edge ring pulse</option>
                                <option value="flash" %MILVIS_FLASH%>Screen flash + ring</option>
                            </select>
                        </label>
                        <label class="field">
                            <span>Emergency-squawk visual alert:</span>
                            <select name="emg-visual">
                                <option value="off" %EMGVIS_OFF%>Off</option>
                                <option value="ring" %EMGVIS_RING%>Edge ring pulse</option>
                                <option value="flash" %EMGVIS_FLASH%>Screen flash + ring</option>
                            </select>
                        </label>
                    </div>
                    <label class="check mt"><input name="visual-night" type="checkbox" %VISUAL_NIGHT%><span>Visual alerts override night dimming</span></label>
                    <span class="hint mt">
                        On-screen attention when a military or emergency-squawk (7500/7600/7700) contact is in range:
                        a colour-pulsing ring at the screen edge (orange = military, red = emergency), or a brief
                        full-screen flash when it first appears &mdash; a few gentle pulses, then the ring.
                    </span>
                    <div class="field mt">
                        <label class="check"><input name="lookup" type="checkbox" %LOOKUP%><span>&ldquo;Look up!&rdquo; overhead alert within</span></label>
                        <input name="lookup-dist" type="number" min="0.5" step="0.5" value='%LOOKUP_DIST%' class="w6">
                        <label class="check"><input name="lookup-alert" type="checkbox" %LOOKUP_ALERT%><span>also ntfy</span></label>
                    </div>
                    <span class="hint mt">
                        Flashes a cyan &ldquo;LOOK UP&rdquo; ring when a contact passes within that distance (in your radar's units) of your location &mdash; glance up and spot it.
                    </span>
                </details>

                <details class="auto">
                    <summary>Spotting logbook <input name="logbook" type="checkbox" %LOGBOOK%></summary>
                    <span class="hint">
                        Keeps a running &ldquo;lifelist&rdquo; of every unique aircraft type, airline, and country
                        you've seen overhead (shown on the Stats screen), and flags a gold &ldquo;NEW&rdquo; on first
                        sightings. It looks up each contact's type/airline, so it adds a little network traffic.
                    </span>
                </details>

                <details class="auto">
                    <summary>Home Assistant / MQTT <input name="mqtt" type="checkbox" %MQTT%></summary>
                    <div class="stack">
                        <div class="row">
                            <label class="field">
                                <span>Broker:</span>
                                <input name="mqtt-host" value='%MQTT_HOST%' placeholder="192.168.1.10" class="grow">
                            </label>
                            <label class="field">
                                <span>Port:</span>
                                <input name="mqtt-port" type="number" min="1" max="65535" value='%MQTT_PORT%' class="w6">
                            </label>
                        </div>
                        <div class="row">
                            <label class="field">
                                <span>Username:</span>
                                <input name="mqtt-user" value='%MQTT_USER%' class="grow">
                            </label>
                            <label class="field">
                                <span>Password:</span>
                                <input name="mqtt-pass" type="password" autocomplete="off" value='%MQTT_PASS%' class="grow">
                            </label>
                        </div>
                        <div class="row">
                            <label class="field">
                                <span>Base topic:</span>
                                <input name="mqtt-base" value='%MQTT_BASE%' placeholder="blipscope" class="grow">
                            </label>
                            <label class="check"><input name="mqtt-disco" type="checkbox" %MQTT_DISCO%><span>HA auto-discovery</span></label>
                        </div>
                    </div>
                    <span class="hint mt">
                        Publishes a retained &ldquo;&lt;base&gt;/summary&rdquo; (count, nearest aircraft, overhead &amp; military flags)
                        to your broker every few seconds. With auto-discovery on, Home Assistant creates the sensors automatically.
                    </span>
                </details>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
        <script>
            // cap the radius at ~2 degrees of scan box (222 km / 138 mi) to stay
            // within OpenSky's rate-limit area, swapping the limit with the unit
            const radiusInput = document.getElementById('radius');
            const radiusUnit = document.getElementById('radius-unit');
            const KM_PER_MILE = 1.609344;
            function updateRadiusMax() {
                radiusInput.max = radiusUnit.value === 'mi' ? '138' : '222';
            }
            radiusUnit.addEventListener('change', function() {
                // the unit just flipped, so convert the displayed value to keep the
                // real-world distance the same: -> mi means it was km, -> km means it was mi
                const value = parseFloat(radiusInput.value);
                if (!isNaN(value)) {
                    const converted = radiusUnit.value === 'mi' ? value / KM_PER_MILE : value * KM_PER_MILE;
                    radiusInput.value = Math.round(converted * 10) / 10;
                }
                updateRadiusMax();
            });
            updateRadiusMax();

            // show only the fields relevant to the selected data source. The hidden
            // blocks' inputs still submit, but the firmware ignores whichever source
            // isn't selected, so a leftover value does no harm. cloud-fields only
            // exists on cloud-capable builds, hence the null guard.
            const dataSource = document.getElementById('data-source');
            const openskyFields = document.getElementById('opensky-fields');
            const localFields = document.getElementById('local-fields');
            const cloudFields = document.getElementById('cloud-fields');
            function syncDataSource() {
                const v = dataSource.value;
                openskyFields.style.display = v === 'opensky' ? '' : 'none';
                localFields.style.display = v === 'local' ? '' : 'none';
                if (cloudFields) cloudFields.style.display = v === 'cloud' ? '' : 'none';
            }
            dataSource.addEventListener('change', syncDataSource);
            syncDataSource();

            // dim the per-field list when the master Aircraft Info toggle is off.
            // purely cosmetic -- the inputs stay enabled so their state still saves.
            const infoMaster = document.querySelector('input[name="infotext"]');
            const infoFields = document.getElementById('info-fields');
            function syncInfoFields() {
                infoFields.style.opacity = infoMaster.checked ? '1' : '0.4';
            }
            infoMaster.addEventListener('change', syncInfoFields);
            syncInfoFields();
        </script>
    </body>
</html>
)";
#elif defined(FEATURE_EAM)
// FEATURE_EAM config page. Stage 1: the valar-eam-feed backend base URL + Reset WiFi.
// Per-screen toggles/reorder, the command-post source dropdown, OpenSky credentials, ntfy,
// poller intervals, and lat/lon arrive in a later stage. Shares the page chrome/JS pattern.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope EAM</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(17,24,39)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(34,197,94)' stroke-width='1'/><circle cx='8' cy='8' r='1.7' fill='rgb(34,197,94)'/></svg>">
        <style>:root{--ink:#22c55e;--line:#22c55e;--dim:#15803d;--btn:#22c55e}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Blipscope EAM</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (EAM)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <label class="field">
                    <span>EAM feed base URL:</span>
                    <input name="eam-base-url" value='%EAM_BASE_URL%' placeholder="https://eam.example.com" class="grow">
                </label>
                <span class="hint">The valar-eam-feed backend this device polls for EAM / Skyking / tempo / propagation / launch data.</span>

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional. Used for propagation day/night and the command-post bearing/distance. Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>

                <details class="auto">
                    <summary>Command-post watch</summary>
                    <label class="field">
                        <span>Source:</span>
                        <select id="abncp-source" name="abncp-source" class="grow">
                            <option value="backend" %ABNCP_BACKEND%>adsb.lol &mdash; via Valar feed (no setup)</option>
                            <option value="opensky" %ABNCP_OPENSKY%>OpenSky &mdash; your account</option>
                        </select>
                    </label>
                    <div id="opensky-fields" class="stack mt">
                        <label class="field">
                            <span>OpenSky client ID:</span>
                            <input name="opensky-id" value='%OPENSKY_ID%' class="grow">
                        </label>
                        <label class="field">
                            <span>OpenSky client secret:</span>
                            <input name="opensky-secret" type="password" autocomplete="off" value='%OPENSKY_SECRET%' class="grow">
                        </label>
                        <label class="stack">
                            <span>ICAO24 watchlist (hex, comma-separated):</span>
                            <textarea name="abncp-watch" rows="2">%ABNCP_WATCH%</textarea>
                        </label>
                        <span class="hint">
                            Queried from this device with YOUR OpenSky account only &mdash; never shared, never routed through the Valar backend.
                            Seeded with the E-4B &ldquo;Nightwatch&rdquo; hexes (verify them); add E-6B hexes as needed. Blank ID/secret keeps the watch off.
                        </span>
                    </div>
                </details>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="grid2 mt">
                        <label class="check"><input name="eam-alert-new" type="checkbox" %ALERT_NEW%><span>New EAM</span></label>
                        <label class="check"><input name="eam-alert-tempo" type="checkbox" %ALERT_TEMPO%><span>Tempo elevated/high</span></label>
                        <label class="check"><input name="eam-alert-abncp" type="checkbox" %ALERT_ABNCP%><span>Command post airborne</span></label>
                        <label class="check"><input name="eam-alert-space" type="checkbox" %ALERT_SPACE%><span>Space weather (HF blackout / storm)</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable all push alerts.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <div class="row">
                        <label class="field">
                            <span>Palette:</span>
                            <select name="eam-palette" class="grow">
                                <option value="green" %PAL_GREEN%>Green console</option>
                                <option value="amber" %PAL_AMBER%>Amber console</option>
                            </select>
                        </label>
                        <label class="field">
                            <span>Refresh:</span>
                            <select name="eam-refresh" class="grow">
                                <option value="normal" %RR_NORMAL%>Normal</option>
                                <option value="relaxed" %RR_RELAXED%>Relaxed (2x)</option>
                                <option value="battery" %RR_BATTERY%>Battery (4x)</option>
                            </select>
                        </label>
                    </div>
                    <div class="grid2 mt">
                        <label class="check"><input name="eam-colon-blink" type="checkbox" %COLON_BLINK%><span>Clock colon blink</span></label>
                        <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                    </div>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <details>
                    <summary>Screens</summary>
                    <label class="stack">
                        <span>Order &amp; enable (comma-separated; omit one to hide it):</span>
                        <input name="eam-screens" value='%EAM_SCREENS%'>
                    </label>
                    <span class="hint mt">ids: ticker, tempo, activity, codewords, abncp, milair, prop, icbm, ref, clock. Empty rotates all. Activity and milair appear only when their feed has data; the clock always shows when nothing else does.</span>
                </details>

                <details>
                    <summary>Logbook</summary>
                    <span class="hint">Download the EAMs &amp; Skyking codewords this device has logged (codewords carry timestamps).</span>
                    <div class="check mt" style="gap:1.5rem">
                        <a href="/eam-log.csv">Download CSV</a>
                        <a href="/eam-log.json">Download JSON</a>
                    </div>
                </details>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
        <script>
            // show the OpenSky credential fields only when that source is selected
            const abncpSrc = document.getElementById('abncp-source');
            const openskyFields = document.getElementById('opensky-fields');
            function syncAbncp() { openskyFields.style.display = abncpSrc.value === 'opensky' ? '' : 'none'; }
            abncpSrc.addEventListener('change', syncAbncp);
            syncAbncp();
        </script>
    </body>
</html>
)";
#elif defined(FEATURE_SPACE)
// FEATURE_SPACE (Spacescope) config page. Stage 1: location, optional backend, screen order,
// ntfy alerts, display. Per-source API keys (Launch Library / NASA) and finer per-screen options
// arrive with the screens that use them. Shares the page chrome / JS pattern.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Spacescope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(8,12,28)'/><circle cx='8' cy='8' r='2' fill='rgb(120,200,255)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(120,200,255)' stroke-width='0.8'/><circle cx='13' cy='4' r='1' fill='rgb(255,255,255)'/></svg>">
        <style>:root{--ink:#7dd3fc;--line:#38bdf8;--dim:#0284c7;--btn:#38bdf8}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Spacescope</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (Space)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional, but unlocks the location-aware screens: next visible ISS pass, local aurora odds, and the solar night auto-dim. Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="grid2 mt">
                        <label class="check"><input name="sp-alert-launch" type="checkbox" %AL_LAUNCH%><span>Launch imminent (T-10 / T-1)</span></label>
                        <label class="check"><input name="sp-alert-aurora" type="checkbox" %AL_AURORA%><span>Aurora likely (high Kp)</span></label>
                        <label class="check"><input name="sp-alert-flare" type="checkbox" %AL_FLARE%><span>Solar flare (M+ class)</span></label>
                        <label class="check"><input name="sp-alert-iss" type="checkbox" %AL_ISS%><span>ISS passing overhead</span></label>
                        <label class="check"><input name="sp-alert-dsn" type="checkbox" %AL_DSN%><span>Deep-space probe contact (DSN)</span></label>
                        <label class="check"><input name="sp-alert-neo" type="checkbox" %AL_ASTEROID%><span>Asteroid inside 1 lunar distance</span></label>
                        <label class="check"><input name="sp-chime" type="checkbox" %AL_CHIME%><span>Chime on the speaker too</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable push alerts (the speaker chime is independent). ISS / aurora alerts need a location above.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <details open>
                    <summary>Screens</summary>
                    <span class="hint">Tick the screens to include in the rotation. Each still appears only when it has data; clock / moon / eclipse / meteor / cosmic are always available. ISS pass, aurora and the star map need a location above.</span>
                    <div class="grid2 mt">
                        %SPACE_SCREENS_HTML%
                    </div>
                </details>

                <details class="auto">
                    <summary>Advanced</summary>
                    <label class="field">
                        <span>Backend base URL:</span>
                        <input name="space-base-url" value='%SPACE_BASE_URL%' placeholder="blank = direct public APIs" class="grow">
                    </label>
                    <span class="hint mt">Optional. Leave blank and Spacescope pulls straight from free public space APIs. Point it at a valar-space-feed backend to offload the heavy / key-gated sources.</span>
                </details>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
    </body>
</html>
)";
#elif defined(FEATURE_SEISMIC)
// FEATURE_SEISMIC (Seismic edition) config page: location, radar magnitude/radius, ntfy alerts,
// display, and an optional backend. Shares the page chrome / JS pattern with the other editions.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope Seismic</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(24,14,4)'/><path d='M1 8 L4 8 L5 3 L7 13 L9 6 L10.5 8 L15 8' fill='none' stroke='rgb(255,170,0)' stroke-width='1.2'/></svg>">
        <style>:root{--ink:#fcd34d;--line:#fbbf24;--dim:#d97706;--btn:#fbbf24}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Blipscope &mdash; Seismic</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (Seismic)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Your location centres the quake radar, the "near me" feed and alerts, and the solar night auto-dim. Without it you still get the worldwide list and stats. Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>

                <fieldset>
                    <legend>Radar</legend>
                    <div class="row">
                        <label class="field">
                            <span>Min magnitude (worldwide):</span>
                            <input name="se-min-mag" type="number" min="0" max="9" step="0.1" value='%SE_MIN_MAG%' class="grow">
                        </label>
                        <label class="field">
                            <span>Radar radius (km):</span>
                            <input name="se-radius-km" type="number" min="50" max="20000" step="10" value='%SE_RADIUS%' class="grow">
                        </label>
                    </div>
                </fieldset>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="stack mt">
                        <label class="check"><input name="se-alert-big" type="checkbox" %AL_BIG%><span>Big quake worldwide, M &ge;</span>
                            <input name="se-big-mag" type="number" min="0" max="9" step="0.1" value='%SE_BIG_MAG%' class="w4"></label>
                        <label class="check"><input name="se-alert-near" type="checkbox" %AL_NEAR%><span>Quake near me, M &ge;</span>
                            <input name="se-near-mag" type="number" min="0" max="9" step="0.1" value='%SE_NEAR_MAG%' class="w4"></label>
                        <label class="check"><input name="se-alert-tsnmi" type="checkbox" %AL_TSUNAMI%><span>Tsunami-flagged quake</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable all push alerts. The "near me" alert needs a location above.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <details class="auto">
                    <summary>Advanced</summary>
                    <label class="field">
                        <span>Backend base URL:</span>
                        <input name="se-base-url" value='%SE_BASE_URL%' placeholder="blank = USGS directly" class="grow">
                    </label>
                    <span class="hint mt">Optional. Leave blank and the device pulls straight from the public USGS earthquake API.</span>
                </details>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
    </body>
</html>
)";
#elif defined(FEATURE_BIRDING)
// FEATURE_BIRDING (Birding edition) config page: eBird API key (BYO, masked), location, search
// radius/look-back, target species, ntfy alerts, display. Shares the page chrome / JS pattern.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope Birding</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(8,20,8)'/><circle cx='6.5' cy='7' r='3' fill='rgb(150,220,130)'/><circle cx='7.5' cy='6.2' r='0.7' fill='rgb(8,20,8)'/><path d='M9 7 L13 6 L10 8 Z' fill='rgb(255,215,90)'/></svg>">
        <style>:root{--ink:#86efac;--line:#22c55e;--dim:#16a34a;--btn:#4ade80}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Blipscope &mdash; Birding</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (Birding)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <fieldset>
                    <legend>eBird</legend>
                    <label class="field">
                        <span>API key:</span>
                        <input name="ebird-key" type="password" autocomplete="off" value='%EBIRD_KEY%' class="grow">
                    </label>
                    <span class="hint mt">Free with an eBird account &mdash; generate one at <a href="https://ebird.org/api/keygen" target="_blank" rel="noopener">ebird.org/api/keygen</a>. It's stored on the device and sent only to eBird. Nothing is fetched until a key and location are set.</span>
                </fieldset>

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Your location centres the sightings radar, the nearby feeds, and alerts. Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>

                <fieldset>
                    <legend>Search</legend>
                    <div class="row">
                        <label class="field">
                            <span>Radius (km, max 50):</span>
                            <input name="bd-radius-km" type="number" min="1" max="50" value='%BD_RADIUS%' class="grow">
                        </label>
                        <label class="field">
                            <span>Look-back (days, max 30):</span>
                            <input name="bd-back-days" type="number" min="1" max="30" value='%BD_BACK%' class="grow">
                        </label>
                    </div>
                </fieldset>

                <details class="auto">
                    <summary>Targets</summary>
                    <label class="stack">
                        <span>Target species (comma-separated names or codes):</span>
                        <input name="bd-targets" value='%BD_TARGETS%' placeholder="e.g. Painted Bunting, Snowy Owl">
                    </label>
                    <span class="hint mt">A "Targets" screen lists matches nearby, and (with a topic below) you get a phone alert when one appears.</span>
                </details>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="grid2 mt">
                        <label class="check"><input name="bd-alert-rare" type="checkbox" %AL_NOTABLE%><span>Notable / rare sighting nearby</span></label>
                        <label class="check"><input name="bd-alert-target" type="checkbox" %AL_TARGET%><span>Target species appears</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable all push alerts.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
    </body>
</html>
)";
#elif defined(FEATURE_FISHING)
// FEATURE_FISHING (Reelscope) config page: water type, freshwater (USGS) + saltwater (NOAA/NDBC)
// stations, per-view toggles, ntfy alerts + thresholds, display, and an optional aggregator. All
// feeds are keyless -- no masked secret. Shares the page chrome / JS pattern with the other editions.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Reelscope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(4,16,22)'/><path d='M2 8 Q5 4 9 8 Q5 12 2 8 Z' fill='rgb(120,220,255)'/><circle cx='4' cy='7.4' r='0.6' fill='rgb(4,16,22)'/><path d='M9 8 L13 5 L12 8 L13 11 Z' fill='rgb(120,230,140)'/></svg>">
        <style>:root{--ink:#a5f3fc;--line:#06b6d4;--dim:#0891b2;--btn:#22d3ee}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Reelscope &mdash; Fishing</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (Reelscope)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <label class="field">
                    <span>Water type:</span>
                    <select name="fi-water" class="grow">
                        <option value="both" %FI_WATER_BOTH%>Both</option>
                        <option value="fresh" %FI_WATER_FRESH%>Freshwater only</option>
                        <option value="salt" %FI_WATER_SALT%>Saltwater only</option>
                    </select>
                </label>
                <span class="hint">Fresh-only and salt-only skip the other family's feeds entirely.</span>

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Location drives on-device solunar/sun/moon, the keyless weather feed, and the night auto-dim. Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>

                <fieldset>
                    <legend>Freshwater (USGS)</legend>
                    <label class="field">
                        <span>USGS site number:</span>
                        <input name="fi-usgs" value='%FI_USGS%' placeholder="e.g. 08167000" class="grow">
                    </label>
                    <span class="hint mt">Find your gauge at <a href="https://waterdata.usgs.gov" target="_blank" rel="noopener">waterdata.usgs.gov</a>. Keyless.</span>
                </fieldset>

                <fieldset>
                    <legend>Saltwater (NOAA)</legend>
                    <label class="field">
                        <span>CO-OPS tide station:</span>
                        <input id="fi-noaa" name="fi-noaa" value='%FI_NOAA%' placeholder="e.g. 8443970" class="grow">
                        <button type="button" id="findstation" class="btn-line">Find nearest</button>
                    </label>
                    <label class="field mt">
                        <span>NDBC buoy:</span>
                        <input id="fi-buoy" name="fi-buoy" value='%FI_BUOY%' placeholder="e.g. 44013" class="grow">
                        <button type="button" id="findbuoy" class="btn-line">Find nearest</button>
                    </label>
                    <span class="hint mt">Stations at <a href="https://tidesandcurrents.noaa.gov" target="_blank" rel="noopener">tidesandcurrents.noaa.gov</a> / buoys at <a href="https://www.ndbc.noaa.gov" target="_blank" rel="noopener">ndbc.noaa.gov</a>. Keyless. "Find nearest" uses the location above.</span>
                    <label class="field mt">
                        <span>Units:</span>
                        <select name="fi-units">
                            <option value="imperial" %FI_UNITS_IMP%>Imperial (ft, &deg;F, mph, inHg)</option>
                            <option value="metric" %FI_UNITS_MET%>Metric (m, &deg;C, km/h, hPa)</option>
                        </select>
                    </label>
                </fieldset>

                <fieldset>
                    <legend>Views</legend>
                    <div class="grid4">
                        <label class="check"><input name="fi-v-tide" type="checkbox" %FI_V_TIDE%><span>Tide</span></label>
                        <label class="check"><input name="fi-v-flow" type="checkbox" %FI_V_FLOW%><span>Flow</span></label>
                        <label class="check"><input name="fi-v-temp" type="checkbox" %FI_V_TEMP%><span>Water temp</span></label>
                        <label class="check"><input name="fi-v-solunar" type="checkbox" %FI_V_SOLUNAR%><span>Solunar</span></label>
                        <label class="check"><input name="fi-v-weather" type="checkbox" %FI_V_WEATHER%><span>Weather</span></label>
                        <label class="check"><input name="fi-v-moon" type="checkbox" %FI_V_MOON%><span>Moon</span></label>
                        <label class="check"><input name="fi-v-catch" type="checkbox" %FI_V_CATCH%><span>Catch log</span></label>
                        <label class="check"><input name="fi-v-clock" type="checkbox" %FI_V_CLOCK%><span>Clock</span></label>
                    </div>
                    <span class="hint mt">Enabled views auto-rotate (skipping any with no data) and are swipeable; tap a dial to inspect it.</span>
                </fieldset>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="stack mt">
                        <label class="check"><input name="fi-a-solunar" type="checkbox" %FI_A_SOLUNAR%><span>Bite window opening (solunar major)</span></label>
                        <label class="check"><input name="fi-a-baro" type="checkbox" %FI_A_BARO%><span>Barometer falling fast (front moving in)</span></label>
                        <label class="check"><input name="fi-a-tide" type="checkbox" %FI_A_TIDE%><span>A high/low tide is ~30 min away</span></label>
                        <label class="check" style="flex-wrap:wrap"><input name="fi-a-flow" type="checkbox" %FI_A_FLOW%><span>River crosses</span>
                            <input name="fi-flow-cfs" type="number" min="0" step="1" value='%FI_FLOW_CFS%' class="w6"><span>CFS</span></label>
                        <label class="check" style="flex-wrap:wrap"><input name="fi-a-temp" type="checkbox" %FI_A_TEMP%><span>Water temp enters</span>
                            <input name="fi-temp-lo" type="number" step="1" value='%FI_TEMP_LO%' class="w4"><span>&ndash;</span>
                            <input name="fi-temp-hi" type="number" step="1" value='%FI_TEMP_HI%' class="w4"><span>&deg;</span></label>
                        <label class="check"><input name="fi-chime" type="checkbox" %FI_CHIME%><span>Also chime the speaker on alerts</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable push alerts (the speaker chime still works). Thresholds are edge-triggered and seeded at boot, so the backlog never fires. The CFS and water-temp band are in your selected units &mdash; re-enter them if you change units.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <div class="field">
                        <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                        <label class="check"><span>UTC offset (h):</span>
                            <input name="fi-tz-offset" type="number" min="-14" max="14" step="0.5" value='%FI_TZ%' class="w6"></label>
                    </div>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <details class="auto">
                    <summary>Advanced</summary>
                    <label class="field">
                        <span>Aggregator base URL:</span>
                        <input name="fi-base-url" value='%FI_BASE_URL%' placeholder="blank = public APIs directly" class="grow">
                    </label>
                    <span class="hint mt">Optional. Leave blank and the device pulls straight from the public USGS / NOAA / Open-Meteo APIs.</span>
                </details>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
        <script>
            // Resolve the nearest NOAA tide-prediction station in the browser (it has the heap for the
            // full ~3450-station list); the device then only ever stores + polls the one station id.
            document.getElementById('findstation').addEventListener('click', async function() {
                const la = parseFloat(document.querySelector('[name=latitude]').value);
                const lo = parseFloat(document.querySelector('[name=longitude]').value);
                if (isNaN(la) || isNaN(lo)) { alert('Enter latitude and longitude first.'); return; }
                const btn = this; btn.textContent = 'searching...';
                try {
                    const r = await fetch('https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions');
                    const j = await r.json();
                    let best = null, bd = 1e18;
                    for (const s of j.stations) {
                        const dx = (s.lng - lo) * Math.cos(la * Math.PI / 180), dy = s.lat - la;
                        const d = dx * dx + dy * dy;
                        if (d < bd) { bd = d; best = s; }
                    }
                    if (best) { document.getElementById('fi-noaa').value = best.id; btn.textContent = '✓ ' + best.name.substring(0, 16); }
                    else btn.textContent = 'none found';
                } catch (e) { btn.textContent = 'error - enter manually'; }
            });
            // Nearest NDBC buoy that reports meteorology (met='y'), resolved in the browser from the
            // active-stations XML; the device only stores + polls the one buoy id.
            document.getElementById('findbuoy').addEventListener('click', async function() {
                const la = parseFloat(document.querySelector('[name=latitude]').value);
                const lo = parseFloat(document.querySelector('[name=longitude]').value);
                if (isNaN(la) || isNaN(lo)) { alert('Enter latitude and longitude first.'); return; }
                const btn = this; btn.textContent = 'searching...';
                try {
                    const r = await fetch('https://www.ndbc.noaa.gov/activestations.xml');
                    const xml = new DOMParser().parseFromString(await r.text(), 'text/xml');
                    let best = null, bd = 1e18;
                    for (const s of xml.getElementsByTagName('station')) {
                        if (s.getAttribute('met') !== 'y') continue;
                        const sy = parseFloat(s.getAttribute('lat')), sx = parseFloat(s.getAttribute('lon'));
                        const dx = (sx - lo) * Math.cos(la * Math.PI / 180), dy = sy - la, d = dx * dx + dy * dy;
                        if (d < bd) { bd = d; best = s; }
                    }
                    if (best) { document.getElementById('fi-buoy').value = best.getAttribute('id'); btn.textContent = '✓ ' + best.getAttribute('id'); }
                    else btn.textContent = 'none found';
                } catch (e) { btn.textContent = 'error - enter manually'; }
            });
        </script>
    </body>
</html>
)";
#elif defined(FEATURE_CLAUDESCOPE)
// FEATURE_CLAUDESCOPE (Claudescope) config page: the on-LAN sidecar URL (required), location (for the
// night auto-dim + clock), alert thresholds, ntfy, and display. All feeds are keyless -- no masked
// secret; the Claude OAuth token stays on the sidecar host, never on the device. Shares the page
// chrome / JS pattern with the other editions.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Claudescope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(28,18,12)'/><g stroke='rgb(217,119,87)' stroke-width='1.4' stroke-linecap='round'><path d='M8 3 L8 13'/><path d='M3.7 5.5 L12.3 10.5'/><path d='M3.7 10.5 L12.3 5.5'/></g></svg>">
        <style>:root{--ink:#fed7aa;--line:#fb923c;--dim:#ea580c;--btn:#fb923c}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Claudescope</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (Claudescope)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <label class="field">
                    <span>Sidecar URL:</span>
                    <input name="cl-base-url" value='%CL_BASE_URL%' placeholder="http://192.168.1.50:8080" class="grow">
                </label>
                <span class="hint">Run <code>claudescope-sidecar</code> on a machine on your LAN (see tools/claudescope-sidecar), then point this at it. The sidecar holds your Claude token; the device only ever sees usage numbers. Until this is set, the device shows the setup splash.</span>

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional. Location drives only the night auto-dim and the local clock; usage numbers work without it. Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="stack mt">
                        <label class="check" style="flex-wrap:wrap"><input name="cl-alert-sess" type="checkbox" %AL_SESSION%><span>Session usage reaches</span>
                            <input name="cl-session-pct" type="number" min="1" max="100" step="1" value='%CL_SESSION_PCT%' class="w4"><span>&#37;</span></label>
                        <label class="check" style="flex-wrap:wrap"><input name="cl-alert-week" type="checkbox" %AL_WEEK%><span>Weekly usage reaches</span>
                            <input name="cl-week-pct" type="number" min="1" max="100" step="1" value='%CL_WEEK_PCT%' class="w4"><span>&#37;</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable all push alerts. Thresholds are edge-triggered and seeded at boot, so the state already high when you power on never fires.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <div class="field">
                        <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                        <label class="check"><span>UTC offset (h):</span>
                            <input name="cl-tz-offset" type="number" min="-14" max="14" step="0.5" value='%CL_TZ%' class="w6"></label>
                    </div>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
    </body>
</html>
)";
#elif defined(FEATURE_SPEED)
// FEATURE_SPEED (Speedscope) config page: the MiniSpeedCam host, posted limit, per-view toggles,
// ntfy alerts + speeder threshold, display, and an optional proxy. All camera endpoints are keyless
// on the LAN -- no masked secret. Shares the page chrome / JS pattern with the other editions.
static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Speedscope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(20,14,2)'/><path d='M2 12 A6 6 0 0 1 14 12' fill='none' stroke='rgb(255,176,40)' stroke-width='1.4'/><line x1='8' y1='12' x2='12' y2='6' stroke='rgb(255,60,40)' stroke-width='1.4'/><circle cx='8' cy='12' r='1' fill='rgb(255,176,40)'/></svg>">
        <style>:root{--ink:#fde68a;--line:#f59e0b;--dim:#d97706;--btn:#fbbf24}</style>
)" CONFIG_SHELL_CSS R"(
    </head>
    <body>
        <fieldset class="wrap">
            <legend>Configure Speedscope &mdash; Speed radar</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION% (Speedscope)</span>
            </div>

            <form id="cfg" action="/save" method="POST">

                <fieldset>
                    <legend>MiniSpeedCam</legend>
                    <label class="field">
                        <span>Camera host:</span>
                        <input name="sc-host" value='%SC_HOST%' placeholder="MiniSpeedCam, or an IP e.g. 192.168.1.50" class="grow">
                    </label>
                    <span class="hint mt">The MiniSpeedCam on your network. A bare name is resolved over mDNS (&lt;name&gt;.local); an IP is most reliable. Blank = MiniSpeedCam.</span>
                    <label class="field mt">
                        <span>Posted speed limit:</span>
                        <input name="sc-limit" type="number" min="0" step="1" value='%SC_LIMIT%' placeholder="optional" class="w8">
                    </label>
                    <span class="hint mt">In the camera's own unit (mph/kph, as set on the camera). Over-limit passes read red. Leave blank to disable.</span>
                </fieldset>

                <fieldset>
                    <legend>Views</legend>
                    <div class="grid3">
                        <label class="check"><input name="sc-v-last" type="checkbox" %SC_V_LAST%><span>Last pass</span></label>
                        <label class="check"><input name="sc-v-live" type="checkbox" %SC_V_LIVE%><span>Live</span></label>
                        <label class="check"><input name="sc-v-list" type="checkbox" %SC_V_LIST%><span>Recent</span></label>
                        <label class="check"><input name="sc-v-stats" type="checkbox" %SC_V_STATS%><span>Today</span></label>
                        <label class="check"><input name="sc-v-device" type="checkbox" %SC_V_DEVICE%><span>Camera</span></label>
                        <label class="check"><input name="sc-v-clock" type="checkbox" %SC_V_CLOCK%><span>Clock</span></label>
                    </div>
                    <span class="hint mt">Enabled views auto-rotate (skipping any with no data) and are swipeable; tap a view to inspect it.</span>
                </fieldset>

                <details class="auto">
                    <summary>Alerts (ntfy)</summary>
                    <label class="field">
                        <span>ntfy.sh topic:</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <div class="stack mt">
                        <label class="check" style="flex-wrap:wrap"><input name="sc-a-speeder" type="checkbox" %SC_A_SPEEDER%><span>Speeder: a pass at/over</span>
                            <input name="sc-alert-speed" type="number" min="0" step="1" value='%SC_ALERT%' class="w6"><span>mph/kph</span></label>
                        <label class="check"><input name="sc-a-record" type="checkbox" %SC_A_RECORD%><span>New fastest pass of the day</span></label>
                        <label class="check"><input name="sc-a-offline" type="checkbox" %SC_A_OFFLINE%><span>Camera goes offline</span></label>
                    </div>
                    <span class="hint mt">Leave the topic blank to disable all push alerts. Triggers are edge-detected and seeded at boot, so the backlog never fires.</span>
                </details>

                <fieldset>
                    <legend>Display</legend>
                    <div class="field">
                        <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                        <label class="check"><span>UTC offset (h):</span>
                            <input name="sc-tz-offset" type="number" min="-14" max="14" step="0.5" value='%SC_TZ%' class="w6"></label>
                    </div>
                    <div class="row mt">
                        <label class="field">
                            <span>Latitude:</span>
                            <input name="latitude" type="number" min="-90" step="0.000001" max="90" value='%LATITUDE%' class="grow">
                        </label>
                        <label class="field">
                            <span>Longitude:</span>
                            <input name="longitude" type="number" min="-180" step="0.000001" max="180" value='%LONGITUDE%' class="grow">
                        </label>
                    </div>
                    <span class="hint mt">Location is optional &mdash; it only drives the night auto-dim (sunset/sunrise at your spot). Tip: paste a &ldquo;lat, lon&rdquo; pair into the latitude box and both fields fill in.</span>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                </fieldset>

                <details class="auto">
                    <summary>Advanced</summary>
                    <label class="field">
                        <span>Proxy base URL:</span>
                        <input name="sc-base-url" value='%SC_BASE_URL%' placeholder="blank = the camera on your LAN directly" class="grow">
                    </label>
                    <span class="hint mt">Optional. Point at an aggregator that mirrors the camera's /api/state and /api/events (e.g. to reach it off your LAN). Blank = the local camera directly.</span>
                </details>

                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
            </form>

            <div class="foot">
                <a href="https://github.com/Valar-Systems/Blipscope/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                <button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button>
            </div>
        </fieldset>
)" CONFIG_SHELL_JS R"(
    </body>
</html>
)";
#endif

// Escape a user-sourced value before it is echoed into the config page. Without
// this a stored value containing ' " < > & either shreds the form on the next
// load (a legit apostrophe in a watchlist/URL/username terminates the attribute)
// or plants stored XSS that runs in the owner's browser on the device origin.
// Applied to every free-text value returned by the template callbacks. Escaping
// the toggle/enum/number defaults ("true"/"km"/"255") is a harmless no-op.
static String HtmlEscape(const String& in)
{
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:  out += c;
        }
    }
    return out;
}

// Reject a state-changing request that didn't originate from the config page itself.
// The page's own fetch() adds "X-Blipscope: 1"; a cross-origin page (CSRF) cannot set
// a custom header on a POST without a CORS preflight the server never answers, so a
// missing/blank header means the request came from somewhere else. This blocks a
// malicious LAN/internet page from silently wiping Wi-Fi creds or rewriting config.
// Returns true if the request was rejected (and a 403 sent).
static bool RejectCrossOrigin(AsyncWebServerRequest* request)
{
    if (request->hasHeader("X-Blipscope"))
        return false;
    Serial.println("[POST] rejected: missing X-Blipscope header (cross-origin/CSRF)");
    request->send(403, "text/plain", "forbidden: cross-origin request");
    return true;
}

// Accept only a Host header that names THIS device: the mDNS name (bare or .local)
// or a bare IP literal. A DNS-rebinding attack reaches the device via an attacker
// DOMAIN that re-resolves to the LAN IP, so the Host is that domain -- rejecting
// unknown hostnames blocks it, while IP-literal and <name>.local (how a user
// actually reaches the page) stay allowed. Empty Host (some minimal clients) is
// allowed so we don't break legitimate odd clients.
static bool HostAllowed(AsyncWebServerRequest* request)
{
    String host = request->host();
    const int colon = host.indexOf(':');   // strip any :port
    if (colon >= 0) host = host.substring(0, colon);
    host.toLowerCase();
    if (host.isEmpty()) return true;

    bool ipLiteral = true;                  // all digits/dots (v4) or contains ':' (v6, already stripped above)
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if ((c < '0' || c > '9') && c != '.') { ipLiteral = false; break; }
    }
    if (ipLiteral) return true;

    String name = DeviceIdentity::Name();
    name.toLowerCase();
    return host == name || host == name + ".local";
}

// The config page renders stored secrets as ALL asterisks (std::fill in the GET
// handler), so only a value that is entirely '*' is the untouched mask sentinel.
// Testing "contains an asterisk" instead would silently drop the save of any real
// secret/password that merely contains one. [[maybe_unused]]: only the builds with
// secret fields (radar/EAM/Birding) reference it.
[[maybe_unused]] static bool IsMaskedValue(const String& v)
{
    if (v.isEmpty()) return false;
    for (size_t i = 0; i < v.length(); ++i)
        if (v[i] != '*') return false;
    return true;
}

void ConfigurationWebServer::Initialise() {
    // Create the "config" NVS namespace up front. Opening read-write creates it if
    // missing, so the read-only reads here, in AircraftManager, and every frame in
    // loop() stop logging "nvs_open failed: NOT_FOUND" before the user has ever saved
    // settings. Reads still fall back to their defaults until the config page is used.
    {
        Preferences prefs;
        prefs.begin("config", false);
        prefs.end();
    }

    // start mDNS with a per-device hostname (e.g. Blipscope-A1B2C3.local)
    // so multiple boards on the same network don't collide
    if (!MDNS.begin(DeviceIdentity::Name().c_str())) {
        Serial.println("[WARN] Failed to start mDNS. Continuing without mDNS...");
    }

    // Handle visit to config web server
    server.on("/", HTTP_GET, [&](AsyncWebServerRequest* request) {
        // Anti-DNS-rebinding: only serve the config page (home location, opensky-id,
        // mqtt-user, ntfy topic...) to a request that actually addressed this device.
        if (!HostAllowed(request)) {
            Serial.printf("[GET] rejected Host '%s' (DNS-rebinding guard)\n", request->host().c_str());
            request->send(403, "text/plain", "forbidden: bad Host");
            return;
        }
        Serial.println("[GET] Handling request to config web server...");
        // Diagnostic: the async response needs a ~2.8 KB *contiguous* send buffer
        // (ASYNC_RESPONCE_BUFF_SIZE = 2 x TCP_MSS). If the largest free block is
        // below that, ESPAsyncWebServer silently fails to send and the page hangs.
        Serial.printf("[GET] heap free=%u largest-block=%u\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());

        // status-strip values shared by every edition's page (device name / IP /
        // RSSI at the top of the form, so users can confirm health at a glance)
        const String deviceName = DeviceIdentity::Name();
        const String deviceIp = WiFi.localIP().toString();
        const String wifiRssi = String(WiFi.RSSI());

        // read all values up front so the processor lambda can capture by value
        Preferences prefs;
        prefs.begin("config", true);
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        const String radius = HtmlEscape(prefs.getString("radius", "100"));
        // isKey() probes without logging; a plain getString() on this not-yet-saved
        // key spams "nvs_get_str ... NOT_FOUND" on every page load until first save
        const String radiusUnit = HtmlEscape(prefs.isKey("radius-unit") ? prefs.getString("radius-unit", "km") : "km");
        const String openskyClientId = HtmlEscape(prefs.getString("opensky-id", ""));
        String openskySecret = HtmlEscape(prefs.getString("opensky-secret", ""));
#ifdef FEATURE_CLOUD_FEED
        // Cloud builds default the unset key to the proxy: new devices land on
        // Blipscope Cloud out of the box (AircraftManager::Initialise mirrors this).
        const String dataSource = HtmlEscape(prefs.isKey("data-source") ? prefs.getString("data-source", "cloud") : "cloud");
        const String cloudUrlCfg = HtmlEscape(prefs.isKey("cloud-url") ? prefs.getString("cloud-url", "") : "");
        String cloudKeyCfg = HtmlEscape(prefs.getString("cloud-key", ""));
#else
        const String dataSource = HtmlEscape(prefs.isKey("data-source") ? prefs.getString("data-source", "opensky") : "opensky");
#endif
        const String localUrl = HtmlEscape(prefs.getString("local-url", ""));
        const String scanlineEnabled = HtmlEscape(prefs.getString("scanline", "true"));
        const String fadeEnabled = HtmlEscape(prefs.getString("fade", "true"));
        const String infoTextEnabled = HtmlEscape(prefs.getString("infotext", "true"));
        const String triangleEnabled = HtmlEscape(prefs.getString("triangle", "true"));
        const String trailEnabled = HtmlEscape(prefs.getString("trail", "true"));
        const String altColorEnabled = HtmlEscape(prefs.getString("altcolor", "true"));
        const String highlightEnabled = HtmlEscape(prefs.getString("highlight", "true"));
        const String autoDimEnabled = HtmlEscape(prefs.getString("autodim", "true"));
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
        // default the clock offset to the nominal zone from longitude (15 deg/hour)
        const String tzOffset = prefs.isKey("tz-offset")
            ? prefs.getString("tz-offset", "0")
            : String((int)round(longitude.toFloat() / 15.0));
        const String radarUp = HtmlEscape(prefs.isKey("radar-up") ? prefs.getString("radar-up", "0") : "0");
        const String watchlist = HtmlEscape(prefs.getString("watchlist", ""));
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        // isKey() guards keep the not-yet-saved reads from logging NVS NOT_FOUND
        const String milShow = HtmlEscape(prefs.isKey("mil-show") ? prefs.getString("mil-show", "true") : "true");
        const String milAlert = HtmlEscape(prefs.isKey("mil-alert") ? prefs.getString("mil-alert", "false") : "false");
        const String heliShow = HtmlEscape(prefs.isKey("heli-show") ? prefs.getString("heli-show", "false") : "false");
        const String spcShow = HtmlEscape(prefs.isKey("spc-show") ? prefs.getString("spc-show", "false") : "false");
        // visual alerts: defaults mirror AircraftManager::Initialise (emergency = ring, military = off)
        const String milVisual = HtmlEscape(prefs.isKey("mil-visual") ? prefs.getString("mil-visual", "off") : "off");
        const String emgVisual = HtmlEscape(prefs.isKey("emg-visual") ? prefs.getString("emg-visual", "ring") : "ring");
        const String visualNight = HtmlEscape(prefs.isKey("visual-night") ? prefs.getString("visual-night", "false") : "false");
        const String logbookOn = HtmlEscape(prefs.isKey("logbook") ? prefs.getString("logbook", "false") : "false");
        const String lookupOn = HtmlEscape(prefs.isKey("lookup") ? prefs.getString("lookup", "false") : "false");
        const String lookupAlert = HtmlEscape(prefs.isKey("lookup-alert") ? prefs.getString("lookup-alert", "false") : "false");
        const String lookupDist = HtmlEscape(prefs.isKey("lookup-dist") ? prefs.getString("lookup-dist", "3") : "3");
        const String mqttOn = HtmlEscape(prefs.isKey("mqtt") ? prefs.getString("mqtt", "false") : "false");
        const String mqttHost = HtmlEscape(prefs.getString("mqtt-host", ""));
        const String mqttPort = HtmlEscape(prefs.isKey("mqtt-port") ? prefs.getString("mqtt-port", "1883") : "1883");
        const String mqttUser = HtmlEscape(prefs.getString("mqtt-user", ""));
        String mqttPass = HtmlEscape(prefs.getString("mqtt-pass", ""));
        const String mqttBase = HtmlEscape(prefs.isKey("mqtt-base") ? prefs.getString("mqtt-base", "blipscope") : "blipscope");
        const String mqttDisco = HtmlEscape(prefs.isKey("mqtt-disco") ? prefs.getString("mqtt-disco", "true") : "true");

        // Build the per-field info checkboxes from the shared table so the form
        // always reflects exactly the fields the renderer knows how to draw.
        String infoFieldsHtml;
        for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i) {
            const AircraftInfoFieldDef& field = AIRCRAFT_INFO_FIELDS[i];
            const bool checked = prefs.isKey(field.key)
                ? (prefs.getString(field.key, "") == "true")
                : field.defaultOn;
            infoFieldsHtml += F("<label class=\"check\"><input type=\"checkbox\" name=\"");
            infoFieldsHtml += field.key;
            infoFieldsHtml += '"';
            if (checked) infoFieldsHtml += F(" checked");
            infoFieldsHtml += F("><span>");
            infoFieldsHtml += field.label;
            infoFieldsHtml += F("</span></label>");
        }
#elif defined(FEATURE_EAM)
        // FEATURE_EAM: load the EAM config fields. isKey() guards keep a not-yet-saved read from
        // logging NVS NOT_FOUND; the base-URL default is the EAM_FEED_BASE build flag.
        const String eamBaseUrl = HtmlEscape(prefs.isKey("eam-base-url")
            ? prefs.getString("eam-base-url", EAM_FEED_BASE)
            : String(EAM_FEED_BASE));
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        const String abncpSource = HtmlEscape(prefs.isKey("abncp-source") ? prefs.getString("abncp-source", "backend") : "backend");
        const String openskyClientId = HtmlEscape(prefs.getString("opensky-id", ""));
        String openskySecret = HtmlEscape(prefs.getString("opensky-secret", ""));
        const String abncpWatch = HtmlEscape(prefs.getString("abncp-watch", ""));
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        const String alertNew = HtmlEscape(prefs.isKey("eam-alert-new") ? prefs.getString("eam-alert-new", "true") : "true");
        const String alertTempo = HtmlEscape(prefs.isKey("eam-alert-tempo") ? prefs.getString("eam-alert-tempo", "true") : "true");
        const String alertAbncp = HtmlEscape(prefs.isKey("eam-alert-abncp") ? prefs.getString("eam-alert-abncp", "true") : "true");
        const String alertSpace = HtmlEscape(prefs.isKey("eam-alert-space") ? prefs.getString("eam-alert-space", "true") : "true");
        const String eamPalette = HtmlEscape(prefs.isKey("eam-palette") ? prefs.getString("eam-palette", "green") : "green");
        const String eamRefresh = HtmlEscape(prefs.isKey("eam-refresh") ? prefs.getString("eam-refresh", "normal") : "normal");
        const String colonBlink = HtmlEscape(prefs.isKey("eam-colon-blink") ? prefs.getString("eam-colon-blink", "false") : "false");
        const String autoDimEnabled = HtmlEscape(prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
        // default the field to the full ordered set so the user can see and edit it
        const String eamScreens = prefs.isKey("eam-screens")
            ? prefs.getString("eam-screens", "")
            : String("ticker,tempo,activity,codewords,abncp,milair,prop,icbm,ref,clock");
#elif defined(FEATURE_SPACE)
        // FEATURE_SPACE: load the Spacescope config fields. isKey() guards keep not-yet-saved
        // reads from logging NVS NOT_FOUND; the backend base-URL default is the SPACE_FEED_BASE
        // build flag (empty = direct public APIs).
        const String spaceBaseUrl = HtmlEscape(prefs.isKey("space-base-url")
            ? prefs.getString("space-base-url", SPACE_FEED_BASE)
            : String(SPACE_FEED_BASE));
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        const String alertLaunch = HtmlEscape(prefs.isKey("sp-alert-launch") ? prefs.getString("sp-alert-launch", "true") : "true");
        const String alertAurora = HtmlEscape(prefs.isKey("sp-alert-aurora") ? prefs.getString("sp-alert-aurora", "true") : "true");
        const String alertFlare = HtmlEscape(prefs.isKey("sp-alert-flare") ? prefs.getString("sp-alert-flare", "true") : "true");
        const String alertIss = HtmlEscape(prefs.isKey("sp-alert-iss") ? prefs.getString("sp-alert-iss", "true") : "true");
        const String alertDsn = HtmlEscape(prefs.isKey("sp-alert-dsn") ? prefs.getString("sp-alert-dsn", "false") : "false");
        const String alertAsteroid = HtmlEscape(prefs.isKey("sp-alert-neo") ? prefs.getString("sp-alert-neo", "true") : "true");
        const String chimeOnAlert = HtmlEscape(prefs.isKey("sp-chime") ? prefs.getString("sp-chime", "true") : "true");
        const String autoDimEnabled = HtmlEscape(prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
        const String spaceScreens = prefs.isKey("space-screens")
            ? prefs.getString("space-screens", "")
            : String("iss,isspass,launch,kp,solarwind,scales,flare,aurora,dsn,deepspace,asteroid,humans,moon,starmap,observing,planets,algol,dso,orrery,jupiter,lunar,eclipse,meteor,cosmic,logbook,clock");

        // Build the screen on/off checkbox grid from the canonical table, reflecting the saved CSV
        // (empty = all on, matching SpaceManager). Each box is "scr-<id>"; the save rebuilds the CSV.
        const bool spaceScreensAll = spaceScreens.isEmpty();
        String spaceScreensCsv = "," + spaceScreens + ",";
        spaceScreensCsv.replace(" ", "");
        spaceScreensCsv.toLowerCase();
        String spaceScreensHtml;
        for (size_t i = 0; i < SPACE_SCREEN_DEF_COUNT; ++i) {
            const SpaceScreenDef& s = SPACE_SCREEN_DEFS[i];
            const bool on = spaceScreensAll || spaceScreensCsv.indexOf("," + String(s.id) + ",") >= 0;
            spaceScreensHtml += F("<label class=\"check\"><input type=\"checkbox\" name=\"scr-");
            spaceScreensHtml += s.id;
            spaceScreensHtml += '"';
            if (on) spaceScreensHtml += F(" checked");
            spaceScreensHtml += F("><span>");
            spaceScreensHtml += s.label;
            spaceScreensHtml += F("</span></label>");
        }
#elif defined(FEATURE_SEISMIC)
        // FEATURE_SEISMIC: load the Seismic edition config fields. isKey() guards keep not-yet-saved
        // reads from logging NVS NOT_FOUND; the device talks to USGS directly (se-base-url empty).
        const String seBaseUrl = HtmlEscape(prefs.getString("se-base-url", ""));
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        const String seMinMag = HtmlEscape(prefs.isKey("se-min-mag") ? prefs.getString("se-min-mag", "2.5") : "2.5");
        const String seRadius = HtmlEscape(prefs.isKey("se-radius-km") ? prefs.getString("se-radius-km", "500") : "500");
        const String seBigMag = HtmlEscape(prefs.isKey("se-big-mag") ? prefs.getString("se-big-mag", "6.0") : "6.0");
        const String seNearMag = HtmlEscape(prefs.isKey("se-near-mag") ? prefs.getString("se-near-mag", "4.0") : "4.0");
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        const String alertBig = HtmlEscape(prefs.isKey("se-alert-big") ? prefs.getString("se-alert-big", "true") : "true");
        const String alertNear = HtmlEscape(prefs.isKey("se-alert-near") ? prefs.getString("se-alert-near", "true") : "true");
        const String alertTsunami = HtmlEscape(prefs.isKey("se-alert-tsnmi") ? prefs.getString("se-alert-tsnmi", "true") : "true");
        const String autoDimEnabled = HtmlEscape(prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
#elif defined(FEATURE_BIRDING)
        // FEATURE_BIRDING: load the Birding edition config fields. ebirdKey is non-const so it can be
        // masked before sending to the client (same masked-value guard on save).
        String ebirdKey = HtmlEscape(prefs.getString("ebird-key", ""));
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        const String bdRadius = HtmlEscape(prefs.isKey("bd-radius-km") ? prefs.getString("bd-radius-km", "25") : "25");
        const String bdBack = HtmlEscape(prefs.isKey("bd-back-days") ? prefs.getString("bd-back-days", "7") : "7");
        const String bdTargets = HtmlEscape(prefs.getString("bd-targets", ""));
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        const String alertNotable = HtmlEscape(prefs.isKey("bd-alert-rare") ? prefs.getString("bd-alert-rare", "true") : "true");
        const String alertTarget = HtmlEscape(prefs.isKey("bd-alert-target") ? prefs.getString("bd-alert-target", "true") : "true");
        const String autoDimEnabled = HtmlEscape(prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
#elif defined(FEATURE_FISHING)
        // FEATURE_FISHING: load the Reelscope config fields. All feeds are keyless (no masked secret).
        const String fiWater = HtmlEscape(prefs.isKey("fi-water") ? prefs.getString("fi-water", "both") : "both");
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        const String fiUsgs = HtmlEscape(prefs.getString("fi-usgs", ""));
        const String fiNoaa = HtmlEscape(prefs.getString("fi-noaa", ""));
        const String fiBuoy = HtmlEscape(prefs.getString("fi-buoy", ""));
        const String fiUnits = HtmlEscape(prefs.isKey("fi-units") ? prefs.getString("fi-units", "imperial") : "imperial");
        const String fiBaseUrl = HtmlEscape(prefs.getString("fi-base-url", ""));
        const String fiTz = HtmlEscape(prefs.isKey("fi-tz-offset") ? prefs.getString("fi-tz-offset", "0") : "0");
        const String fiFlowCfs = HtmlEscape(prefs.getString("fi-flow-cfs", ""));
        const String fiTempLo = HtmlEscape(prefs.getString("fi-temp-lo", ""));
        const String fiTempHi = HtmlEscape(prefs.getString("fi-temp-hi", ""));
        const String vTide = HtmlEscape(prefs.isKey("fi-v-tide") ? prefs.getString("fi-v-tide", "true") : "true");
        const String vFlow = HtmlEscape(prefs.isKey("fi-v-flow") ? prefs.getString("fi-v-flow", "true") : "true");
        const String vTemp = HtmlEscape(prefs.isKey("fi-v-temp") ? prefs.getString("fi-v-temp", "true") : "true");
        const String vSolunar = HtmlEscape(prefs.isKey("fi-v-solunar") ? prefs.getString("fi-v-solunar", "true") : "true");
        const String vWeather = HtmlEscape(prefs.isKey("fi-v-weather") ? prefs.getString("fi-v-weather", "true") : "true");
        const String vMoon = HtmlEscape(prefs.isKey("fi-v-moon") ? prefs.getString("fi-v-moon", "true") : "true");
        const String vCatch = HtmlEscape(prefs.isKey("fi-v-catch") ? prefs.getString("fi-v-catch", "true") : "true");
        const String vClock = HtmlEscape(prefs.isKey("fi-v-clock") ? prefs.getString("fi-v-clock", "true") : "true");
        const String aFlow = HtmlEscape(prefs.isKey("fi-a-flow") ? prefs.getString("fi-a-flow", "false") : "false");
        const String aTemp = HtmlEscape(prefs.isKey("fi-a-temp") ? prefs.getString("fi-a-temp", "false") : "false");
        const String aSolunar = HtmlEscape(prefs.isKey("fi-a-solunar") ? prefs.getString("fi-a-solunar", "false") : "false");
        const String aBaro = HtmlEscape(prefs.isKey("fi-a-baro") ? prefs.getString("fi-a-baro", "false") : "false");
        const String aTide = HtmlEscape(prefs.isKey("fi-a-tide") ? prefs.getString("fi-a-tide", "false") : "false");
        const String fiChime = HtmlEscape(prefs.isKey("fi-chime") ? prefs.getString("fi-chime", "false") : "false");
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        const String autoDimEnabled = HtmlEscape(prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
#elif defined(FEATURE_CLAUDESCOPE)
        // FEATURE_CLAUDESCOPE: load the Claudescope config fields. The sidecar URL is required and
        // empty by default (no baked-in backend); all feeds are keyless (no masked secret).
        const String clBaseUrl = HtmlEscape(prefs.getString("cl-base-url", ""));
        const String latitude = HtmlEscape(prefs.getString("latitude", ""));
        const String longitude = HtmlEscape(prefs.getString("longitude", ""));
        // default the local-clock offset to the nominal zone from longitude (15 deg/hour)
        const String clTz = prefs.isKey("cl-tz-offset")
            ? prefs.getString("cl-tz-offset", "0")
            : String((int)round(longitude.toFloat() / 15.0));
        const String clSessionPct = HtmlEscape(prefs.isKey("cl-session-pct") ? prefs.getString("cl-session-pct", "80") : "80");
        const String clWeekPct = HtmlEscape(prefs.isKey("cl-week-pct") ? prefs.getString("cl-week-pct", "80") : "80");
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        const String alertSession = HtmlEscape(prefs.isKey("cl-alert-sess") ? prefs.getString("cl-alert-sess", "true") : "true");
        const String alertWeek = HtmlEscape(prefs.isKey("cl-alert-week") ? prefs.getString("cl-alert-week", "true") : "true");
        const String autoDimEnabled = HtmlEscape(prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
#elif defined(FEATURE_SPEED)
        // FEATURE_SPEED: load the Speedscope config fields. The camera endpoints are keyless (no masked secret).
        const String scHost = HtmlEscape(prefs.getString("sc-host", ""));
        const String scBaseUrl = prefs.getString("sc-base-url", "");
        const String scLimit = prefs.getString("sc-limit", "");
        const String scAlert = prefs.getString("sc-alert-speed", "");
        const String scTz = prefs.isKey("sc-tz-offset") ? prefs.getString("sc-tz-offset", "0") : "0";
        const String latitude = prefs.getString("latitude", "");
        const String longitude = prefs.getString("longitude", "");
        const String vLast = prefs.isKey("sc-v-last") ? prefs.getString("sc-v-last", "true") : "true";
        const String vLive = prefs.isKey("sc-v-live") ? prefs.getString("sc-v-live", "true") : "true";
        const String vList = prefs.isKey("sc-v-list") ? prefs.getString("sc-v-list", "true") : "true";
        const String vStats = prefs.isKey("sc-v-stats") ? prefs.getString("sc-v-stats", "true") : "true";
        const String vDevice = prefs.isKey("sc-v-device") ? prefs.getString("sc-v-device", "true") : "true";
        const String vClock = prefs.isKey("sc-v-clock") ? prefs.getString("sc-v-clock", "true") : "true";
        const String aSpeeder = prefs.isKey("sc-a-speeder") ? prefs.getString("sc-a-speeder", "false") : "false";
        const String aRecord = prefs.isKey("sc-a-record") ? prefs.getString("sc-a-record", "false") : "false";
        const String aOffline = prefs.isKey("sc-a-offline") ? prefs.getString("sc-a-offline", "false") : "false";
        const String ntfyTopic = prefs.getString("ntfy-topic", "");
        const String autoDimEnabled = prefs.isKey("autodim") ? prefs.getString("autodim", "true") : "true";
        const String brightness = prefs.getString("brightness", "255");
#endif
        prefs.end();

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
        // mask secrets before sending to client
        std::fill(openskySecret.begin(), openskySecret.end(), '*');
        std::fill(mqttPass.begin(), mqttPass.end(), '*');
#ifdef FEATURE_CLOUD_FEED
        std::fill(cloudKeyCfg.begin(), cloudKeyCfg.end(), '*');
#endif
#elif defined(FEATURE_EAM)
        // mask the OpenSky secret before sending to the client (same masked-value guard on save)
        std::fill(openskySecret.begin(), openskySecret.end(), '*');
#elif defined(FEATURE_BIRDING)
        // mask the eBird key before sending to the client (same masked-value guard on save)
        std::fill(ebirdKey.begin(), ebirdKey.end(), '*');
#endif
        // FEATURE_SPACE has no secret fields yet (no API keys until the key-gated screens land).

        // template processor called once per %PLACEHOLDER% token found in CONFIG_HTML.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, latitude, longitude, radius, radiusUnit, openskyClientId, openskySecret, dataSource, localUrl, scanlineEnabled, fadeEnabled, infoTextEnabled, triangleEnabled, trailEnabled, altColorEnabled, highlightEnabled, autoDimEnabled, brightness, tzOffset, radarUp, watchlist, ntfyTopic, milShow, milAlert, heliShow, spcShow, milVisual, emgVisual, visualNight, logbookOn, lookupOn, lookupAlert, lookupDist, mqttOn, mqttHost, mqttPort, mqttUser, mqttPass, mqttBase, mqttDisco, infoFieldsHtml
#ifdef FEATURE_CLOUD_FEED
             , cloudUrlCfg, cloudKeyCfg
#endif
            ]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "RADIUS")         return radius;
                if (var == "RADIUS_UNIT_KM") return radiusUnit == "mi" ? "" : "selected";
                if (var == "RADIUS_UNIT_MI") return radiusUnit == "mi" ? "selected" : "";
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
#ifdef FEATURE_CLOUD_FEED
                // cloud is the default: anything that isn't an explicit opensky/local
                // choice (including the never-saved empty) selects it.
                if (var == "DATASRC_CLOUD")   return (dataSource == "opensky" || dataSource == "local") ? "" : "selected";
                if (var == "DATASRC_OPENSKY") return dataSource == "opensky" ? "selected" : "";
                if (var == "CLOUD_URL")       return cloudUrlCfg;
                if (var == "CLOUD_KEY")       return cloudKeyCfg;
#else
                if (var == "DATASRC_OPENSKY") return dataSource == "local" ? "" : "selected";
#endif
                if (var == "DATASRC_LOCAL")   return dataSource == "local" ? "selected" : "";
                if (var == "LOCAL_URL")      return localUrl;
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "FADE")           return fadeEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                if (var == "TRAIL")          return trailEnabled == "true" ? "checked" : "";
                if (var == "ALTCOLOR")       return altColorEnabled == "true" ? "checked" : "";
                if (var == "HIGHLIGHT")      return highlightEnabled == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "TZ_OFFSET")      return tzOffset;
                if (var == "RADAR_UP")       return radarUp;
                if (var == "WATCHLIST")      return watchlist;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "MIL_SHOW")       return milShow == "true" ? "checked" : "";
                if (var == "MIL_ALERT")      return milAlert == "true" ? "checked" : "";
                if (var == "HELI_SHOW")      return heliShow == "true" ? "checked" : "";
                if (var == "SPC_SHOW")       return spcShow == "true" ? "checked" : "";
                // visual-alert selects: the OFF/RING branches also catch legacy/unknown
                // values, so each select always renders exactly one option selected
                if (var == "MILVIS_OFF")     return (milVisual == "ring" || milVisual == "flash") ? "" : "selected";
                if (var == "MILVIS_RING")    return milVisual == "ring" ? "selected" : "";
                if (var == "MILVIS_FLASH")   return milVisual == "flash" ? "selected" : "";
                if (var == "EMGVIS_OFF")     return emgVisual == "off" ? "selected" : "";
                if (var == "EMGVIS_RING")    return (emgVisual == "off" || emgVisual == "flash") ? "" : "selected";
                if (var == "EMGVIS_FLASH")   return emgVisual == "flash" ? "selected" : "";
                if (var == "VISUAL_NIGHT")   return visualNight == "true" ? "checked" : "";
                if (var == "LOGBOOK")        return logbookOn == "true" ? "checked" : "";
                if (var == "LOOKUP")         return lookupOn == "true" ? "checked" : "";
                if (var == "LOOKUP_ALERT")   return lookupAlert == "true" ? "checked" : "";
                if (var == "LOOKUP_DIST")    return lookupDist;
                if (var == "MQTT")           return mqttOn == "true" ? "checked" : "";
                if (var == "MQTT_HOST")      return mqttHost;
                if (var == "MQTT_PORT")      return mqttPort;
                if (var == "MQTT_USER")      return mqttUser;
                if (var == "MQTT_PASS")      return mqttPass;
                if (var == "MQTT_BASE")      return mqttBase;
                if (var == "MQTT_DISCO")     return mqttDisco == "true" ? "checked" : "";
                if (var == "INFO_FIELDS")    return infoFieldsHtml;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_EAM)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, eamBaseUrl, latitude, longitude, abncpSource, openskyClientId, openskySecret, abncpWatch, ntfyTopic, alertNew, alertTempo, alertAbncp, alertSpace, eamPalette, eamRefresh, colonBlink, autoDimEnabled, brightness, eamScreens]
            (const String& var) -> String {
                if (var == "EAM_BASE_URL")   return eamBaseUrl;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "ABNCP_BACKEND")  return abncpSource == "opensky" ? "" : "selected";
                if (var == "ABNCP_OPENSKY")  return abncpSource == "opensky" ? "selected" : "";
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
                if (var == "ABNCP_WATCH")    return abncpWatch;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "ALERT_NEW")      return alertNew == "true" ? "checked" : "";
                if (var == "ALERT_TEMPO")    return alertTempo == "true" ? "checked" : "";
                if (var == "ALERT_ABNCP")    return alertAbncp == "true" ? "checked" : "";
                if (var == "ALERT_SPACE")    return alertSpace == "true" ? "checked" : "";
                if (var == "PAL_GREEN")      return eamPalette == "amber" ? "" : "selected";
                if (var == "PAL_AMBER")      return eamPalette == "amber" ? "selected" : "";
                if (var == "RR_NORMAL")      return eamRefresh == "relaxed" || eamRefresh == "battery" ? "" : "selected";
                if (var == "RR_RELAXED")     return eamRefresh == "relaxed" ? "selected" : "";
                if (var == "RR_BATTERY")     return eamRefresh == "battery" ? "selected" : "";
                if (var == "COLON_BLINK")    return colonBlink == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "EAM_SCREENS")    return eamScreens;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_SPACE)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, spaceBaseUrl, latitude, longitude, ntfyTopic, alertLaunch, alertAurora, alertFlare, alertIss, alertDsn, alertAsteroid, chimeOnAlert, autoDimEnabled, brightness, spaceScreensHtml]
            (const String& var) -> String {
                if (var == "SPACE_BASE_URL") return spaceBaseUrl;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "AL_LAUNCH")      return alertLaunch == "true" ? "checked" : "";
                if (var == "AL_AURORA")      return alertAurora == "true" ? "checked" : "";
                if (var == "AL_FLARE")       return alertFlare == "true" ? "checked" : "";
                if (var == "AL_ISS")         return alertIss == "true" ? "checked" : "";
                if (var == "AL_DSN")         return alertDsn == "true" ? "checked" : "";
                if (var == "AL_ASTEROID")    return alertAsteroid == "true" ? "checked" : "";
                if (var == "AL_CHIME")       return chimeOnAlert == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "SPACE_SCREENS_HTML") return spaceScreensHtml;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_SEISMIC)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, seBaseUrl, latitude, longitude, seMinMag, seRadius, seBigMag, seNearMag, ntfyTopic, alertBig, alertNear, alertTsunami, autoDimEnabled, brightness]
            (const String& var) -> String {
                if (var == "SE_BASE_URL")    return seBaseUrl;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "SE_MIN_MAG")     return seMinMag;
                if (var == "SE_RADIUS")      return seRadius;
                if (var == "SE_BIG_MAG")     return seBigMag;
                if (var == "SE_NEAR_MAG")    return seNearMag;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "AL_BIG")         return alertBig == "true" ? "checked" : "";
                if (var == "AL_NEAR")        return alertNear == "true" ? "checked" : "";
                if (var == "AL_TSUNAMI")     return alertTsunami == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_BIRDING)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, ebirdKey, latitude, longitude, bdRadius, bdBack, bdTargets, ntfyTopic, alertNotable, alertTarget, autoDimEnabled, brightness]
            (const String& var) -> String {
                if (var == "EBIRD_KEY")      return ebirdKey;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "BD_RADIUS")      return bdRadius;
                if (var == "BD_BACK")        return bdBack;
                if (var == "BD_TARGETS")     return bdTargets;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "AL_NOTABLE")     return alertNotable == "true" ? "checked" : "";
                if (var == "AL_TARGET")      return alertTarget == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_FISHING)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, fiWater, latitude, longitude, fiUsgs, fiNoaa, fiBuoy, fiUnits, fiBaseUrl, fiTz, fiFlowCfs, fiTempLo, fiTempHi, vTide, vFlow, vTemp, vSolunar, vWeather, vMoon, vCatch, vClock, aFlow, aTemp, aSolunar, aBaro, aTide, fiChime, ntfyTopic, autoDimEnabled, brightness]
            (const String& var) -> String {
                if (var == "FI_WATER_BOTH")  return (fiWater == "fresh" || fiWater == "salt") ? "" : "selected";
                if (var == "FI_WATER_FRESH") return fiWater == "fresh" ? "selected" : "";
                if (var == "FI_WATER_SALT")  return fiWater == "salt" ? "selected" : "";
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "FI_USGS")        return fiUsgs;
                if (var == "FI_NOAA")        return fiNoaa;
                if (var == "FI_BUOY")        return fiBuoy;
                if (var == "FI_UNITS_IMP")   return fiUnits == "metric" ? "" : "selected";
                if (var == "FI_UNITS_MET")   return fiUnits == "metric" ? "selected" : "";
                if (var == "FI_BASE_URL")    return fiBaseUrl;
                if (var == "FI_TZ")          return fiTz;
                if (var == "FI_FLOW_CFS")    return fiFlowCfs;
                if (var == "FI_TEMP_LO")     return fiTempLo;
                if (var == "FI_TEMP_HI")     return fiTempHi;
                if (var == "FI_V_TIDE")      return vTide == "true" ? "checked" : "";
                if (var == "FI_V_FLOW")      return vFlow == "true" ? "checked" : "";
                if (var == "FI_V_TEMP")      return vTemp == "true" ? "checked" : "";
                if (var == "FI_V_SOLUNAR")   return vSolunar == "true" ? "checked" : "";
                if (var == "FI_V_WEATHER")   return vWeather == "true" ? "checked" : "";
                if (var == "FI_V_MOON")      return vMoon == "true" ? "checked" : "";
                if (var == "FI_V_CATCH")     return vCatch == "true" ? "checked" : "";
                if (var == "FI_V_CLOCK")     return vClock == "true" ? "checked" : "";
                if (var == "FI_A_FLOW")      return aFlow == "true" ? "checked" : "";
                if (var == "FI_A_TEMP")      return aTemp == "true" ? "checked" : "";
                if (var == "FI_A_SOLUNAR")   return aSolunar == "true" ? "checked" : "";
                if (var == "FI_A_BARO")      return aBaro == "true" ? "checked" : "";
                if (var == "FI_A_TIDE")      return aTide == "true" ? "checked" : "";
                if (var == "FI_CHIME")       return fiChime == "true" ? "checked" : "";
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_CLAUDESCOPE)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, clBaseUrl, latitude, longitude, clTz, clSessionPct, clWeekPct, ntfyTopic, alertSession, alertWeek, autoDimEnabled, brightness]
            (const String& var) -> String {
                if (var == "CL_BASE_URL")    return clBaseUrl;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "CL_TZ")          return clTz;
                if (var == "CL_SESSION_PCT") return clSessionPct;
                if (var == "CL_WEEK_PCT")    return clWeekPct;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "AL_SESSION")     return alertSession == "true" ? "checked" : "";
                if (var == "AL_WEEK")        return alertWeek == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#elif defined(FEATURE_SPEED)
        AsyncWebServerResponse* response = request->beginResponse(
            200, "text/html",
            (const uint8_t*)CONFIG_HTML, sizeof(CONFIG_HTML) - 1,
            [deviceName, deviceIp, wifiRssi, scHost, scBaseUrl, scLimit, scAlert, scTz, latitude, longitude, vLast, vLive, vList, vStats, vDevice, vClock, aSpeeder, aRecord, aOffline, ntfyTopic, autoDimEnabled, brightness]
            (const String& var) -> String {
                if (var == "SC_HOST")        return scHost;
                if (var == "SC_BASE_URL")    return scBaseUrl;
                if (var == "SC_LIMIT")       return scLimit;
                if (var == "SC_ALERT")       return scAlert;
                if (var == "SC_TZ")          return scTz;
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "SC_V_LAST")      return vLast == "true" ? "checked" : "";
                if (var == "SC_V_LIVE")      return vLive == "true" ? "checked" : "";
                if (var == "SC_V_LIST")      return vList == "true" ? "checked" : "";
                if (var == "SC_V_STATS")     return vStats == "true" ? "checked" : "";
                if (var == "SC_V_DEVICE")    return vDevice == "true" ? "checked" : "";
                if (var == "SC_V_CLOCK")     return vClock == "true" ? "checked" : "";
                if (var == "SC_A_SPEEDER")   return aSpeeder == "true" ? "checked" : "";
                if (var == "SC_A_RECORD")    return aRecord == "true" ? "checked" : "";
                if (var == "SC_A_OFFLINE")   return aOffline == "true" ? "checked" : "";
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                if (var == "DEVICE_NAME")    return deviceName;
                if (var == "DEVICE_IP")      return deviceIp;
                if (var == "WIFI_RSSI")      return wifiRssi;
                return "";
            }
        );
#endif
        // never cache the config page: a stale copy (e.g. predating a new option)
        // would hide controls and, once submitted, silently clear the missing fields
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
        }
    );

    // Handle save submission to web server
    server.on("/save", HTTP_POST, [&](AsyncWebServerRequest* request) {
        if (RejectCrossOrigin(request)) return; // CSRF guard (see RejectCrossOrigin)
        Serial.println("[POST] Handling form submission to config web server...");

        Preferences prefs;

        // Form field names double as NVS keys, and NVS caps key names at 15 chars
        // (NVS_KEY_NAME_MAX_SIZE - 1). Longer keys make putString/isKey fail
        // SILENTLY (the toggle just never sticks), so keep every name <= 15.

        // safe parameter retrieval helper lambda
        auto TrySaveParam = [request, &prefs](const char* paramName) {
            const auto* param = request->getParam(paramName, true);
            if (param == nullptr)
                return false;

            prefs.putString(paramName, param->value());
            return true;
            };

        prefs.begin("config", false);

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("radius");
        TrySaveParam("radius-unit");
        TrySaveParam("brightness");
        TrySaveParam("tz-offset");
        TrySaveParam("watchlist");
        TrySaveParam("ntfy-topic");
        TrySaveParam("opensky-id");
        TrySaveParam("data-source");
        TrySaveParam("local-url");
#ifdef FEATURE_CLOUD_FEED
        TrySaveParam("cloud-url");
        // cloud key: same masked-value handling as the OpenSky secret (the GET
        // serves it as asterisks; only a genuinely new value overwrites)
        const auto* cloudKeyParam = request->getParam("cloud-key", true);
        if (cloudKeyParam != nullptr) {
            const String& key = cloudKeyParam->value();
            if (!IsMaskedValue(key))
                prefs.putString("cloud-key", key);
        }
#endif
        TrySaveParam("lookup-dist");
        TrySaveParam("radar-up");
        TrySaveParam("mil-visual");
        TrySaveParam("emg-visual");
        TrySaveParam("mqtt-host");
        TrySaveParam("mqtt-port");
        TrySaveParam("mqtt-user");
        TrySaveParam("mqtt-base");

        const auto* param = request->getParam("opensky-secret", true);
        if (param != nullptr) {
            const String& secret = param->value();
            if (!IsMaskedValue(secret)) { // Special handling for secret: don't overwrite with masked value
                prefs.putString("opensky-secret", secret);
            }
        }

        // MQTT password: same masked-value handling as the OpenSky secret
        const auto* mqttPassParam = request->getParam("mqtt-pass", true);
        if (mqttPassParam != nullptr) {
            const String& pass = mqttPassParam->value();
            if (!IsMaskedValue(pass))
                prefs.putString("mqtt-pass", pass);
        }

        prefs.putString("scanline", request->hasParam("scanline", true) ? "true" : "false");
        prefs.putString("fade", request->hasParam("fade", true) ? "true" : "false");
        prefs.putString("triangle", request->hasParam("triangle", true) ? "true" : "false");
        prefs.putString("trail", request->hasParam("trail", true) ? "true" : "false");
        prefs.putString("altcolor", request->hasParam("altcolor", true) ? "true" : "false");
        prefs.putString("highlight", request->hasParam("highlight", true) ? "true" : "false");
        prefs.putString("autodim", request->hasParam("autodim", true) ? "true" : "false");
        prefs.putString("infotext", request->hasParam("infotext", true) ? "true" : "false");
        prefs.putString("mil-show", request->hasParam("mil-show", true) ? "true" : "false");
        prefs.putString("mil-alert", request->hasParam("mil-alert", true) ? "true" : "false");
        prefs.putString("visual-night", request->hasParam("visual-night", true) ? "true" : "false");
        prefs.putString("heli-show", request->hasParam("heli-show", true) ? "true" : "false");
        prefs.putString("spc-show", request->hasParam("spc-show", true) ? "true" : "false");
        prefs.putString("logbook", request->hasParam("logbook", true) ? "true" : "false");
        prefs.putString("lookup", request->hasParam("lookup", true) ? "true" : "false");
        prefs.putString("lookup-alert", request->hasParam("lookup-alert", true) ? "true" : "false");
        prefs.putString("mqtt", request->hasParam("mqtt", true) ? "true" : "false");
        prefs.putString("mqtt-disco", request->hasParam("mqtt-disco", true) ? "true" : "false");

        // an unchecked checkbox isn't sent in the form body, so hasParam() is the
        // on/off signal for each individual info field
        for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i) {
            const char* key = AIRCRAFT_INFO_FIELDS[i].key;
            prefs.putString(key, request->hasParam(key, true) ? "true" : "false");
        }
#elif defined(FEATURE_EAM)
        // FEATURE_EAM: persist the EAM config fields.
        TrySaveParam("eam-base-url");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("abncp-source");
        TrySaveParam("opensky-id");
        TrySaveParam("abncp-watch");
        TrySaveParam("ntfy-topic");
        TrySaveParam("eam-palette");
        TrySaveParam("eam-refresh");
        TrySaveParam("brightness");
        TrySaveParam("eam-screens");

        // OpenSky secret: don't overwrite the stored value with the masked placeholder
        const auto* eamSecret = request->getParam("opensky-secret", true);
        if (eamSecret != nullptr) {
            const String& secret = eamSecret->value();
            if (!IsMaskedValue(secret))
                prefs.putString("opensky-secret", secret);
        }

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        prefs.putString("eam-alert-new", request->hasParam("eam-alert-new", true) ? "true" : "false");
        prefs.putString("eam-alert-tempo", request->hasParam("eam-alert-tempo", true) ? "true" : "false");
        prefs.putString("eam-alert-abncp", request->hasParam("eam-alert-abncp", true) ? "true" : "false");
        prefs.putString("eam-alert-space", request->hasParam("eam-alert-space", true) ? "true" : "false");
        prefs.putString("eam-colon-blink", request->hasParam("eam-colon-blink", true) ? "true" : "false");
        prefs.putString("autodim", request->hasParam("autodim", true) ? "true" : "false");
#elif defined(FEATURE_SPACE)
        // FEATURE_SPACE: persist the Spacescope config fields.
        TrySaveParam("space-base-url");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // Screens: rebuild the CSV from the per-screen checkboxes (canonical order). Unchecked boxes
        // are absent from the body, so hasParam() is the on/off signal. All-off saves "clock" so the
        // device still shows the idle clock instead of falling back to "empty CSV = all on".
        {
            String csv;
            for (size_t i = 0; i < SPACE_SCREEN_DEF_COUNT; ++i) {
                if (request->hasParam(String("scr-") + SPACE_SCREEN_DEFS[i].id, true)) {
                    if (csv.length()) csv += ",";
                    csv += SPACE_SCREEN_DEFS[i].id;
                }
            }
            prefs.putString("space-screens", csv.isEmpty() ? String("clock") : csv);
        }

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        prefs.putString("sp-alert-launch", request->hasParam("sp-alert-launch", true) ? "true" : "false");
        prefs.putString("sp-alert-aurora", request->hasParam("sp-alert-aurora", true) ? "true" : "false");
        prefs.putString("sp-alert-flare", request->hasParam("sp-alert-flare", true) ? "true" : "false");
        prefs.putString("sp-alert-iss", request->hasParam("sp-alert-iss", true) ? "true" : "false");
        prefs.putString("sp-alert-dsn", request->hasParam("sp-alert-dsn", true) ? "true" : "false");
        prefs.putString("sp-alert-neo", request->hasParam("sp-alert-neo", true) ? "true" : "false");
        prefs.putString("sp-chime", request->hasParam("sp-chime", true) ? "true" : "false");
        prefs.putString("autodim", request->hasParam("autodim", true) ? "true" : "false");
#elif defined(FEATURE_SEISMIC)
        // FEATURE_SEISMIC: persist the Seismic edition config fields.
        TrySaveParam("se-base-url");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("se-min-mag");
        TrySaveParam("se-radius-km");
        TrySaveParam("se-big-mag");
        TrySaveParam("se-near-mag");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        prefs.putString("se-alert-big", request->hasParam("se-alert-big", true) ? "true" : "false");
        prefs.putString("se-alert-near", request->hasParam("se-alert-near", true) ? "true" : "false");
        prefs.putString("se-alert-tsnmi", request->hasParam("se-alert-tsnmi", true) ? "true" : "false");
        prefs.putString("autodim", request->hasParam("autodim", true) ? "true" : "false");
#elif defined(FEATURE_BIRDING)
        // FEATURE_BIRDING: persist the Birding edition config fields.
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("bd-radius-km");
        TrySaveParam("bd-back-days");
        TrySaveParam("bd-targets");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");
        prefs.putString("bd-alert-rare", request->hasParam("bd-alert-rare", true) ? "true" : "false");
        prefs.putString("bd-alert-target", request->hasParam("bd-alert-target", true) ? "true" : "false");
        prefs.putString("autodim", request->hasParam("autodim", true) ? "true" : "false");

        // eBird key: don't overwrite the stored value with the masked placeholder
        const auto* ebirdParam = request->getParam("ebird-key", true);
        if (ebirdParam != nullptr) {
            const String& k = ebirdParam->value();
            if (!IsMaskedValue(k))
                prefs.putString("ebird-key", k);
        }
#elif defined(FEATURE_FISHING)
        // FEATURE_FISHING: persist the Reelscope config fields. All feeds are keyless (no secret).
        TrySaveParam("fi-water");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("fi-usgs");
        TrySaveParam("fi-noaa");
        TrySaveParam("fi-buoy");
        TrySaveParam("fi-units");
        TrySaveParam("fi-base-url");
        TrySaveParam("fi-tz-offset");
        TrySaveParam("fi-flow-cfs");
        TrySaveParam("fi-temp-lo");
        TrySaveParam("fi-temp-hi");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        prefs.putString("fi-v-tide",    request->hasParam("fi-v-tide", true) ? "true" : "false");
        prefs.putString("fi-v-flow",    request->hasParam("fi-v-flow", true) ? "true" : "false");
        prefs.putString("fi-v-temp",    request->hasParam("fi-v-temp", true) ? "true" : "false");
        prefs.putString("fi-v-solunar", request->hasParam("fi-v-solunar", true) ? "true" : "false");
        prefs.putString("fi-v-weather", request->hasParam("fi-v-weather", true) ? "true" : "false");
        prefs.putString("fi-v-moon",    request->hasParam("fi-v-moon", true) ? "true" : "false");
        prefs.putString("fi-v-catch",   request->hasParam("fi-v-catch", true) ? "true" : "false");
        prefs.putString("fi-v-clock",   request->hasParam("fi-v-clock", true) ? "true" : "false");
        prefs.putString("fi-a-flow",    request->hasParam("fi-a-flow", true) ? "true" : "false");
        prefs.putString("fi-a-temp",    request->hasParam("fi-a-temp", true) ? "true" : "false");
        prefs.putString("fi-a-solunar", request->hasParam("fi-a-solunar", true) ? "true" : "false");
        prefs.putString("fi-a-baro",    request->hasParam("fi-a-baro", true) ? "true" : "false");
        prefs.putString("fi-a-tide",    request->hasParam("fi-a-tide", true) ? "true" : "false");
        prefs.putString("fi-chime",     request->hasParam("fi-chime", true) ? "true" : "false");
        prefs.putString("autodim",      request->hasParam("autodim", true) ? "true" : "false");
#elif defined(FEATURE_CLAUDESCOPE)
        // FEATURE_CLAUDESCOPE: persist the Claudescope config fields. All feeds are keyless -- no
        // masked secret to guard (the OAuth token lives on the sidecar host, never here).
        TrySaveParam("cl-base-url");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("cl-tz-offset");
        TrySaveParam("cl-session-pct");
        TrySaveParam("cl-week-pct");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        prefs.putString("cl-alert-sess", request->hasParam("cl-alert-sess", true) ? "true" : "false");
        prefs.putString("cl-alert-week", request->hasParam("cl-alert-week", true) ? "true" : "false");
        prefs.putString("autodim", request->hasParam("autodim", true) ? "true" : "false");
#elif defined(FEATURE_SPEED)
        // FEATURE_SPEED: persist the Speedscope config fields. The camera endpoints are keyless (no secret).
        TrySaveParam("sc-host");
        TrySaveParam("sc-base-url");
        TrySaveParam("sc-limit");
        TrySaveParam("sc-alert-speed");
        TrySaveParam("sc-tz-offset");
        TrySaveParam("latitude");
        TrySaveParam("longitude");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        prefs.putString("sc-v-last",    request->hasParam("sc-v-last", true) ? "true" : "false");
        prefs.putString("sc-v-live",    request->hasParam("sc-v-live", true) ? "true" : "false");
        prefs.putString("sc-v-list",    request->hasParam("sc-v-list", true) ? "true" : "false");
        prefs.putString("sc-v-stats",   request->hasParam("sc-v-stats", true) ? "true" : "false");
        prefs.putString("sc-v-device",  request->hasParam("sc-v-device", true) ? "true" : "false");
        prefs.putString("sc-v-clock",   request->hasParam("sc-v-clock", true) ? "true" : "false");
        prefs.putString("sc-a-speeder", request->hasParam("sc-a-speeder", true) ? "true" : "false");
        prefs.putString("sc-a-record",  request->hasParam("sc-a-record", true) ? "true" : "false");
        prefs.putString("sc-a-offline", request->hasParam("sc-a-offline", true) ? "true" : "false");
        prefs.putString("autodim",      request->hasParam("autodim", true) ? "true" : "false");
#endif
        prefs.end();

        // No reboot: flag the change and let loop() re-read settings on the main
        // task. NVS is already committed by the putString() calls above, so the
        // reload will see the new values.
        configChanged = true;
        request->send(200, "text/html", "Saved - settings applied.");
        }
    );

    // Forget WiFi credentials and reboot into the WiFiManager setup portal. The
    // response is sent first; the restart is deferred a moment so it can flush.
    server.on("/reset-wifi", HTTP_POST, [&](AsyncWebServerRequest* request) {
        if (RejectCrossOrigin(request)) return; // CSRF guard (see RejectCrossOrigin)
        Serial.println("[POST] Clearing WiFi credentials and restarting...");
        request->send(200, "text/html", "WiFi cleared - restarting into setup mode. Reconnect to the device's setup network.");
        wifiResetRequested = true;
        }
    );

#ifdef FEATURE_EAM
    // Logbook export (firmware-only; no backend). Serves the persisted EAM/codeword log straight
    // from NVS as a file download. Read-only, so it's safe from the async task alongside the
    // loop-task logbook writer.
    server.on("/eam-log.csv", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* r = request->beginResponse(200, "text/csv", EamLogbook::ExportCsv());
        r->addHeader("Content-Disposition", "attachment; filename=\"eam-log.csv\"");
        r->addHeader("Cache-Control", "no-store");
        request->send(r);
    });
    server.on("/eam-log.json", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* r = request->beginResponse(200, "application/json", EamLogbook::ExportJson());
        r->addHeader("Content-Disposition", "attachment; filename=\"eam-log.json\"");
        r->addHeader("Cache-Control", "no-store");
        request->send(r);
    });
#endif

    server.begin();
}

bool ConfigurationWebServer::ConsumeConfigChanged()
{
    if (!configChanged)
        return false;
    configChanged = false;
    return true;
}

bool ConfigurationWebServer::ConsumeWifiReset()
{
    if (!wifiResetRequested)
        return false;
    wifiResetRequested = false;
    return true;
}

const String ConfigurationWebServer::GetStoredString(const char* key)
{
    Preferences prefs;
    prefs.begin("config", true);
    // isKey() probes without logging; calling getString() on a missing key would spam
    // "nvs_get_str ... NOT_FOUND" on every call (e.g. every frame for "scanline") until
    // the user first saves settings. Returns the same "" default as before when absent.
    const String value = prefs.isKey(key) ? prefs.getString(key, "") : String();
    prefs.end();
    return value;
}