#include "ConfigVocabulary.h"
#include "ConfigurationWebServer.h"
#include "NtfyTopic.h" // the generated private topic (spec 14.1)
#include "ConfigMigration.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <lwip/tcpip.h>            // LOCK_TCPIP_CORE / UNLOCK_TCPIP_CORE
#include <lwip/priv/tcp_priv.h>    // tcp_listen_pcbs -- the LISTEN pcb list itself
#include <Preferences.h>
#include <memory>             // shared_ptr: keeps the chunked logbook stream alive across fills
#include "DeviceIdentity.h"
#include "OtaUpdater.h"
#include "BuildIdentity.h"
#include "CoordParse.h"       // forgiving lat/lon parsing for /save (see the header)
#ifdef FEATURE_CLOUD_FEED
#include "CloudFeed.h"        // NormalizeBaseUrl + the CLOUD_FEED_BASE default, for the leaderboard link
#endif
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
#include "AircraftInfoFields.h"   // radar-only; filtered out of the FEATURE_EAM/FEATURE_SPACE builds
#include "FrameBuffer.h"
#include <esp_heap_caps.h>
#include <memory>
#include "Logbook.h"              // radar-only; serves the spotting lifelist as /logbook.json
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
//    <style>:root{--ink:..;--line:..;--dim:..;--btn:..;--text:..}</style>
//    (--ink headings/links/accent, --text label + field text, --line borders/frames,
//     --dim hint text, --btn save button). THREE TONES: near-white labels, green
//     headings, muted hints -- all three >= 4.5:1 on #111827.
#define CONFIG_SHELL_CSS \
    R"(<style>)" \
    R"(*{box-sizing:border-box})" \
    R"(body{margin:0;padding:1rem;background:#111827;color:var(--text);font-family:ui-monospace,Menlo,Consolas,monospace;font-size:1rem;min-height:100vh})" \
    R"(a{color:var(--ink)})" \
    R"(.wrap{max-width:42rem;margin:0 auto;border:1px solid var(--line);padding:1rem})" \
    R"(legend{padding:0 .5rem;color:var(--ink)})" \
    R"(form{display:flex;flex-direction:column;gap:1rem})" \
    R"(fieldset,details{border:1px solid var(--line);padding:.75rem;margin:0;min-width:0})" \
    R"(summary{cursor:pointer;-webkit-user-select:none;user-select:none})" \
    R"(details[open]>summary{margin-bottom:.75rem})" \
    R"(summary input[type=checkbox]{margin-left:.4rem;vertical-align:-.15rem})" \
    R"(.field{display:flex;flex-direction:column;gap:.4rem})" \
    R"(.field>span:first-child{flex:none})" \
    R"(.stack{display:flex;flex-direction:column;gap:.75rem})" \
    R"(.check{display:flex;align-items:center;gap:.5rem})" \
    R"(.presets{display:flex;flex-wrap:wrap;gap:.4rem;align-items:center;margin:0 0 .75rem})" \
    R"(.preset.on{border-color:var(--ink);background:rgba(34,197,94,.22);font-weight:600})" \
    R"(.preset-state{font-size:.78rem;color:var(--dim);border:1px dashed var(--dim);border-radius:999px;padding:.3rem .6rem})" \
    R"(.preset-state.on{color:var(--ink);border-color:var(--ink);border-style:solid})" \
    R"(.row{display:flex;flex-direction:column;gap:1rem})" \
    R"(.grid2{display:grid;grid-template-columns:1fr;gap:.5rem .9rem})" \
    R"(.grid3,.grid4{display:grid;grid-template-columns:repeat(2,1fr);gap:.5rem .9rem})" \
    R"(input,select,textarea,button{font:inherit;color:var(--text);background:#111827;border:1px solid var(--line);padding:.5rem .6rem;min-width:0})" \
    R"(input[type=checkbox]{width:1.05rem;height:1.05rem;padding:0;margin:0;accent-color:var(--btn);flex:none})" \
    R"(input[type=range]{border:none;padding:0;accent-color:var(--btn);flex:1})" \
    R"(:focus-visible{outline:2px solid var(--ink);outline-offset:1px})" \
    R"(.grow{flex:1})" \
    R"(.w4{width:4.5rem}.w6{width:6rem}.w8{width:8rem})" \
    R"(.mt{margin-top:.75rem})" \
    R"(.hint{display:block;font-size:.85rem;color:var(--dim);line-height:1.5;)" \
    /* A system sans for PROSE ONLY. Monospace is right for IPs, hexes and
       coordinates and wrong for 79 paragraphs of explanation: ~15% wider
       and materially less legible at small sizes. Values keep the mono
       face; the sentences that explain them do not. */ \
    R"(font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif})" \
    R"(.hint a{color:inherit})" \
    R"(.btn{background:var(--btn);color:#000;border:none;padding:.6rem 1.6rem;cursor:pointer})" \
    R"(.btn-line{background:transparent;border:1px solid var(--line);color:var(--ink);padding:.4rem .8rem;cursor:pointer;white-space:nowrap})" \
    R"(.btn-danger{background:transparent;color:#ef4444;border:1px solid #ef4444;padding:.4rem .9rem;font-size:.8rem;cursor:pointer})" \
    R"(.savebar{position:sticky;bottom:0;z-index:5;display:flex;align-items:center;gap:1rem;background:#111827;border-top:1px solid var(--line);padding:.7rem 0 .1rem})" \
    R"(#result{font-size:.8rem})" \
    R"(.status{display:flex;flex-wrap:wrap;gap:.3rem 1.2rem;font-size:.78rem;color:var(--dim);margin-bottom:1rem})" \
    R"(.foot{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:.8rem;font-size:.78rem;color:var(--dim);margin-top:1.1rem})" \
    R"(@media(min-width:640px){body{font-size:.9rem;padding:2.5rem 1rem}.wrap{padding:1.25rem}.field{flex-direction:row;align-items:center;gap:.5rem}.row{flex-direction:row}.row>*{flex:1}.grid2{grid-template-columns:repeat(2,1fr)}.grid3{grid-template-columns:repeat(3,1fr)}.grid4{grid-template-columns:repeat(4,1fr)}input,select,textarea{padding:.25rem .45rem}input[type=checkbox]{width:.95rem;height:.95rem}.btn{padding:.45rem 1.4rem}})" \
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
    R"(document.getElementById('cfg').addEventListener('submit',function(e){e.preventDefault();var st=document.getElementById('result');)" \
    R"(var la=document.querySelector('input[name=latitude]'),lo=document.querySelector('input[name=longitude]'),miss=[],junk=[];)" \
    R"([la,lo].forEach(function(i){if(!i)return;var raw=String(i.value).trim(),empty=!raw,bad=!!raw&&!isFinite(bpOne(raw,i===la));)" \
    R"(i.style.outline=(empty||bad)?'2px solid #ff4d4d':'';i.style.background=(empty||bad)?'#4a0000':'';)" \
    R"(if(empty)miss.push(i);if(bad)junk.push(i);)" \
    R"(if(empty||bad){i.addEventListener('input',function(){i.style.outline='';i.style.background=''},{once:true})}});)" \
    /* An unparseable coordinate stops the save outright rather than storing it: a \
       half-saved form is more confusing than a rejected one, and the offending box \
       is already on screen. Every line inside a macro needs the continuation. */ \
    R"(if(junk.length){st.textContent='NOT SAVED - could not read that '+(junk[0]===la?'latitude':'longitude')+'. Examples: 44.058173 or 44.058 N or 44 3 29.4 N';)" \
    R"(st.style.color='#ff4d4d';st.style.fontWeight='bold';junk[0].scrollIntoView({block:'center'});junk[0].focus();return})" \
    R"(st.textContent='saving...';st.style.color='';st.style.fontWeight='';)" \
/* The form declares its own vocabulary: every checkbox name in the DOM AS \
   RENDERED. A toggle absent from it is one this page does not know about, \
   and /save leaves those alone rather than writing false. Built from the \
   DOM, so it cannot drift from the form. See include/ConfigVocabulary.h. */ \
    R"(var fd=new FormData(this);)" \
    R"(fd.append('cfg-toggles',Array.prototype.map.call(this.querySelectorAll('input[type=checkbox]'),function(c){return c.name}).join(','));)" \
    R"(fetch(this.action,{method:'POST',headers:{'X-Blipscope':'1'},body:fd}).then(function(r){return r.text()}).then(function(t){)" \
    R"(st.textContent=t;var w=/MISSING/.test(t);st.style.color=w?'#ff4d4d':'';st.style.fontWeight=w?'bold':'';)" \
    /* Only on a CLEAN save. A response containing MISSING means the device \
       rejected something, and ticking off a step the device did not accept would \
       be the checklist lying in the other direction. Null-guarded because this \
       script is shared by every edition and only the radar page defines it. */ \
    R"(if(!w&&window.bpSetupDone)window.bpSetupDone();)" \
    R"(if(miss.length){miss[0].scrollIntoView({block:'center'});miss[0].focus()}}).catch(function(){st.textContent='save failed - device unreachable'})});)" \
    R"(document.getElementById('resetwifi').addEventListener('click',function(){if(!confirm('Forget WiFi credentials and restart into setup mode? You will need to reconnect the device to a network.'))return;fetch('/reset-wifi',{method:'POST',headers:{'X-Blipscope':'1'}}).then(function(r){return r.text()}).then(function(t){document.getElementById('result').textContent=t})});)" \
    /* Factory reset. EVERY LOOKUP IS NULL-GUARDED: this script is shared by every \
       edition's page and only the radar page carries the factory block, so an \
       unguarded getElementById here would throw and take the SAVE handler above \
       down with it on six other products. */ \
    R"(var fO=document.getElementById('factoryopen'),fP=document.getElementById('factorypanel'),)" \
    R"(fW=document.getElementById('factoryword'),fG=document.getElementById('factorygo'),)" \
    R"(fC=document.getElementById('factorycancel');)" \
    R"(if(fO&&fP&&fW&&fG&&fC){)" \
    R"(fO.addEventListener('click',function(){fP.style.display='block';fO.style.display='none';fW.focus()});)" \
    R"(fC.addEventListener('click',function(){fP.style.display='none';fO.style.display='';fW.value='';fG.disabled=true});)" \
    /* The typed word gates the BUTTON, not the request -- the device checks it \
       again. Exact match, uppercase only: a case-insensitive compare would let \
       "reset" through, and this is the one control where making it easier is the \
       wrong direction. */ \
    R"(fW.addEventListener('input',function(){fG.disabled=(fW.value!=='RESET')});)" \
    R"(fG.addEventListener('click',function(){if(fW.value!=='RESET')return;)" \
    R"(var b=new FormData();b.append('confirm','RESET');)" \
    R"(fetch('/factory-reset',{method:'POST',headers:{'X-Blipscope':'1'},body:b}).then(function(r){return r.text()}).then(function(t){document.getElementById('result').textContent=t});)" \
    R"(})})" \
    R"(var shBr=document.querySelector('input[name=brightness]'),shBv=document.getElementById('brival');)" \
    R"(if(shBr&&shBv){var shSync=function(){shBv.textContent=shBr.value};shBr.addEventListener('input',shSync);shSync()})" \
    R"(var shLa=document.querySelector('input[name=latitude]'),shLo=document.querySelector('input[name=longitude]');)" \
    /* Fold the punctuation people actually paste down to plain ASCII. Written as \
       \u escapes so this file stays 7-bit: the degree sign, both prime marks, the \
       unicode minus and the smart quotes all arrive from map sites and phones. */ \
    R"(function bpN(t){return String(t).replace(/[\u2212\u2013\u2014]/g,'-').replace(/[\u00b0\u00ba]/g,' '))" \
    R"(.replace(/[\u2032\u2019']/g,' ').replace(/[\u2033\u201d]/g,' ').replace(/["\t\r\n]/g,' ').replace(/\s+/g,' ').trim().toUpperCase()})" \
    /* One coordinate -> number, or NaN. Decimal degrees, degrees+decimal minutes \
       and full DMS, hemisphere letter at either end. Deliberately strict about \
       what it REJECTS: a stray letter fails the whole value rather than parsing a \
       prefix, so "Bend, Oregon" and a ZIP code can never become a location. */ \
    R"(function bpOne(t,isLat){var s=bpN(t);if(!s)return NaN;if(/[^0-9NSEW.\-+ ]/.test(s))return NaN;)" \
    R"(var hm=s.match(/[NSEW]/g);if(hm&&hm.length>1)return NaN;var h=hm?hm[0]:'';)" \
    R"(if(h&&isLat&&(h=='E'||h=='W'))return NaN;if(h&&!isLat&&(h=='N'||h=='S'))return NaN;)" \
    R"(var p=s.replace(/[NSEW]/g,' ').match(/[-+]?\d+(?:\.\d+)?/g);if(!p||p.length<1||p.length>3)return NaN;)" \
    R"(p=p.map(Number);if(p.some(function(n){return !isFinite(n)}))return NaN;)" \
    R"(var m=p.length>1?p[1]:0,sc=p.length>2?p[2]:0;if(m<0||sc<0||m>=60||sc>=60)return NaN;)" \
    R"(var v=Math.abs(p[0])+m/60+sc/3600;if(p[0]<0||h=='S'||h=='W')v=-v;)" \
    R"(if(!(Math.abs(v)<=(isLat?90:180)))return NaN;return v})" \
    /* A pasted blob -> [lat,lon] or null, trying separators strongest-first: an \
       explicit comma, then a pair of hemisphere letters, then an even count of \
       numeric terms split down the middle (which is what covers pasted DMS). */ \
    R"(function bpPair(t){var s=bpN(t);if(!s)return null;var hv=[],c=s.indexOf(',');)" \
    R"(if(c>0)hv.push([s.slice(0,c),s.slice(c+1)]);)" \
    R"(var lt=s.match(/[NSEW]/g);if(lt&&lt.length==2){var j=s.search(/[NSEW]/);hv.push([s.slice(0,j+1),s.slice(j+1)])})" \
    R"(var ns=s.match(/[-+]?\d+(?:\.\d+)?/g);)" \
    R"(if(ns&&ns.length>=2&&(ns.length&1)==0){var k=0;for(var i=0;i<ns.length/2;i++)k=s.indexOf(ns[i],k)+ns[i].length;hv.push([s.slice(0,k),s.slice(k)])})" \
    R"(for(var q=0;q<hv.length;q++){var a=bpOne(hv[q][0],true),b=bpOne(hv[q][1],false);if(isFinite(a)&&isFinite(b))return [a,b]}return null})" \
    /* Stored at 6 dp (~11 cm, far past what a desk radar can use) with trailing \
       zeros trimmed; echoed at 4 dp, which is the precision a human can actually \
       check against the place they meant. */ \
    R"(function bpF(v){return String(Number(v.toFixed(6)))})" \
    R"(if(shLa&&shLo){var bpMsg=null;)" \
    R"(function bpSay(txt,ok){if(!bpMsg){bpMsg=document.createElement('div');bpMsg.style.cssText='margin:.35rem 0 0;font-size:.8rem';)" \
    R"(var r=shLa.closest?shLa.closest('.row'):null;if(r&&r.parentNode)r.parentNode.insertBefore(bpMsg,r.nextSibling);else shLa.parentNode.appendChild(bpMsg)})" \
    R"(bpMsg.textContent=txt;bpMsg.style.color=ok?'var(--ink)':'#ff4d4d'})" \
    /* Confirm what was understood, so a paste that landed looks like it landed. */ \
    R"(function bpEcho(){var a=String(shLa.value).trim(),b=String(shLo.value).trim();if(!a&&!b){if(bpMsg)bpMsg.textContent='';return})" \
    R"(var x=bpOne(a,true),y=bpOne(b,false);)" \
    R"(if(a&&!isFinite(x)){bpSay('Could not read the latitude. Examples: 44.058173 or 44.058 N or 44 3 29.4 N',false);return})" \
    R"(if(b&&!isFinite(y)){bpSay('Could not read the longitude. Examples: -121.315308 or 121.315 W or 121 18 55 W',false);return})" \
    R"(if(!a||!b){bpSay('Enter both boxes to finish.',false);return})" \
    R"(bpSay('Using '+x.toFixed(4)+', '+y.toFixed(4),true)})" \
    /* Normalise on blur, never per-keystroke: rewriting the box while someone is \
       still typing into it is the kind of "help" that loses their input. */ \
    R"(function bpTidy(i,isLat){var raw=String(i.value).trim();if(!raw)return;var v=bpOne(raw,isLat);if(isFinite(v))i.value=bpF(v);bpEcho()})" \
    R"(shLa.addEventListener('change',function(){bpTidy(shLa,true)});shLo.addEventListener('change',function(){bpTidy(shLo,false)});)" \
    /* A pair pasted into EITHER box fills both -- people paste into whichever one \
       they clicked, and being wrong about which should not cost them the paste. */ \
    R"([[shLa,true],[shLo,false]].forEach(function(f){f[0].addEventListener('paste',function(e){)" \
    R"(var t=((e.clipboardData||window.clipboardData).getData('text')||'');var pr=bpPair(t);)" \
    R"(if(pr){e.preventDefault();shLa.value=bpF(pr[0]);shLo.value=bpF(pr[1]);bpEcho();return})" \
    R"(var one=bpOne(t,f[1]);if(isFinite(one)){e.preventDefault();f[0].value=bpF(one);bpEcho()}})});)" \
    R"(bpEcho()})" \
    /* SETUP CHECKLIST -- ONE block, never two competing banners. \
       Both steps state the SAME consequence ("the screen stays empty"), because \
       both cause it. A customer who verifies but forgets their location must be \
       able to work out which half is missing from the page rather than from \
       support, and two separate red boxes would each read as the only problem. \
       A completed step collapses to a tick instead of vanishing, so the list \
       still reads as a list of two. */ \
    /* Substituted by the RADAR page's processor only. On an edition without the \
       cloud feed both come back empty, so BP_ENROLLED is '' rather than '0' and \
       step 2 never renders -- the checklist degrades to the location banner it \
       replaced. */ \
    R"(window.BP_DEVID='%DEVICE_ID%';window.BP_ENROLLED='%ENROLLED%';window.BP_REFUSED='%REFUSED%';)" \
    R"(var stNeedLoc=(shLa&&shLo&&(!String(shLa.value).trim()||!String(shLo.value).trim()));)" \
    /* A REFUSED board takes the same path as an unverified one, which is why \
       re-enrollment costs nothing to offer: the Verify button, the Turnstile \
       popup, the ?id= paste fallback and the /enroll-key landing all already \
       render off stNeedKey. Only the CONDITION was missing, never the action. */ \
    R"(var stRefused=(window.BP_REFUSED==='1');)" \
    R"(var stNeedKey=(window.BP_ENROLLED==='0')||stRefused;)" \
    R"(if(stNeedLoc||stNeedKey){var stB=document.createElement('div');stB.id='bpBanner';)" \
    R"(stB.style.cssText='background:#4a0000;color:#ffd9d9;border:1px solid #ff4d4d;border-radius:6px;padding:12px 14px;margin:10px 0';)" \
    /* A refused board is not a new board, and must not be greeted as one. It was \
       working; something server-side stopped accepting its key -- most likely a \
       credential rotation nobody here did anything to cause. The heading says what \
       happened without blaming the owner. */ \
    R"(var stH=document.createElement('div');stH.textContent=stRefused?'This device needs re-verifying.':'Two steps and your radar is live.';)" \
    R"(stH.style.cssText='font-weight:bold;margin-bottom:8px';stB.appendChild(stH);)" \
    R"(function stStep(n,t,b,done){var d=document.createElement('div');d.style.cssText='margin:7px 0;line-height:1.45';)" \
    /* ASCII on purpose: this page is served without an explicit charset, so a \
       multi-byte glyph would be a coin-flip between a tick and mojibake on the \
       one screen a customer reads when something is already wrong. */ \
    R"(var s=document.createElement('b');s.textContent=(done?'DONE - ':n+'. ')+t;d.appendChild(s);)" \
    R"(if(!done){d.appendChild(document.createTextNode(' '+b))}else{d.style.color='#9fe6a0'})" \
    R"(stB.appendChild(d);return d})" \
    R"(var stS1=stStep(1,'Set your location.','The radar draws the sky around you. Until it has a location, the screen stays empty.',!stNeedLoc);stS1.id='bpStep1';)" \
    R"(var stK=stStep(2,stRefused?'Re-verify this device.':'Verify this device.',stRefused?'The server is no longer accepting this device key, so the screen has stopped filling. One click restores it. Nothing else on this page needs changing, and your logbook is untouched.':'Verification is how a self-flashed board gets aircraft data. Without it the screen stays empty even with a location set. One click, once per board. It also puts you on the leaderboard under your own standing.',!stNeedKey);)" \
    R"(if(stNeedKey){var stW=document.createElement('div');stW.style.cssText='margin-top:8px';)" \
    R"(var stBtn=document.createElement('button');stBtn.type='button';stBtn.id='bpVerify';)" \
    R"(stBtn.textContent='Verify this device';)" \
    R"(stBtn.style.cssText='font:inherit;padding:7px 14px;border:0;border-radius:5px;background:#1f6feb;color:#fff;cursor:pointer';)" \
    R"(var stAlt=document.createElement('div');stAlt.style.cssText='font-size:12px;color:#ffbdbd;margin-top:7px';)" \
    R"(stAlt.textContent='No internet on this machine? Open scopes.valarsystems.com/enroll?id='+window.BP_DEVID+' on your phone, then paste the key into Access key below.';)" \
    R"(stW.appendChild(stBtn);stW.appendChild(stAlt);stK.appendChild(stW)})" \
    R"(var shF=document.getElementById('cfg');shF.parentNode.insertBefore(stB,shF);)" \
    /* THE CHECKLIST IS BUILT ONCE, AT LOAD, AND SAVING IS AN ASYNC FETCH -- so \
       nothing re-evaluated it and a customer who had just entered their location \
       still read "1. Set your location" until they refreshed. The page was \
       telling them the step was outstanding immediately after they completed it, \
       which on the one screen somebody reads when setup is not working is the \
       worst possible place to be wrong. \
       \
       Re-checked from the LIVE input values rather than from a flag set at save \
       time: the inputs are what the customer sees, so reading them is the only \
       version that cannot disagree with the screen. */ \
    R"(window.bpSetupDone=function(){var b=document.getElementById('bpBanner');if(!b)return;)" \
    R"(var la=document.querySelector('input[name=latitude]'),lo=document.querySelector('input[name=longitude]');)" \
    R"(if(!la||!lo||!String(la.value).trim()||!String(lo.value).trim())return;)" \
    /* The location is set. If verification was the only other outstanding step \
       and it is done too, the whole block goes -- an empty checklist is not a \
       checklist. Otherwise step 1 collapses to a tick and step 2 stays, which is \
       the same "still reads as a list of two" rule the block was built on. */ \
    R"(if(!((window.BP_ENROLLED==='0')||(window.BP_REFUSED==='1'))){b.parentNode.removeChild(b);return})" \
    R"(var s1=document.getElementById('bpStep1');if(!s1)return;)" \
    R"(while(s1.firstChild)s1.removeChild(s1.firstChild);)" \
    R"(var d=document.createElement('b');d.textContent='DONE - Set your location.';)" \
    R"(s1.appendChild(d);s1.style.color='#9fe6a0'};)" \
    R"(if(stNeedLoc){[shLa,shLo].forEach(function(i){i.style.outline='2px solid #ff4d4d';i.style.background='#4a0000';)" \
    R"(i.addEventListener('input',function(){i.style.outline='';i.style.background=''})})}})" \
    /* THE POPUP IS A CONVENIENCE, NOT A BOUNDARY. It carries the device id so the \
       hosted page can mint for this board, and the key returns by postMessage -- \
       which crosses HTTPS -> HTTP because no resource is loaded, only a message. \
       Every failure here (popup blocked, network blocked, window closed) lands on \
       the same paste fallback rather than a dead end. */ \
    R"(document.addEventListener('click',function(e){if(!e.target||e.target.id!=='bpVerify')return;)" \
    /* CANONICAL path, not the short one the fallback text prints: the popup is \
       machine-driven and has no reason to spend a redirect hop it could fail on. \
       The short /enroll is a 301 for the human who types it. */ \
    R"(window.open('https://scopes.valarsystems.com/blipscope/enroll?id='+encodeURIComponent(window.BP_DEVID),'bpEnroll','width=520,height=640')});)" \
    /* Validate the VALUE, not the sender. A key must be 64 hex and is handed \
       straight back to this device, which checks it names THIS board before \
       storing it -- so a page that lies about its origin gains nothing. */ \
    /* ONE KEY LANDING PER PAGE LOAD. The popup used to re-post its key every
       time Turnstile refreshed its token (~5 min, fixed in enrollpage.ts on
       2026-09-01), and each message here is an NVS write of "cloud-key-fac"
       plus a reload that installs a fresh listener -- so the cycle could
       sustain itself. Harmless in the end (NVS wear budget is ~100k and the
       observed volume was thousands), and the driving loop is fixed upstream,
       but a receiver that writes flash on every message it is handed should
       not depend on the sender being well-behaved. Same shape as the bug that
       fed it: a handler the caller may invoke repeatedly, with no guard. */ \
    R"(var enrolling=false;)" \
    R"(window.addEventListener('message',function(e){var d=e.data;)" \
    R"(if(!d||d.type!=='blipscope-enroll'||!/^[0-9a-f]{64}$/.test(String(d.key||'')))return;)" \
    R"(if(enrolling)return;enrolling=true;)" \
    R"(var fd=new FormData();fd.append('key',d.key);fd.append('id',d.id||'');)" \
    R"(fetch('/enroll-key',{method:'POST',headers:{'X-Blipscope':'1'},body:fd}).then(function(r){)" \
    R"(if(r.ok){location.reload()}else{enrolling=false;r.text().then(function(t){alert('Could not save the key: '+t)})}})});)" \
    R"(document.querySelectorAll('summary input').forEach(function(i){i.addEventListener('click',function(e){e.stopPropagation()})});)" \
    R"(document.querySelectorAll('details.auto').forEach(function(d){if(d.open)return;var m=d.querySelector('summary input[type=checkbox]');if(m){if(m.checked)d.open=true;return}var any=false;d.querySelectorAll('textarea,input[type=password],input[type=text],input:not([type])').forEach(function(i){var v=(i.value||'').trim();if(v&&!/^\*+$/.test(v))any=true});if(any)d.open=true});)" \
    R"(</script>)"

// HTML stored in flash
// %PLACEHOLDER% tokens are substituted at serve time by the template processor.
// The page is feature-specific: the radar build serves the radar settings form below; the
// FEATURE_EAM build serves the EAM monitor form; the FEATURE_SPACE build serves the Spacescope
// form. The ConfigurationWebServer shell (NVS namespace, mDNS, /reset-wifi, save flag) is shared.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
// The viewer page for /diag/fb. Its own literal rather than part of CONFIG_HTML:
// it takes no %PLACEHOLDER% substitution, so it is served with send_P and the
// percent sign is an ordinary character here.
//
// DECODING HAPPENS IN THE BROWSER. The device sends 115,200 bytes exactly as
// they sit in PSRAM and spends no CPU on encoding; the phone that asked turns
// RGB565 into pixels. A PNG encoder on the S3 would cost flash and frame time to
// save bandwidth on a LAN that has plenty.
static const char FB_VIEWER_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Blipscope screen</title>
        <style>
          body{margin:0;padding:1rem;background:#111827;color:#e6edea;
               font-family:ui-monospace,Menlo,Consolas,monospace;font-size:1rem}
          h1{font-size:1.1rem;margin:0 0 .2rem;color:#22c55e}
          p{margin:.2rem 0 1rem;color:#7f9e91;font-size:.85rem;max-width:34rem}
          /* THE CANVAS IS NOT STYLED, and that is load-bearing. It carries the
             `hidden` attribute, which works through the USER-AGENT rule
             [hidden]{display:none} -- and ANY author rule beats the user-agent
             origin whatever its specificity. Styling `canvas` here with
             display:block re-showed it, so the page rendered the frame twice. */
          [hidden]{display:none}
          img{display:block;width:100%;max-width:320px;height:auto;
                 image-rendering:pixelated;border:1px solid #22c55e;border-radius:50%}
          #hint{font-size:.8rem;color:#7f9e91;margin:.5rem 0 0}
          .row{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;margin-top:.9rem}
          button{font:inherit;color:#e6edea;background:transparent;border:1px solid #22c55e;
                 padding:.45rem .8rem;border-radius:999px;cursor:pointer}
          #note{font-size:.8rem;color:#7f9e91}
        </style>
    </head>
    <body>
        <h1>Blipscope screen</h1>
        <p>A copy of what the device is showing right now. Save or screenshot this
           and send it to support.</p>
        <!-- THE IMAGE IS WHAT A PHONE CAN SAVE. A canvas long-presses to
             nothing, and the Save button hits Chrome's "Insecure download
             blocked" on an http origin -- reported from a real phone. An <img>
             with a data: URL needs no download event at all, so long-press
             offers "Save to Photos" with no warning.

             The canvas still does the decoding; it is simply not displayed. -->
        <canvas id="c" width="240" height="240" hidden></canvas>
        <img id="shot" alt="the device screen" width="240" height="240">
        <div class="row">
            <button id="again" type="button">Refresh</button>
            <button id="save" type="button">Save image</button>
            <span id="note">loading&hellip;</span>
        </div>
        <!-- BOTH PLATFORMS SPELLED OUT, and no UA sniffing. On iOS the long-press
             saves with no download event at all. On Android, "Download image"
             still goes through the download manager, so Chrome's plain-HTTP
             policy still applies and the warning can still appear -- naming that
             is the difference between a customer tapping Keep and a customer
             deciding the tool is broken.

             Sniffing the UA to show one line would be wrong twice over: it gets
             it wrong on desktop Safari and on every browser that lies, and it
             hides the instruction the customer actually needs when it guesses. -->
        <p id="hint"><b>iPhone / iPad:</b> press and hold the picture, then
           <b>Save to Photos</b>.<br>
           <b>Android:</b> press and hold, then <b>Download image</b>. If Chrome
           warns about an insecure download, tap <b>Keep</b> &mdash; the device is
           on your own network and the picture is the one above.<br>
           On a computer, use the <b>Save image</b> button.</p>
        <script>
        const cv = document.getElementById('c');
        const note = document.getElementById('note');
        // ONE RETRY, NOT A LOOP. A torn frame is caught mid-redraw, so asking
        // again almost always lands on a whole one -- but a device redrawing
        // continuously could tear every time, and a page that retried until it
        // got a clean frame would hang instead of showing a slightly torn one.
        async function load(retry) {
            note.textContent = 'fetching…';
            let r;
            try { r = await fetch('/diag/fb', {cache: 'no-store'}); }
            catch (e) { note.textContent = 'could not reach the device'; return; }
            if (!r.ok) { note.textContent = 'device said ' + r.status; return; }
            const w = parseInt(r.headers.get('X-Blipscope-Frame-Width') || '240', 10);
            const h = parseInt(r.headers.get('X-Blipscope-Frame-Height') || '240', 10);
            const torn = r.headers.get('X-Blipscope-Frame-Torn') === 'true';
            const buf = new Uint8Array(await r.arrayBuffer());
            if (buf.length < w * h * 2) {
                note.textContent = 'short frame: ' + buf.length + ' of ' + (w * h * 2) + ' bytes';
                return;
            }
            if (torn && retry) { load(false); return; }
            cv.width = w; cv.height = h;
            const ctx = cv.getContext('2d');
            const img = ctx.createImageData(w, h);
            // RGB565 little-endian -> RGBA. The 5- and 6-bit channels are scaled
            // by replicating their high bits, so full-scale stays full-scale:
            // 0x1F becomes 255, not 248.
            // BIG-ENDIAN: the sprite holds pixels in the panel's byte order, so the
            // high byte comes first. See the header comment on the /diag/fb route.
            for (let i = 0, p = 0; i < w * h; i++, p += 4) {
                const v = (buf[i * 2] << 8) | buf[i * 2 + 1];
                const r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
                img.data[p]     = (r5 << 3) | (r5 >> 2);
                img.data[p + 1] = (g6 << 2) | (g6 >> 4);
                img.data[p + 2] = (b5 << 3) | (b5 >> 2);
                img.data[p + 3] = 255;
            }
            ctx.putImageData(img, 0, 0);
            // toDataURL, not toBlob: a blob: URL still behaves like a download
            // when saved, which is the thing being avoided. A data: URL is just
            // an image as far as the long-press menu is concerned.
            document.getElementById('shot').src = cv.toDataURL('image/png');
            note.textContent = w + '×' + h + (torn ? ' — torn (caught mid-redraw)' : '');
        }
        // SAVE, because "screenshot this" is one more step than it sounds on a
        // phone and produces a picture of a browser rather than of the device.
        // The canvas already holds the decoded frame, so this costs no second
        // fetch and no device work at all.
        //
        // The filename carries a local timestamp: support ends up with several of
        // these from one customer, and a folder of blipscope-screen.png
        // followed by the same name with a 2 after it tells nobody
        // which is which. It is the browser's own clock, which is a fact it has --
        // the device's time is not in the response and would be invented here.
        document.getElementById('save').addEventListener('click', function () {
            if (!cv.toBlob) { note.textContent = 'this browser cannot save the image'; return; }
            cv.toBlob(function (b) {
                if (!b) { note.textContent = 'could not build the image'; return; }
                const d = new Date();
                const two = function (n) { return (n < 10 ? '0' : '') + n; };
                const stamp = d.getFullYear() + two(d.getMonth() + 1) + two(d.getDate())
                            + '-' + two(d.getHours()) + two(d.getMinutes()) + two(d.getSeconds());
                const a = document.createElement('a');
                a.href = URL.createObjectURL(b);
                a.download = 'blipscope-screen-' + stamp + '.png';
                document.body.appendChild(a);
                a.click();
                a.remove();
                // Revoked on a timer rather than immediately: Safari reads the
                // object URL after the click returns, and revoking synchronously
                // gives a saved file of zero bytes.
                setTimeout(function () { URL.revokeObjectURL(a.href); }, 5000);
                // NOT "saved": the page hands the browser a PNG and cannot see
                // what it does with it. On a phone the file lands in Downloads
                // with no visible confirmation, so the useful half of this
                // sentence is WHERE to look, not a claim about what happened.
                note.textContent = a.download + ' — check your downloads';
            }, 'image/png');
        });
        document.getElementById('again').addEventListener('click', function () { load(true); });
        load(true);
        </script>
    </body>
</html>
)";

static const char CONFIG_HTML[] PROGMEM = R"(
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope</title>
        <!-- inline SVG favicon (radar blip) so the tab is easy to spot; no extra flash asset / route needed.
             Colors use rgb() not #-hex on purpose: a "#" in a data URI must be percent-encoded, and any
             stray percent sign collides with this page's PLACEHOLDER template engine and shreds the whole
             form (write it as &#37; in visible text - and keep it out of comments too, like this one). -->
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(17,24,39)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(34,197,94)' stroke-width='1'/><circle cx='8' cy='8' r='1.7' fill='rgb(34,197,94)'/></svg>">
        <style>:root{--ink:#22c55e;--line:#22c55e;--dim:#7f9e91;--btn:#22c55e;--text:#e6edea}</style>
)" CONFIG_SHELL_CSS R"(
        <!-- Sidebar layout. Radar-only on purpose: the other seven editions have short
             single-screen forms a nav would only get in the way of, and this block is
             PROGMEM, so scoping it here keeps ~1 KB off each of their pages.
             No percent signs below - the template engine claims that character (see the
             favicon comment above), so every size is fr/px/flex. -->
        <style>
          /* The rail has to be ADDITIVE, not carved out of the form. .wrap is 42rem
             and shared with the other seven editions; dropping a 170px column inside
             it left the form column ~27rem -- narrow enough that every two-word
             checkbox label wrapped onto two lines: Directional aircraft, Altitude
             colors, Night clock, Position source. Widen by exactly the rail plus its
             gap so the form is the same width it was before the nav existed.
             Scoped to this radar-only block, so the other editions keep 42rem.
             NB no close-paren-then-double-quote anywhere in this block: that pair
             ends the C++ raw string literal the whole page lives in. */
          .wrap{max-width:54rem}
          .shell{display:grid;grid-template-columns:170px 1fr;gap:1.1rem;align-items:start}
          .side{display:flex;flex-direction:column;gap:.2rem;position:sticky;top:.5rem}
          .navb{text-align:left;background:none;border:1px solid var(--dim);color:var(--dim);
                padding:.45rem .6rem;border-radius:6px;cursor:pointer;font:inherit;line-height:1.3}
          .navb:hover{color:var(--ink)}
          .navb.on{color:var(--ink);border-color:var(--line);background:rgba(34,197,94,.22);font-weight:600}
          .content{min-width:0}
          .sec{display:none}
          .sec.on{display:block}
          .stand{border:1px solid var(--line);border-radius:8px;padding:.6rem .75rem;margin-bottom:.9rem}
          .kv{display:flex;justify-content:space-between;gap:.5rem;border-bottom:1px solid var(--line);padding:.3rem 0}
          /* Phone: the rail becomes a scrollable chip row above the content. No drawer,
             no hamburger, nothing that can get stuck open while somebody is standing next
             to the device trying to set their location. */
          @media(max-width:700px){
            .shell{grid-template-columns:1fr;gap:.6rem}
            .side{flex-direction:row;flex-wrap:wrap;position:static;gap:.35rem;border-top:1px solid var(--line);border-bottom:1px solid var(--line);padding:.55rem 0;margin:0 0 .85rem}
            /* WRAP, DO NOT SCROLL. This was overflow-x:auto with flex:0 0 auto --
               a horizontal scroller by design. Measured at 390 px the four buttons
               need 446 px in a 324 px rail, so two sat off the right edge (right
               edges 479 and 406) behind a scrollbar nobody looks for.
               THE DOCUMENT NEVER OVERFLOWED, so a documentElement.scrollWidth
               check passes in both worlds; the assertion has to be per-element. */
            .navb{white-space:nowrap;flex:0 1 auto;padding:.45rem .7rem;border-radius:999px}
          }
        </style>
    </head>
    <body data-start="%START_SECTION%">
        <fieldset class="wrap">
            <legend>Configure Blipscope</legend>

            <div class="status">
                <span>%DEVICE_NAME%.local</span>
                <span>%DEVICE_IP%</span>
                <span>WiFi %WIFI_RSSI% dBm</span>
                <span>firmware v%FW_VERSION%</span>
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <div class="shell">
                <nav class="side" id="side">
                    <button type="button" class="navb" data-go="location">Location</button>
                    <button type="button" class="navb" data-go="display">Display</button>
                    <button type="button" class="navb" data-go="labels">Labels</button>
                    <button type="button" class="navb" data-go="alerts">Alerts</button>
                    <button type="button" class="navb" data-go="follow">Follow</button>
                    <button type="button" class="navb" data-go="network">Network</button>
                    <button type="button" class="navb" data-go="collection">Collection</button>
                    <button type="button" class="navb" data-go="about">About</button>
                </nav>
                <div class="content">

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <div class="sec" data-sec="location">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Right-click your spot in Google Maps and copy the numbers, then paste into either box &mdash; both fill in. &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

                <details class="auto">
                    <summary>Saved locations (home / work)</summary>
                    <span class="hint">Store your regular spots, then &ldquo;Use&rdquo; to load one into the fields above &mdash; save the form to switch. &ldquo;Save here&rdquo; captures the current lat/lon into a slot.</span>
                    <div id="locslots">
                        <div class="row loc-slot" data-slot="0">
                            <input name="loc0-name" value='%LOC0_NAME%' maxlength="16" placeholder="Home" class="grow">
                            <input name="loc0-lat" value='%LOC0_LAT%' placeholder="lat" class="w8">
                            <input name="loc0-lon" value='%LOC0_LON%' placeholder="lon" class="w8">
                            <button type="button" class="btn-line loc-use">Use</button>
                            <button type="button" class="btn-line loc-save">Save here</button>
                        </div>
                        <div class="row loc-slot" data-slot="1">
                            <input name="loc1-name" value='%LOC1_NAME%' maxlength="16" placeholder="Work" class="grow">
                            <input name="loc1-lat" value='%LOC1_LAT%' placeholder="lat" class="w8">
                            <input name="loc1-lon" value='%LOC1_LON%' placeholder="lon" class="w8">
                            <button type="button" class="btn-line loc-use">Use</button>
                            <button type="button" class="btn-line loc-save">Save here</button>
                        </div>
                        <div class="row loc-slot" data-slot="2">
                            <input name="loc2-name" value='%LOC2_NAME%' maxlength="16" placeholder="Trip" class="grow">
                            <input name="loc2-lat" value='%LOC2_LAT%' placeholder="lat" class="w8">
                            <input name="loc2-lon" value='%LOC2_LON%' placeholder="lon" class="w8">
                            <button type="button" class="btn-line loc-use">Use</button>
                            <button type="button" class="btn-line loc-save">Save here</button>
                        </div>
                    </div>
                    <script>
                    (function(){
                      var lat=document.querySelector('[name=latitude]'), lon=document.querySelector('[name=longitude]');
                      document.querySelectorAll('.loc-slot').forEach(function(s){
                        var n=s.dataset.slot;
                        var sl=s.querySelector('[name=loc'+n+'-lat]'), so=s.querySelector('[name=loc'+n+'-lon]');
                        s.querySelector('.loc-use').addEventListener('click',function(){ if(sl.value&&so.value){lat.value=sl.value;lon.value=so.value;} });
                        s.querySelector('.loc-save').addEventListener('click',function(){ sl.value=lat.value;so.value=lon.value; });
                      });
                    })();
                    </script>
                </details>

                <label class="field">
                    <span>Radius:</span>
                    <input id="radius" name="radius" type="number" min="0.1" step="0.1" max="222" value='%RADIUS%' class="grow">
                    <select id="radius-unit" name="radius-unit">
                        <option value="mi" %RADIUS_UNIT_MI%>mi</option>
                        <option value="km" %RADIUS_UNIT_KM%>km</option>
                        <option value="nmi" %RADIUS_UNIT_NMI%>nmi</option>
                    </select>
                </label>

                </div><!-- /sec -->

                <div class="sec" data-sec="network">
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
// The cloud option is still LISTED here, disabled, and it leads so the list does
// not reshuffle between builds. Omitting it entirely was the old behaviour and it
// made the most consequential fact about a binary invisible: the page simply did
// not mention the cloud, so a no-cloud build looked like a normal one that had
// been configured for OpenSky. On 2026-08-07 two bench boards sat on anonymous
// OpenSky for hours and the staleness was chased through the proxy, the relay
// TTLs and mDNS before anyone read the build stamp.
//
// Disabled rather than selectable, because a control that cannot do what it says
// is worse than an absent one -- picking "cloud" here would save a value this
// firmware ignores (AircraftManager has no useCloudSource to set), and the page
// would then assert something false to the next person debugging it.
//
// Never `selected`: a select whose selected option is disabled submits
// unpredictably. If NVS holds "cloud", OpenSky renders selected, which is exactly
// what the firmware is doing -- the disabled row explains why.
//
// This does NOT violate rule 4 of scripts/check-config-form.py ("nothing inside
// the form is disabled"). That rule exists because a disabled INPUT/SELECT is
// dropped from FormData and silently turns a whole-form POST into a partial one.
// A disabled <option> removes no field: the <select> still submits, it just
// cannot submit this value. The checker scans <input|select|textarea> only, so
// this is outside its rule by construction as well as by intent.
R"(                        <option value="cloud" disabled>Blipscope Cloud &mdash; not in this firmware build</option>
                        <option value="opensky" %DATASRC_OPENSKY%>OpenSky Network (cloud)</option>
)"
#endif
R"(                        <option value="local" %DATASRC_LOCAL%>My own ADS-B receiver</option>
                    </select>
                </label>
)"
#ifdef FEATURE_CLOUD_FEED
// Cloud fields double as the data credit. BOTH position sources are named here
// permanently, regardless of which one served any given response -- adsb.fi
// requires a citation + link to their home page, adsb.lol's ODbL requires
// attribution, and the chain can fail over between them mid-session. Their
// licences differ (adsb.fi grants no ODbL), so each gets its OWN sentence: a
// shared "licensed under ODbL 1.0" clause would misattribute adsb.fi's terms.
R"(
                <div id="cloud-fields" class="stack">
                    <!-- DEVICE ID, ALWAYS VISIBLE AND ALWAYS SELECTABLE.
                         It was reachable only inside the not-yet-verified checklist,
                         interpolated into one sentence of red helper text -- so it
                         vanished the moment a board enrolled, and was easy to miss
                         before that. Both states leave a customer stuck:
                           - on a Turnstile-blocked network the paste fallback needs
                             the id to build the ?id= URL by hand, and being told to
                             read it out of a sentence that is no longer on screen is
                             not a fallback;
                           - revocation and support are BY ID, so "which board is
                             this?" has to be answerable without a serial console.
                         readonly, not disabled: disabled inputs are skipped by form
                         submission AND are not selectable in some browsers, and being
                         able to COPY this is the entire point. It carries no name
                         attribute, so it is never posted back -- the id is derived
                         from the efuse MAC on the device and is not settable.
                         NOTE the single quotes on onclick, which are load-bearing:
                         this markup lives inside a C++ raw string literal, so a close
                         paren followed immediately by a double quote terminates that
                         literal early -- anywhere in the block, including inside a
                         comment like this one. Writing onclick with double quotes ends
                         the string mid-attribute, and the compiler then reports a
                         missing terminating character against an unrelated comment
                         sixty lines further down, a long way from the actual edit. -->
                    <label class="field">
                        <span>Device ID:</span>
                        <input type="text" readonly value='%DEVICE_ID%' class="grow" onclick='this.select()'>
                    </label>
                    <label class="field">
                        <span>Access key:</span>
                        <input name="cloud-key" type="password" autocomplete="off" value='%CLOUD_KEY%' placeholder="access key" class="grow">
                    </label>
                    <!-- CLOUD SERVER LIVES BEHIND A DISCLOSURE, and the rule is worth
                         keeping: an OVERRIDE OF A WORKING DEFAULT goes in the drawer;
                         a field CONDITIONALLY REQUIRED by the selector above does not.
                         That is why opensky-id/secret and local-url stay out in the
                         open -- pick that source and you must fill them in -- while
                         this one, which nobody on the default path ever touches, does
                         not sit between the source selector and the Access key box the
                         enrolment checklist points at by name ("paste the key into
                         Access key below").
                         "Advanced" was rejected as the label: it is a category that
                         accretes anything nobody wants to place, and within two
                         releases it is a junk drawer. Naming what the control DOES
                         keeps the boundary decidable.
                         details.auto auto-opens when any field inside holds a value
                         (see the page script), so a self-hoster or a board pointed at
                         staging still finds it open on arrival -- the usual objection
                         to hiding a set field does not apply. -->
                    <details class="auto">
                        <summary>Point this device at a different server</summary>
                        <label class="field">
                            <span>Cloud server:</span>
                            <input name="cloud-url" value='%CLOUD_URL%' placeholder="built-in default" class="grow">
                        </label>
                        <span class="hint">For self-hosting or a staging server. Blank uses the default.</span>
                    </details>
                    <span class="hint">Managed Blipscope feed, no account needed. The access key is set during assembly; if it is ever changed by mistake, clear the box and save. Sources and licences are under About.</span>
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
                    <span class="hint">dump1090-fa / readsb / PiAware / tar1090. Enter the IP or the aircraft.json URL.</span>
                    <label class="field">
                        <span>Aircraft details:</span>
                        <select name="local-details" id="local-details" class="grow">
                            <option value="" disabled %LD_UNSET%>Choose one &mdash; no default</option>
                            <option value="cloud" %LD_CLOUD%>Blipscope Cloud</option>
                            <option value="off" %LD_OFF%>Off &mdash; receiver data only</option>
                        </select>
                    </label>
                    <span class="hint">Positions always come from your receiver; this only chooses where card details (type, airline, route, photo) come from. No default &mdash; <b>until you choose, details stay off.</b><br><b>Blipscope Cloud</b> sends the tapped aircraft's hex, callsign and position, plus your device model, firmware and access key; never your receiver's address or your location &mdash; though <i>a tapped aircraft is near you, so treat it as coarse location</i>. Only Cloud has photos.<br><b>Off</b> contacts nothing; the card shows what your receiver reported.</span>
                </div>

                </div><!-- /sec -->

                <div class="sec" data-sec="display">
                <fieldset>
                    <div class="grid3">
                        <label class="check"><input name="scanline" type="checkbox" %SCANLINE%><span>Radar sweep</span></label>
                        <label class="check"><input name="fade" type="checkbox" %FADE%><span>Sweep fade</span></label>
                        <label class="check"><input name="triangle" type="checkbox" %TRIANGLE%><span>Directional aircraft</span></label>
                        <label class="check"><input name="airports" type="checkbox" %AIRPORTS%><span>Airports</span></label>
                        <label class="check"><input name="trail" type="checkbox" %TRAIL%><span>Flight trails</span></label>
                        <label class="check"><input name="altcolor" type="checkbox" %ALTCOLOR%><span>Altitude colors</span></label>
                        <label class="check"><input name="highlight" type="checkbox" %HIGHLIGHT%><span>Highlights</span></label>
                        <label class="check"><input name="autodim" type="checkbox" %AUTODIM%><span>Auto-dim at night</span></label>
                        <label class="check"><input name="night-clock" type="checkbox" %NIGHT_CLOCK%><span>Night clock (empty sky)</span></label>
                    </div>
                    <label class="field mt">
                        <span>Brightness:</span>
                        <input name="brightness" type="range" min="10" max="255" value='%BRIGHTNESS%'>
                        <span id="brival" class="hint"></span>
                    </label>
                    <label class="field mt">
                        <span>Clock UTC offset (hrs):</span>
                        <input name="tz-offset" type="number" min="-12" max="14" step="0.5" value='%TZ_OFFSET%' placeholder='%TZ_AUTO%' class="w6">
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
                    <label class="field mt">
                        <span>Show airports:</span>
                        <select name="airports-min" class="grow">
                            <option value="all" %AIRPORTS_MIN_ALL%>All (incl. small strips)</option>
                            <option value="med" %AIRPORTS_MIN_MED%>Medium &amp; large only</option>
                            <option value="large" %AIRPORTS_MIN_LARGE%>Large only</option>
                        </select>
                    </label>
                    <span class="hint">
                        With the Blipscope Cloud feed the overlay draws every real airport near you. In a
                        busy general-aviation area that can be a lot of small strips &mdash; narrow it to the
                        fields with scheduled service.
                    </span>
                </fieldset>

                </div><!-- /sec -->

                <div class="sec" data-sec="labels">

                <fieldset>
                    <!-- THE FLAG, NOT A SWITCH. This was a full-width labelled
                         toggle above the grid; it is the "None" chip in the row below
                         now, because a switch and a preset row were two controls
                         answering one question, and customers read the row first.

                         STILL A CHECKBOX, hidden rather than replaced: an unchecked
                         checkbox is absent from the POST and SaveToggle writes
                         "false" for it, which is the existing semantics exactly. A
                         hidden input carrying "true"/"false" would be a different
                         contract with the save path. Same NVS key, no firmware
                         change, no migration. -->
                    <input name="infotext" id="infotext" type="checkbox" %INFOTEXT% hidden>
                    <span class="hint">What&rsquo;s written beside each blip on the radar. The radar draws <b>at most three lines</b> per aircraft; everything else is on the card you get by tapping one.</span>
                    <!-- Nothing is applied on load -- see the script. "None" is the
                         EMPTY preset: it unticks every field, which is what the word
                         says. "Custom" stays a state rather than a button. -->
                    <div class="presets">
                        <button type="button" class="btn-line preset" data-preset="none">None</button>
                        <button type="button" class="btn-line preset" data-preset="info-type">Basic</button>
                        <button type="button" class="btn-line preset" data-preset="info-type info-callsign">Spotter</button>
                        <span class="preset-state" id="preset-custom">Custom</span>
                    </div>
                    <div id="info-fields" class="grid3">
                        %INFO_FIELDS%
                    </div>
                </fieldset>

                </div><!-- /sec -->

                <div class="sec" data-sec="alerts">

                <fieldset>
                    <label class="stack">
                        <span>Watch (callsign / tail / ICAO / type, comma-separated):</span>
                        <textarea name="watchlist" rows="2">%WATCHLIST%</textarea>
                    </label>
                    <label class="field mt">
                        <span>ntfy.sh topic (phone alerts):</span>
                        <input name="ntfy-topic" value='%NTFY_TOPIC%' class="grow">
                    </label>
                    <!-- Both of these are learned AT THIS FIELD, which is why they are here
                         and not only on the support page. The first is the mistake somebody
                         is about to make while looking at this box; the second is the one
                         they cannot detect afterwards. -->
                    <span class="hint mt">Alerts need a trigger &mdash; tick one below, or add to the watch list.</span>
                    <span class="hint">Anyone with this topic can read your alerts. Treat it like a password; a short name is one people guess.</span>
                    <!-- The honest advice on a leaked topic is to change it, so the
                         device offers the change rather than leaving the customer to
                         invent a replacement -- which is how a 50-bit topic becomes
                         "planes2". Regenerated on the DEVICE with esp_random(), not in
                         the browser, so it is the same generator that made the first one. -->
                    <label class="check mt"><input name="ntfy-regen" type="checkbox"><span>Generate a new topic when I save (re-subscribe your phone afterwards)</span></label>
                    <div class="grid2 mt">
                        <label class="check"><input name="mil-show" type="checkbox" %MIL_SHOW%><span>Highlight military</span></label>
                        <label class="check"><input name="mil-alert" type="checkbox" %MIL_ALERT%><span>Alert on military (ntfy)</span></label>
                        <label class="check"><input name="heli-show" type="checkbox" %HELI_SHOW%><span>Highlight helicopters</span></label>
                        <label class="check"><input name="spc-show" type="checkbox" %SPC_SHOW%><span>Highlight special flights</span></label>
                        <label class="check"><input name="emg-alert" type="checkbox" %EMG_ALERT%><span>Alert on emergency squawk (ntfy)</span></label>
                        <label class="check"><input name="tones" type="checkbox" %TONES%><span>Alert tones (speaker models)</span></label>
                    </div>
                    <span class="hint mt">Detected from the live feed. On the radar: orange &ldquo;MIL&rdquo;, blue &ldquo;SPC&rdquo; (rescue, police, NASA, test), violet &ldquo;HELI&rdquo;.</span>
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
                    <span class="hint mt">Edge ring while a military (orange) or emergency-squawk (red) contact is in range, with a brief flash when it arrives.</span>
                    <div class="field mt">
                        <label class="check"><input name="lookup" type="checkbox" %LOOKUP%><span>&ldquo;Look up!&rdquo; overhead alert within</span></label>
                        <input name="lookup-dist" type="number" min="0.5" step="0.5" value='%LOOKUP_DIST%' class="w6">
                        <label class="check"><input name="lookup-alert" type="checkbox" %LOOKUP_ALERT%><span>also ntfy</span></label>
                    </div>
                    <span class="hint mt">Cyan &ldquo;LOOK UP&rdquo; ring when a contact passes within that distance.</span>
                </fieldset>

                </div><!-- /sec -->

                <div class="sec" data-sec="follow">

                <!-- FOLLOW MODE. Its own block, deliberately not folded into
                     "Watchlist & alerts": a watchlist is a category of aircraft you
                     find interesting, and this is one aeroplane with a person in it.
                     The two read differently and are configured for different
                     reasons, so they get different boxes. (spec 14) -->
                <fieldset>
                    <label class="field">
                        <span>Follow (tail / callsign / ICAO hex):</span>
                        <input name="follow" value='%FOLLOW%' class="grow">
                    </label>
                    <span class="hint mt">Names ONE aircraft &mdash; tail number, callsign, or ICAO hex. Leave it empty and there is no Follow screen.</span>
                    <span class="hint">Shows where it is, where it has been, and whether it is airborne, down, or simply out of receiver coverage.</span>
                    <div class="grid2 mt">
                        <label class="check"><input name="follow-track" type="checkbox" %FOLLOW_TRACK%><span>Draw the flight track</span></label>
                        <label class="check"><input name="follow-up" type="checkbox" %FOLLOW_UP%><span>Alert when it takes off (ntfy)</span></label>
                        <label class="check"><input name="follow-down" type="checkbox" %FOLLOW_DOWN%><span>Alert when it lands (ntfy)</span></label>
                        <label class="check"><input name="follow-lost" type="checkbox" %FOLLOW_LOST%><span>Alert when the signal is lost (ntfy)</span></label>
                    </div>
                    <!-- The asymmetry IS the argument (15), and it is worth explaining
                         rather than just defaulting: a missed lost-alert costs mild
                         worry, an unwanted one costs panic. -->
                    <span class="hint mt">Take-off and landing travel together; a landing alert only makes sense with its take-off. Signal-lost is off &mdash; losing signal is normal.</span>
                    <span class="hint">Alerts name the aircraft, so keep the topic private.</span>
                    <span class="hint">Flying with a pilot? Set the distance unit to nmi.</span>
                </fieldset>

                </div><!-- /sec -->

                <!-- The logbook toggle is the master switch for what the Collection
                     tab renders, and the leaderboard opt-in publishes that same
                     collection. Both sat under "Location & Radar", so turning the
                     logbook off emptied a tab you were not looking at. A section is
                     a SET of blocks, so moving them costs nothing but this boundary
                     -- no markup moves and the form stays one whole-form POST. -->
                <div class="sec" data-sec="collection">

                <div class="stand">%LB_STANDING%</div>

                <!-- THE CALLOUT IS STATIC NOW, not generated into #col. It has to sit
                     ABOVE the two switches, and #col is rendered below them -- so
                     leaving it inside the fetched markup would have put it six
                     screens from the thing it introduces. -->
                <span class="hint">Seeing an aircraft is your antenna. Claiming it is you &mdash; open a contact&rsquo;s card on the device to claim its type, operator, country and airports at once.</span>

                <details class="auto">
                    <summary>Spotting logbook <input name="logbook" type="checkbox" %LOGBOOK%></summary>
                    <span class="hint">A lifelist of every aircraft type, airline, country and route airport seen overhead, on the Stats screen. Unclaimed ones show a gold &ldquo;NEW&rdquo; &mdash; tap to claim. Adds a little network traffic. Download: <a href="/logbook.json?download=1" target="_blank" rel="noopener">logbook.json</a>.</span>
                </details>

                <details class="auto">
                    <summary>Spotting leaderboard <input name="lb-enabled" type="checkbox" %LB_ENABLED%></summary>
                    <label class="field">
                        <span>Spotter name:</span>
                        <input name="lb-name" value='%LB_NAME%' maxlength="24" placeholder="e.g. Redmond Radar" class="grow">
                    </label>
                    <span class="hint mt">Opt in to the public %LB_LINK%. Only counts and your type list leave the device &mdash; never your location, never which flights you saw. First device to claim a name owns it.</span>
                </details>

                <!-- THE WALL GOES LAST. It was ~460 chips ABOVE the two settings it
                     belongs to, putting the logbook switch at y=5,390 px on a phone --
                     six screens down, with Save below that. Display-only markup, so it
                     is safe inside the form: it contributes no named control. -->
                <div id="col"><span class="hint">Loading your collection&hellip;</span></div>

                </div><!-- /sec -->

                <div class="sec" data-sec="network">
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
                    <span class="hint mt">Publishes a retained &ldquo;&lt;base&gt;/summary&rdquo; to your broker. Home Assistant finds the sensors via auto-discovery.</span>
                </details>

                </div><!-- /sec -->

                <!-- Every section that contains a control needs the savebar. Moving
                     the logbook/leaderboard blocks to Collection without adding it
                     here left that tab with toggles and no way to apply them. -->
                <div class="sec" data-sec="location display labels alerts follow network collection">
                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
                </div><!-- /sec -->
            </form>

                <div class="sec" data-sec="about">
                <span class="hint">Aircraft data: <a href="https://adsb.fi" target="_blank" rel="noopener">adsb.fi</a>; <a href="https://adsb.lol" target="_blank" rel="noopener">adsb.lol</a> &copy; contributors, <a href="https://opendatacommons.org/licenses/odbl/1-0/" target="_blank" rel="noopener">ODbL 1.0</a>; <a href="https://github.com/Mictronics/aircraft-database" target="_blank" rel="noopener">Mictronics</a>, <a href="https://opendatacommons.org/licenses/by/1-0/" target="_blank" rel="noopener">ODC-By 1.0</a>; <a href="https://github.com/sdr-enthusiasts/plane-alert-db" target="_blank" rel="noopener">plane-alert-db</a>, <a href="https://opendatacommons.org/licenses/odbl/1-0/" target="_blank" rel="noopener">ODbL 1.0</a>; photos from <a href="https://commons.wikimedia.org" target="_blank" rel="noopener">Wikimedia Commons</a>. %CREDITS_LINK%</span>

                    <div class="grid2">
                        <div class="kv"><span>Device</span><b>%DEVICE_NAME%.local</b></div>
                        <div class="kv"><span>Address</span><b>%DEVICE_IP%</b></div>
                        <div class="kv"><span>WiFi signal</span><b>%WIFI_RSSI% dBm</b></div>
                        <div class="kv"><span>Firmware</span><b>v%FW_VERSION%</b></div>
                    </div>
                    <!-- THE SENTENCE SUPPORT NEEDS TO BE ABLE TO SAY. A customer who
                         can find this page can answer "what does your screen look
                         like" without owning a camera or knowing what a framebuffer
                         is. Named here rather than kept for the bench, because the
                         three display faults that prompted it were all reported by
                         customers. -->
                    <span class="hint mt">Screen copy: <a href="/diag/fb.html">%DEVICE_NAME%.local/diag/fb.html</a> shows exactly what the device is displaying. Press and hold the picture to save it (iPhone: Save to Photos; Android: Download image, then Keep if Chrome warns), and send it to support.</span>
                    <div class="foot mt">
                        <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                        %CREDITS_LINK%
                    </div>
                    <div class="hint mt">Forgets this network and restarts into setup. Location, settings and logbook are kept.</div>
                    <div class="mt"><button type="button" id="resetwifi" class="btn-danger">Reset WiFi</button></div>

                    <!-- ------------------------------------------------------------------
                         FACTORY RESET, VISUALLY SEPARATED FROM THE ONE ABOVE.

                         The rule and the divider are load-bearing. These two controls have
                         similar names and wildly different consequences, and a customer
                         scanning for "the reset button" will find whichever is nearer. So
                         the destructive one is below a line, in its own block, and cannot
                         be pressed at all until a word has been typed.

                         The export link is inside the confirmation panel rather than beside
                         it: it is only shown to somebody who has already opened the thing
                         that will delete the logbook, which is the exact moment the offer
                         to save a copy is worth anything.
                         ------------------------------------------------------------------ -->
                    <hr class="mt" style="border:0;border-top:1px solid #333;margin:18px 0">
                    <div class="hint">Erases everything this device knows about you &mdash; logbook, location, leaderboard opt-in and name, and the WiFi network &mdash; and restarts into setup. This cannot be undone.</div>
                    <div class="mt"><button type="button" id="factoryopen" class="btn-danger">Factory reset&hellip;</button></div>
                    <div id="factorypanel" class="mt" style="display:none;border:1px solid #ff4d4d;border-radius:6px;padding:12px">
                        <div class="hint">Save your logbook first: once erased there is no other copy.</div>
                        <div class="hint mt">Type <b>RESET</b> to enable the button:</div>
                        <input type="text" id="factoryword" autocomplete="off" autocapitalize="characters"
                               spellcheck="false" placeholder="RESET" style="max-width:10em">
                        <div class="mt">
                            <button type="button" id="factorygo" class="btn-danger" disabled>Erase everything</button>
                            <button type="button" id="factorycancel">Cancel</button>
                        </div>
                    </div>
                </div>

                </div><!-- /content -->
            </div><!-- /shell -->
        </fieldset>
)" CONFIG_SHELL_JS R"(
        <script>
            // cap the radius at ~2 degrees of scan box (222 km / 138 mi) to stay
            // within OpenSky's rate-limit area, swapping the limit with the unit
            const radiusInput = document.getElementById('radius');
            const radiusUnit = document.getElementById('radius-unit');
            // Mirrors include/DisplayUnits.h. Both are exact by definition.
            // Convert VIA kilometres rather than unit-to-unit: three units would
            // otherwise need six directed branches, and the two-way version this
            // replaced was already the shape that makes a missed case render one
            // unit under another's label.
            const KM_PER = { km: 1, mi: 1.609344, nmi: 1.852 };
            // ~2 degrees of scan box, to stay inside OpenSky's rate-limit area.
            const MAX_KM = 222;
            let prevUnit = radiusUnit.value;
            function updateRadiusMax() {
                radiusInput.max = String(Math.floor(MAX_KM / KM_PER[radiusUnit.value]));
            }
            radiusUnit.addEventListener('change', function() {
                // Keep the real-world distance the same across the swap.
                const value = parseFloat(radiusInput.value);
                if (!isNaN(value)) {
                    const km = value * KM_PER[prevUnit];
                    radiusInput.value = Math.round((km / KM_PER[radiusUnit.value]) * 10) / 10;
                }
                prevUnit = radiusUnit.value;
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
            // "Aircraft details" has no default on purpose, so it must be an explicit
            // pick before a local-receiver setup can be saved. `required` is toggled
            // with visibility rather than set in the markup: a required control inside
            // a display:none block still blocks submit, and the browser cannot focus it
            // to say why -- so leaving it always-on would wedge the form for cloud and
            // OpenSky users with an error they cannot see or fix.
            const localDetails = document.getElementById('local-details');
            function syncDataSource() {
                const v = dataSource.value;
                openskyFields.style.display = v === 'opensky' ? '' : 'none';
                localFields.style.display = v === 'local' ? '' : 'none';
                if (cloudFields) cloudFields.style.display = v === 'cloud' ? '' : 'none';
                if (localDetails) localDetails.required = (v === 'local');
            }
            dataSource.addEventListener('change', syncDataSource);
            syncDataSource();

            // THERE IS NO DIMMER HERE ANY MORE, and the story is worth the six
            // lines because the shape recurs.
            //
            // A dimmer used to live here: it greyed the field grid when the master
            // toggle was off, by writing style.opacity INLINE and listening for
            // `change` on the checkbox. When the preset row replaced the master
            // switch it got a second dimmer, a class. The two did not merely
            // duplicate each other -- the old one DEFEATED the new one, twice over:
            // an inline style outranks any stylesheet rule, and setting .checked
            // from script fires no `change`, so its "opacity: 1" from page load
            // stood forever. Tapping None set the flag, added the class, lit the
            // chip, and changed nothing the customer could see. It was reported as
            // "the None chip is not working", which it was, for a reason no part of
            // the new code contained.
            //
            // Both are gone now: None unticks the boxes instead, so the state is
            // legible without anything needing to be dimmed at all.

            // ---- collection view -------------------------------------------------
            // THE DEVICE SHIPS DATA; THE BROWSER RENDERS IT. Building this list as
            // HTML on the ESP32 would mean one contiguous String of roughly 32 KB at
            // full caps, on the async task, against a largest-free-block that was
            // measured at 36-44 KB with TLS competing for the same heap. It would
            // work on a small logbook and fail on a full one -- the worst failure
            // shape there is. This JS lives in flash (streamed to the browser 1 KB
            // at a time like the rest of the page) and costs no heap at all; the
            // only heap on the device is /logbook.json, which is itself chunked.
            //
            // Lazy: nothing is fetched until the section is actually opened, so the
            // common visit (set location, save) never pays for it.
            const col = document.getElementById('col');
            let colLoaded = false;
            const esc = function (s) {
                const d = document.createElement('div');
                d.textContent = s == null ? '' : String(s);
                return d.innerHTML;
            };
            // Claimed things are solid gold; unclaimed are outlined and dim. The
            // difference has to be obvious at a glance -- the whole point of the
            // page is showing someone the gap they could go and close.
            const chip = function (label, claimed, title) {
                const bg = claimed ? '#ffd200' : 'transparent';
                const fg = claimed ? '#141414' : '#969696';
                const bd = claimed ? '#ffd200' : '#5a5a5a';
                return '<span title="' + esc(title) + '" style="display:inline-block;margin:2px;padding:2px 7px;' +
                    'border:1px solid ' + bd + ';border-radius:10px;font-size:.78rem;' +
                    'background:' + bg + ';color:' + fg + '">' + esc(label) + '</span>';
            };
            // OPERATOR NAMES ARRIVE IN MIXED CASE and the wall reads as two lists:
            // measured on a real board, 314 shouty against 163 mixed. The registry
            // sends "ALASKA AIRLINES INC" while other sources send "Air Canada".
            //
            // UPPERCASE, WITH NO EXCEPTIONS. The first version title-cased and kept a
            // list of tokens that should stay capitalised -- LLC, USDA, and so on.
            // That list is always one short: it turned "SRC LEASING LLC" into "Src
            // Leasing LLC" and would have needed an entry for every acronym the FAA
            // registry has ever emitted. One rule that is occasionally ugly beats a
            // list that is occasionally wrong, and it cannot rot.
            //
            // RENDER-ONLY, and that is load-bearing rather than tidy. The stored name
            // IS the logbook's map key -- claims are filed under that exact spelling
            // (see adoptTruncatedOperator in Logbook.cpp, and the re-keying migration
            // the 24 -> 40 widening needed). Touching the key would orphan every claim
            // filed under the old spelling. This changes the label and nothing else,
            // which is also why it cannot fix the duplicate entries it reveals.
            const tidyCase = function (name) { return name.toUpperCase(); };
            // A flex-grow pair rather than a width percentage: the page is a C++ raw
            // string literal and the template processor claims the percent sign.
            const bar = function (claimed, total) {
                const rest = Math.max(0, total - claimed);
                return '<div style="display:flex;height:8px;border-radius:4px;overflow:hidden;' +
                    'background:#3c3c3c;margin:.4rem 0">' +
                    '<div style="flex-grow:' + claimed + ';background:#ffd200"></div>' +
                    '<div style="flex-grow:' + rest + '"></div></div>';
            };
            // truncAt: the store-time length cap for this category's key, or 0 for
            // categories that are never truncated (codes). Kept in step with
            // Logbook.h's MAX_OP_LEN / MAX_CN_LEN by hand -- the page is a C++ raw
            // string literal, so the constants cannot be interpolated in, and the
            // percent sign the template processor would need is already claimed.
            const section = function (title, items, keyName, claimedN, truncAt, nameCase) {
                if (!items || !items.length) return '';
                // "N claimed of M seen", never a bare "N of M". M is how many
                // entries this device has STORED, which is not the same as what
                // flew over. v5 raised the caps (three of four were reachable in a
                // WEEK) and made a full store evict its dullest unclaimed entry
                // instead of refusing, so M can now go DOWN as well as up while the
                // sky keeps delivering. Labelling the second number is the
                // difference between a comparison of two counts (true) and a
                // progress bar toward a total (not).
                let h = '<div style="margin:.9rem 0 .2rem"><b>' + esc(title) + '</b> ' +
                    '<span class="hint">' + claimedN + ' claimed of ' + items.length + ' seen</span></div>';
                h += bar(claimedN, items.length);
                const sorted = items.slice().sort(function (a, b) {
                    if (a.claimed !== b.claimed) return a.claimed ? -1 : 1;
                    return String(a[keyName]).localeCompare(String(b[keyName]));
                });
                // ONE CHIP, built once, so the claimed list and the collapsed
                // remainder cannot drift apart in how they render.
                //
                // MARK A TRUNCATED NAME. Names are cut to MAX_OP_LEN / MAX_CN_LEN
                // at STORE time, so the full text is already gone by the time it
                // reaches here -- "CSC DELAWARE TRUST CO TR" is genuinely all the
                // device has. An ellipsis is the honest option left: the reader can
                // see the name is clipped instead of being shown a wrong one as if
                // it were complete. Length-equals-cap is a heuristic, so a name that
                // happens to be exactly cap characters gets a spurious ellipsis --
                // a strictly smaller error than the current one, and cosmetic in a
                // way the current one is not.
                const chipFor = function (it) {
                    const when = it.claimed ? ('claimed ' + (it.claimedOn || 'date unknown'))
                        : ('seen ' + (it.first || 'date unknown') + ' - not claimed yet');
                    const n = it.count ? (' x' + it.count) : '';
                    let name = String(it[keyName]);
                    if (nameCase) name = tidyCase(name);
                    if (truncAt && name.length >= truncAt) name += '…';
                    return chip(name + n, it.claimed, when);
                };
                // CLAIMED IN FULL, THE REST BEHIND ONE LINE. On a real board this is
                // 49 claimed against 428 unclaimed -- 90 percent of the wall is grey
                // chips for things nobody has done yet, and they were burying the two
                // settings and the Save button under six screens.
                //
                // A plain <details> rather than a JS toggle: it is closed by default,
                // keyboard-operable for free, and the auto-open pass only touches
                // details.auto, so nothing will expand these behind the customer's
                // back.
                const mine = sorted.filter(function (i) { return i.claimed; });
                const rest = sorted.filter(function (i) { return !i.claimed; });
                h += '<div>';
                for (const it of mine) h += chipFor(it);
                h += '</div>';
                if (rest.length) {
                    h += '<details><summary class="hint" style="cursor:pointer">and ' +
                        rest.length + ' more seen</summary><div>';
                    for (const it of rest) h += chipFor(it);
                    h += '</div></details>';
                }
                return h;
            };
            const loadCollection = function () {
                if (colLoaded) return;
                colLoaded = true;
                fetch('/logbook.json').then(function (r) { return r.json(); }).then(function (d) {
                    const c = d.counts || {}, k = d.claimed || {};
                    // THE HEADLINE GOES FIRST. It was the LAST line on the page,
                    // under ~460 chips. It is the one number that answers "is this
                    // thing working at all", so it sits above the bars it sums up.
                    // The callout that used to open this block is static markup now,
                    // above the two switches -- see the section html.
                    let h = '<div class="hint" style="margin-bottom:.3rem"><b>' +
                        (d.contacts || 0) + '</b> contacts seen in total.</div>';
                    h += section('Types', d.types, 'code', k.types || 0, 0);
                    // "Operators", not "Airlines". The registry returns the
                    // REGISTERED OWNER, and outside airline traffic that is a person or a
                    // single-airframe LLC -- a real board's list reads ANDREW
                    // KLEMISH, BORKOSKI BRIAN, 84 ALPHA KILO LLC. Calling that
                    // "Airlines" was the least honest label on the page. The JSON
                    // key stays `airlines`: it is the wire field the leaderboard
                    // submit sends, so renaming it is a Worker change, not a copy
                    // change.
                    h += section('Operators', d.airlines, 'name', k.airlines || 0, 40, true); // MAX_OP_LEN
                    h += section('Countries', d.countries, 'name', k.countries || 0, 32); // MAX_CN_LEN
                    h += section('Airports', d.airports, 'code', k.airports || 0, 0);
                    const rec = d.records || {};
                    const bits = [];
                    if (rec.high) bits.push('Highest ' + esc(rec.high.callsign) + ' ' + rec.high.value + ' ' + esc(rec.high.unit));
                    if (rec.fast) bits.push('Fastest ' + esc(rec.fast.callsign) + ' ' + rec.fast.value + ' ' + esc(rec.fast.unit));
                    if (rec.near) bits.push('Closest ' + esc(rec.near.callsign) + ' ' + rec.near.value + ' ' + esc(rec.near.unit));
                    if (bits.length) h += '<div style="margin:.9rem 0 .2rem"><b>Records</b></div><div class="hint">' + bits.join('<br>') + '</div>';
                    if (!d.types || !d.types.length) {
                        /* NEVER TELL SOMEONE TO ENABLE WHAT IS ALREADY ENABLED.
                           This used to print "Turn on the spotting logbook above"
                           unconditionally -- including while the box was ticked,
                           the logbook was running, and claims were landing. It
                           sent a customer round a checkbox they had already set,
                           which is the worst possible instruction on the one page
                           people read when something is not working.
                           The empty state has two causes and they need different
                           sentences: not switched on yet, or on and still filling. */
                        const lbOn = document.querySelector('input[name=logbook]');
                        h = (lbOn && lbOn.checked)
                            ? '<span class="hint">Logbook is on &mdash; aircraft appear here as you spot them.</span>'
                            : '<span class="hint">Turn on the spotting logbook below to start a collection.</span>';
                    }
                    col.innerHTML = h;
                }).catch(function () {
                    colLoaded = false; // let a retry happen on the next open
                    col.innerHTML = '<span class="hint">Could not load the logbook from the device.</span>';
                });
            };
            // ---- sidebar navigation ---------------------------------------------
            // A section is a SET of blocks, not one range: `data-sec` holds a
            // space-separated list, and Location's two halves sit either side of the
            // data-source block. That is what let the whole layout land without moving
            // a single line of existing markup -- and moving markup across the
            // #ifdef FEATURE_CLOUD_FEED boundaries in there is exactly the kind of edit
            // that breaks one build config and not the others.
            //
            // Sections are shown and hidden with CSS. They are NOT separate forms and
            // must never become separate forms: `display:none` leaves a field in
            // FormData, so the whole page still posts as one body. `disabled` would
            // not -- which is why nothing here ever disables an input, and why
            // scripts/check-config-form.py fails the build if anything starts to.
            const secs = document.querySelectorAll('.sec');
            const navs = document.querySelectorAll('.navb');
            // Where showSection lands when a name matches nothing. Location,
            // because the device this protects is a first-run one with no
            // location saved -- which is why its landing group broke at all.
            const FALLBACK_SECTION = 'location';
            function showSection(name) {
                let hit = false;
                for (const el of secs) {
                    const on = (el.dataset.sec || '').split(' ').indexOf(name) >= 0;
                    if (on) hit = true;
                    el.classList.toggle('on', on);
                }
                // Nothing matched: every section is off and the page is BLANK.
                // A group was renamed in one of the places that name them.
                // Why this is a real hazard, and its CI half, are in the header of
                // scripts/check-config-form.py -- this comment ships to the phone.
                if (!hit && navs.length) {
                    let fb = '';
                    for (const el of secs) {
                        if ((el.dataset.sec || '').split(' ').indexOf(FALLBACK_SECTION) >= 0) fb = FALLBACK_SECTION;
                    }
                    name = fb || navs[0].dataset.go;
                    for (const el of secs) {
                        el.classList.toggle('on', (el.dataset.sec || '').split(' ').indexOf(name) >= 0);
                    }
                }
                for (const b of navs) b.classList.toggle('on', b.dataset.go === name);
                if (name === 'collection') loadCollection();
                try { history.replaceState(null, '', '#' + name); } catch (e) { /* file:// etc. */ }
            }
            for (const b of navs) {
                b.addEventListener('click', function () { showSection(b.dataset.go); });
            }
            // PRESETS for the label fields. The asymmetry is the whole design:
            //
            //   refresh()  READS the boxes and lights whichever chip matches
            //   click      WRITES them, and only ever from a tap
            //
            // refresh() runs on load; nothing else does. A preset applied at render
            // would rewrite a saved selection for anyone who opened the page to look
            // at something else, and because the form posts in full the next Save
            // would make that silent rewrite permanent -- the same shape as a
            // defaultOn reaching a device that has already saved, which this
            // codebase paid for once in #238.
            //
            // "NONE" IS THE EMPTY PRESET. It unticks every field, which is what a
            // customer reading the word expects to see happen. An earlier version
            // kept the selection and greyed the grid instead, so that switching
            // labels off and on again returned the exact set -- that round trip is
            // deliberately gone, because "None" that leaves thirteen ticks on screen
            // reads as a control that did not work, and it was reported as one.
            //
            // The flag follows the boxes rather than leading them: infotext is false
            // exactly when nothing is ticked. One state on screen, not two.
            const presetBtns = document.querySelectorAll('.preset');
            const customChip = document.getElementById('preset-custom');
            const flag = document.getElementById('infotext');
            const grid = document.getElementById('info-fields');
            if (presetBtns.length && customChip && flag && grid) {
                const boxes = function () {
                    return Array.prototype.slice.call(
                        grid.querySelectorAll('input[type=checkbox]'));
                };
                const keysOf = function (b) {
                    return b.dataset.preset === 'none'
                        ? [] : b.dataset.preset.split(' ').filter(Boolean);
                };
                const ticked = function () {
                    return boxes().filter(function (i) { return i.checked; })
                                  .map(function (i) { return i.name; }).sort().join(' ');
                };
                const refresh = function () {
                    const now = ticked();
                    flag.checked = now.length > 0;
                    let matched = false;
                    for (const b of presetBtns) {
                        const hit = keysOf(b).slice().sort().join(' ') === now;
                        if (hit) matched = true;
                        b.classList.toggle('on', hit);
                    }
                    customChip.classList.toggle('on', !matched);
                };
                for (const b of presetBtns) {
                    b.addEventListener('click', function () {
                        const keys = keysOf(b);
                        for (const i of boxes()) i.checked = keys.indexOf(i.name) >= 0;
                        refresh();
                    });
                }
                for (const i of boxes()) i.addEventListener('change', refresh);

                // THE ONE TIME THIS PAGE WRITES A CHECKBOX ON LOAD, and it is
                // here to stop the page lying rather than to set a preference.
                //
                // A device saved before this change can hold infotext=false with
                // fields still ticked. The radar draws NOTHING in that state --
                // displayInfoText gates both the draw and the tap target -- while
                // the page would show five ticks and light Custom. The customer
                // would be looking at a list of fields their radar is not drawing,
                // and at chips describing a selection that has no effect.
                //
                // So when the SERVED flag is false, the boxes are cleared to match
                // what the radar was actually showing: nothing. It reads the
                // attribute, not the property, because the attribute is what the
                // device sent. Nothing is persisted until the customer saves, and
                // it cannot fire twice -- after one save the flag follows the
                // boxes and the two can no longer disagree.
                if (!flag.hasAttribute('checked')) {
                    for (const i of boxes()) i.checked = false;
                }
                refresh();
            }

            // The landing section is decided ON THE DEVICE and arrives in the markup
            // (body[data-start]), not computed here: a first-run customer with no
            // location set must land on Location & Radar, and doing that in JS would
            // paint the Collection first and then jump -- which over a slow AP link is
            // the moment somebody decides the page is broken. A #hash still wins, so
            // links into a section keep working.
            const startFromHash = (location.hash || '').replace('#', '');
            const valid = ['location', 'display', 'labels', 'alerts', 'follow', 'network', 'collection', 'about'];
            showSection(valid.indexOf(startFromHash) >= 0 ? startFromHash
                        : (document.body.dataset.start || 'collection'));
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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope EAM</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(17,24,39)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(34,197,94)' stroke-width='1'/><circle cx='8' cy='8' r='1.7' fill='rgb(34,197,94)'/></svg>">
        <style>:root{--ink:#22c55e;--line:#22c55e;--dim:#7f9e91;--btn:#22c55e;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <label class="field">
                    <span>EAM feed base URL:</span>
                    <input name="eam-base-url" value='%EAM_BASE_URL%' placeholder="https://eam.example.com" class="grow">
                </label>
                <span class="hint">The valar-eam-feed backend this device polls for EAM / Skyking / tempo / propagation / launch data.</span>

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional. Used for propagation day/night and the command-post bearing/distance. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

                <details class="auto">
                    <summary>Command-post watch</summary>
                    <label class="field">
                        <span>Source:</span>
                        <select id="abncp-source" name="abncp-source" class="grow">
                            <option value="backend" %ABNCP_BACKEND%>Valar feed &mdash; aggregated (no setup)</option>
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

                %USB_OPEN%
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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Spacescope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(8,12,28)'/><circle cx='8' cy='8' r='2' fill='rgb(120,200,255)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(120,200,255)' stroke-width='0.8'/><circle cx='13' cy='4' r='1' fill='rgb(255,255,255)'/></svg>">
        <style>:root{--ink:#7dd3fc;--line:#38bdf8;--dim:#7fa8bd;--btn:#38bdf8;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional, but unlocks the location-aware screens: next visible ISS pass, local aurora odds, and the solar night auto-dim. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope Seismic</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(24,14,4)'/><path d='M1 8 L4 8 L5 3 L7 13 L9 6 L10.5 8 L15 8' fill='none' stroke='rgb(255,170,0)' stroke-width='1.2'/></svg>">
        <style>:root{--ink:#fcd34d;--line:#fbbf24;--dim:#d97706;--btn:#fbbf24;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Your location centres the quake radar, the "near me" feed and alerts, and the solar night auto-dim. Without it you still get the worldwide list and stats. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope Birding</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(8,20,8)'/><circle cx='6.5' cy='7' r='3' fill='rgb(150,220,130)'/><circle cx='7.5' cy='6.2' r='0.7' fill='rgb(8,20,8)'/><path d='M9 7 L13 6 L10 8 Z' fill='rgb(255,215,90)'/></svg>">
        <style>:root{--ink:#86efac;--line:#22c55e;--dim:#7f9e91;--btn:#4ade80;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

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
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Your location centres the sightings radar, the nearby feeds, and alerts. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Reelscope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(4,16,22)'/><path d='M2 8 Q5 4 9 8 Q5 12 2 8 Z' fill='rgb(120,220,255)'/><circle cx='4' cy='7.4' r='0.6' fill='rgb(4,16,22)'/><path d='M9 8 L13 5 L12 8 L13 11 Z' fill='rgb(120,230,140)'/></svg>">
        <style>:root{--ink:#a5f3fc;--line:#06b6d4;--dim:#0891b2;--btn:#22d3ee;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

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
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Location drives on-device solunar/sun/moon, the keyless weather feed, and the night auto-dim. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Claudescope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(28,18,12)'/><g stroke='rgb(217,119,87)' stroke-width='1.4' stroke-linecap='round'><path d='M8 3 L8 13'/><path d='M3.7 5.5 L12.3 10.5'/><path d='M3.7 10.5 L12.3 5.5'/></g></svg>">
        <style>:root{--ink:#fed7aa;--line:#fb923c;--dim:#ea580c;--btn:#fb923c;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <label class="field">
                    <span>Sidecar URL:</span>
                    <input name="cl-base-url" value='%CL_BASE_URL%' placeholder="http://192.168.1.50:8080" class="grow">
                </label>
                <span class="hint">Run <code>claudescope-sidecar</code> on a machine on your LAN (see tools/claudescope-sidecar), then point this at it. The sidecar holds your Claude token; the device only ever sees usage numbers. Until this is set, the device shows the setup splash.</span>

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional. Location drives only the night auto-dim and the local clock; usage numbers work without it. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>

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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Speedscope</title>
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(20,14,2)'/><path d='M2 12 A6 6 0 0 1 14 12' fill='none' stroke='rgb(255,176,40)' stroke-width='1.4'/><line x1='8' y1='12' x2='12' y2='6' stroke='rgb(255,60,40)' stroke-width='1.4'/><circle cx='8' cy='12' r='1' fill='rgb(255,176,40)'/></svg>">
        <style>:root{--ink:#fde68a;--line:#f59e0b;--dim:#d97706;--btn:#fbbf24;--text:#e6edea}</style>
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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

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
                            <input name="latitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                        </label>
                        <label class="field">
                            <span>Longitude:</span>
                            <input name="longitude" type="text" inputmode="text" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                        </label>
                    </div>
                    <span class="hint mt">Location is optional &mdash; it only drives the night auto-dim (sunset/sunrise at your spot). Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. West and south can be a minus sign <em>or</em> a letter &mdash; &ldquo;-121.315&rdquo; and &ldquo;121.315 W&rdquo; are the same place.</span>
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

namespace {

// Is anything listening on `port`? Answered by READING lwIP's own LISTEN list.
//
// THE PREVIOUS ATTEMPT AT THIS SHIPPED A DEAD CONFIG PAGE TO EVERY BOARD (#172).
// It probed by binding a plain BSD socket to the same port, reasoning "if I can bind
// it, nobody is listening". AsyncTCP does not use the socket layer -- it binds a RAW
// lwIP PCB -- so the two do not conflict the way they would on a normal host. The
// bind succeeded ALONGSIDE the live listener, the probe concluded nothing was there,
// and close() then tore the real listener down. A false negative that manufactured
// its own evidence.
//
// So this touches no socket at all. tcp_listen_pcbs is the list AsyncTCP's own
// listener is registered in, which makes it the actual question rather than a proxy
// for it, and walking it cannot disturb what it observes.
//
// The list belongs to the tcpip thread, so the walk holds the core lock. That is real
// here: CONFIG_LWIP_TCPIP_CORE_LOCKING=1 in this SDK, so LOCK_TCPIP_CORE() is a mutex
// and not a no-op. Without it we would be reading a list another task may be splicing.
bool AnyListenerOnPort(uint16_t port)
{
    bool found = false;
    LOCK_TCPIP_CORE();
    for (const struct tcp_pcb_listen* pcb = tcp_listen_pcbs.listen_pcbs;
         pcb != nullptr; pcb = pcb->next) {
        if (pcb->local_port == port) {
            found = true;
            break;
        }
    }
    UNLOCK_TCPIP_CORE();
    return found;
}

} // namespace

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
        // Saved location profiles (home / work / trip); the config-page JS loads
        // a slot into the lat/lon fields on "Use", persisted here as plain slots.
        const String loc0Name = HtmlEscape(prefs.getString("loc0-name", ""));
        const String loc0Lat  = HtmlEscape(prefs.getString("loc0-lat", ""));
        const String loc0Lon  = HtmlEscape(prefs.getString("loc0-lon", ""));
        const String loc1Name = HtmlEscape(prefs.getString("loc1-name", ""));
        const String loc1Lat  = HtmlEscape(prefs.getString("loc1-lat", ""));
        const String loc1Lon  = HtmlEscape(prefs.getString("loc1-lon", ""));
        const String loc2Name = HtmlEscape(prefs.getString("loc2-name", ""));
        const String loc2Lat  = HtmlEscape(prefs.getString("loc2-lat", ""));
        const String loc2Lon  = HtmlEscape(prefs.getString("loc2-lon", ""));
        const String radius = HtmlEscape(prefs.getString("radius", "100"));
        // isKey() probes without logging; a plain getString() on this not-yet-saved
        // key spams "nvs_get_str ... NOT_FOUND" on every page load until first save
        // DEFAULT IS mi. Decided 2026-08-26 while the store was still draft, so
        // there is no installed base to migrate and no cfg-rev cost -- buyers are
        // predominantly American. A device that stored ANY value keeps it: a
        // default only ever reaches keys that were never written.
        const String radiusUnit = HtmlEscape(prefs.isKey("radius-unit") ? prefs.getString("radius-unit", "mi") : "mi");
        const String openskyClientId = HtmlEscape(prefs.getString("opensky-id", ""));
        String openskySecret = HtmlEscape(prefs.getString("opensky-secret", ""));
#ifdef FEATURE_CLOUD_FEED
        // Cloud builds default the unset key to the proxy: new devices land on
        // Blipscope Cloud out of the box (AircraftManager::Initialise mirrors this).
        const String dataSource = HtmlEscape(prefs.isKey("data-source") ? prefs.getString("data-source", "cloud") : "cloud");
        const String cloudUrlCfg = HtmlEscape(prefs.isKey("cloud-url") ? prefs.getString("cloud-url", "") : "");
        String cloudKeyCfg = HtmlEscape(prefs.getString("cloud-key", ""));
        // ENROLLED means "this board holds a device key it did not have to be
        // told", i.e. the factory slot is populated -- by provision-device.py on
        // an assembled unit, or by /enroll-key on a self-flashed one. The
        // editable override is deliberately NOT consulted: a pasted shared key
        // is exactly the state this feature exists to move a board OUT of, and
        // counting it as enrolled would hide the thing we want to see.
        const bool enrolled = prefs.getString("cloud-key-fac", "").length() > 0;
        // THREE STATES, NOT TWO. never-enrolled / enrolled-and-working /
        // enrolled-but-REFUSED. The third is new (2026-08-13) and is the whole
        // point of the credential-recovery work: before it, a board whose key
        // stopped being accepted looked identical to a healthy one here, and the
        // owner's only symptom was a screen that had quietly stopped filling.
        //
        // A never-enrolled board must NOT be told to "re-verify" -- it has nothing
        // to re-do -- which is exactly why this is not a boolean.
        const bool refused = needsReverify;
        const String deviceIdCfg = DeviceIdentity::LeaderboardId();
#else
        const String dataSource = HtmlEscape(prefs.isKey("data-source") ? prefs.getString("data-source", "opensky") : "opensky");
#endif
        const String localUrl = HtmlEscape(prefs.getString("local-url", ""));
        // Detail-card source for a local receiver. Deliberately NO default: an unset
        // value renders the placeholder, so choosing is an explicit act. The firmware
        // treats unset as "off" (contacts nothing), which is the only fallback that
        // cannot surprise anyone -- nothing starts talking to us on its own.
        const String localDetails = prefs.getString("local-details", "");
        const String scanlineEnabled = HtmlEscape(prefs.getString("scanline", "true"));
        const String fadeEnabled = HtmlEscape(prefs.getString("fade", "true"));
        const String infoTextEnabled = HtmlEscape(prefs.getString("infotext", "true"));
        const String triangleEnabled = HtmlEscape(prefs.getString("triangle", "true"));
        const String airportsEnabled = HtmlEscape(prefs.isKey("airports") ? prefs.getString("airports", "true") : "true");
        const String airportsMin = HtmlEscape(prefs.isKey("airports-min") ? prefs.getString("airports-min", "all") : "all");
        const String trailEnabled = HtmlEscape(prefs.getString("trail", "true"));
        const String altColorEnabled = HtmlEscape(prefs.getString("altcolor", "true"));
        const String highlightEnabled = HtmlEscape(prefs.getString("highlight", "true"));
        const String autoDimEnabled = HtmlEscape(prefs.getString("autodim", "true"));
        const String nightClockOn = HtmlEscape(prefs.isKey("night-clock") ? prefs.getString("night-clock", "false") : "false");
        const String brightness = HtmlEscape(prefs.getString("brightness", "255"));
        // AUTO IS THE ABSENCE OF THE KEY, NOT A ZERO. (v11, 2026-09-09.)
        //
        // This used to default the FIELD to the nominal zone from longitude, which
        // reads as helpful and shipped a defect: `longitude` is the STORED one, so
        // on a factory-fresh device it is "" and `"".toFloat()` is 0. The field
        // rendered `0`, and the customer's first save -- the same save that sets
        // their location -- posted that 0 back as an explicit choice. From then on
        // `tz-offset` was explicitly zero forever, LocalOffset.h correctly honoured
        // it, and the longitude fallback could never run. A 03:00 "local" quiet-hour
        // reboot landed at 03:00 UTC = 20:00 Pacific, which is the exact complaint
        // the quiet hour was built to remove.
        //
        // So the field is EMPTY when the key is absent, and the derived value is
        // shown as a PLACEHOLDER instead. A placeholder is not submitted, so an
        // untouched field posts "" and the save below leaves the key absent. The
        // customer still sees what auto resolves to, which is the half that made
        // the old default worth having -- they can see what they would be
        // overriding before they override it.
        //
        // AN EXPLICIT "0" REMAINS COMPLETELY VALID and is stored like any other
        // value: somebody genuinely at UTC must be able to say so. What is fixed is
        // that a 0 nobody typed is no longer manufactured on their behalf.
        const String tzOffset = prefs.isKey("tz-offset")
            ? HtmlEscape(prefs.getString("tz-offset", ""))
            : String("");
        const int    tzAutoZone = (int)lround(longitude.toFloat() / 15.0);
        const String tzAuto = longitude.length() == 0
            ? String("Auto - set your location")
            : String("Auto - UTC") + (tzAutoZone >= 0 ? "+" : "") + String(tzAutoZone)
                  + " from location";
        const String radarUp = HtmlEscape(prefs.isKey("radar-up") ? prefs.getString("radar-up", "0") : "0");
        const String watchlist = HtmlEscape(prefs.getString("watchlist", ""));
        const String ntfyTopic = HtmlEscape(prefs.getString("ntfy-topic", ""));
        // isKey() guards keep the not-yet-saved reads from logging NVS NOT_FOUND
        const String milShow = HtmlEscape(prefs.isKey("mil-show") ? prefs.getString("mil-show", "true") : "true");
        const String milAlert = HtmlEscape(prefs.isKey("mil-alert") ? prefs.getString("mil-alert", "false") : "false");
        const String heliShow = HtmlEscape(prefs.isKey("heli-show") ? prefs.getString("heli-show", "false") : "false");
        const String spcShow = HtmlEscape(prefs.isKey("spc-show") ? prefs.getString("spc-show", "false") : "false");
        const String emgAlert = HtmlEscape(prefs.isKey("emg-alert") ? prefs.getString("emg-alert", "false") : "false");
        const String tonesOn = HtmlEscape(prefs.isKey("tones") ? prefs.getString("tones", "true") : "true");
        // visual alerts: defaults mirror AircraftManager::Initialise (emergency = ring, military = off)
        const String milVisual = HtmlEscape(prefs.isKey("mil-visual") ? prefs.getString("mil-visual", "off") : "off");
        const String emgVisual = HtmlEscape(prefs.isKey("emg-visual") ? prefs.getString("emg-visual", "ring") : "ring");
        const String visualNight = HtmlEscape(prefs.isKey("visual-night") ? prefs.getString("visual-night", "false") : "false");
        // Resolved through the SAME helper the device uses, rather than the bare
        // "false" literal that used to sit here. With the default flipped ON, a
        // page that kept its own copy would render the box unticked on a device
        // that is happily collecting -- the customer's two sources of truth
        // disagreeing, which is precisely how "it says one thing here and another
        // there" bugs get reported and never reproduced.
        const String logbookOn = HtmlEscape(
            configmigration::ResolveToggle(
                prefs.isKey("logbook") ? prefs.getString("logbook", "").c_str() : nullptr,
                configmigration::LOGBOOK_DEFAULT_ON)
            ? "true" : "false");
        const String lbEnabled = HtmlEscape(prefs.isKey("lb-enabled") ? prefs.getString("lb-enabled", "false") : "false");
        const String lbName = HtmlEscape(prefs.getString("lb-name", ""));
        // --- Follow Mode (14 / 15) ------------------------------------------
        // THESE DEFAULTS ARE FOREVER. New keys freeze the moment anyone saves the
        // form, and setting a location IS a whole-form save that every device must
        // do -- so today's values here are the values for everyone who ever owns
        // this. Changing one later costs a cfg-rev bump plus a migration in
        // include/ConfigMigration.h. Cheap now, expensive in a month.
        //
        // follow-lost is OFF and that asymmetry is the argument: a missed
        // lost-alert costs mild worry, an unwanted one costs panic. The screen
        // always shows the state; the phone only if asked.
        const String followTarget = HtmlEscape(prefs.getString("follow", ""));
        const String followTrack = HtmlEscape(prefs.isKey("follow-track") ? prefs.getString("follow-track", "true") : "true");
        const String followUp    = HtmlEscape(prefs.isKey("follow-up")    ? prefs.getString("follow-up", "true")    : "true");
        const String followDown  = HtmlEscape(prefs.isKey("follow-down")  ? prefs.getString("follow-down", "true")  : "true");
        const String followLost  = HtmlEscape(prefs.isKey("follow-lost")  ? prefs.getString("follow-lost", "false") : "false");
        // --- links to pages the CLOUD PROXY serves, not this device -------------
        // Both of these used to be (or were missing precisely because of) a
        // host-confusion bug: the config page is served by the DEVICE, so a
        // root-relative "/leaderboard" resolved to
        // http://blipscope-xxxxxx.local/leaderboard -- a route this server does not
        // have -- and 404'd for every user who opted in. The pages live on the
        // proxy. Build ABSOLUTE urls from the same base the feed itself uses: the
        // saved "cloud-url" override when set, else the compiled-in default. Doing
        // it from the feed's own base (rather than a hardcoded host) means these
        // links can never drift away from the backend the device is really talking
        // to -- staging boards get staging pages, production gets production.
        //
        // Both degrade to no-link rather than a dead link when no base is known (a
        // non-cloud build, or one with no CLOUD_FEED_BASE and no override): a link
        // that silently 404s is exactly what caused this.
        String lbLink = F("spotting leaderboard");
        String creditsLink;
#ifdef FEATURE_CLOUD_FEED
        {
            String cloudBase = CloudFeed::NormalizeBaseUrl(
                prefs.isKey("cloud-url") ? prefs.getString("cloud-url", "") : String(""));
            if (cloudBase.isEmpty()) cloudBase = CloudFeed::NormalizeBaseUrl(String(CLOUD_FEED_BASE));
            if (!cloudBase.isEmpty()) {
                // Edition-namespaced page path (docs/web-url-convention.md). The
                // old /leaderboard still 301s here, so a device that never takes
                // another OTA keeps a working link -- this just stops sending new
                // builds through the redirect.
                lbLink = "<a href='" + HtmlEscape(cloudBase + "/blipscope/leaderboard")
                       + "' target='_blank' rel='noopener'>spotting leaderboard</a>";
                // The credits page carries the photo attribution (CC-BY / CC-BY-SA
                // obligations) alongside the data-source lines above, so it needs to
                // be reachable, not just to exist.
                creditsLink = "Full credits, including aircraft photos: <a href='"
                            + HtmlEscape(cloudBase + "/credits")
                            + "' target='_blank' rel='noopener'>credits</a>.";
            }
        }
#endif
        // --- the Collection tab's standing block --------------------------------
        // Rendered here rather than fetched, because there is nothing to fetch: the
        // standing lives in AircraftManager on the loop task, which this async
        // handler cannot reach. The manager writes a compact "rank/total/points/
        // seasonRank/seasonPoints" record to NVS after each successful submit
        // (hourly, so the flash wear is nil) and this reads it back like any other
        // stored value. Every branch below is a state a real owner can be in, and
        // each says what to do next rather than just being empty.
        String lbStanding;
        {
            const bool on = lbEnabled == "true";
            const String rec = prefs.getString("lb-standing", "");
            String f[5];
            int nf = 0, start = 0;
            for (int k = 0; k <= (int)rec.length() && nf < 5; ++k) {
                if (k == (int)rec.length() || rec[k] == '/') {
                    f[nf++] = rec.substring(start, k);
                    start = k + 1;
                }
            }
            if (!on) {
                lbStanding = F("<b>Your collection is private.</b><br><span class='hint'>Turn on the "
                               "spotting leaderboard under Location &amp; Radar to compare claim counts "
                               "with other spotters. Only a display name and your counts are shared.</span>");
            } else if (nf < 3 || f[0].toInt() <= 0) {
                lbStanding = "<b>Opted in" + (lbName.isEmpty() ? String() : " as " + lbName) + ".</b><br>"
                           + F("<span class='hint'>No standing yet &mdash; the device submits about once an "
                               "hour. Claim a few aircraft and check back.</span>");
            } else {
                lbStanding = "<b>Rank #" + f[0] + (f[1].toInt() > 0 ? " of " + f[1] : String()) + "</b>"
                           + " &middot; " + f[2] + " pts";
                if (nf >= 5 && f[3].toInt() > 0)
                    lbStanding += "<br><span class='hint'>This season: #" + f[3] + ", " + f[4] + " pts</span>";
                // Say WHOSE number this is. It is the score the leaderboard sent
                // back in the last submit response, not one the device worked out
                // -- but the device submits about once an hour, so between submits
                // it legitimately trails the board page. Unlabelled, that gap reads
                // as two authorities disagreeing about your score, which is the one
                // impression a game must never give. Naming the source costs a line
                // and makes the lag self-explanatory instead of suspicious.
                lbStanding += "<div class='hint mt'>Scored by the leaderboard, not by this device "
                              "&mdash; as of the last hourly submit. See the " + lbLink + ".</div>";
            }
        }

        // --- which section the page lands on ------------------------------------
        // DECIDED HERE, not in the browser. A first-run owner with no location set
        // must land on Location & Radar: it is the one thing that has to happen in
        // their first ten minutes, and every one of the pilot units takes this path.
        // Choosing in JS would paint Collection first and then jump, and over a slow
        // AP link that flash of the wrong screen is where somebody decides the page
        // is broken and reboots the device mid-save.
        const bool haveLocation = !latitude.isEmpty() && !longitude.isEmpty();
        const String startSection = haveLocation ? F("collection") : F("location");

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
#if defined(FEATURE_USB_OPEN)
        // "Open on computer" (FEATURE_USB_OPEN, src/eam/UsbOpen.h). Built here rather
        // than in the page literal so a build without the USB keyboard shows nothing.
        const String usbOs = prefs.isKey("eam-usb-os") ? prefs.getString("eam-usb-os", "windows") : String("windows");
        const String usbEmpty = prefs.isKey("eam-usb-empty") ? prefs.getString("eam-usb-empty", "archive") : String("archive");
        auto sel = [](bool on) { return on ? " selected" : ""; };
        const String usbOpenHtml = String(
            "<fieldset><legend>Open on computer (USB)</legend>"
            "<span class=\"hint\">Plug the device into a computer by USB and long-press the screen: it types the "
            "archive link for the message on screen into the computer's launcher. It types only that fixed link "
            "and the message id, on a US keyboard layout.</span>"
            "<div class=\"row mt\"><label class=\"field\"><span>Computer:</span><select name=\"eam-usb-os\" class=\"grow\">")
            + "<option value=\"windows\"" + sel(usbOs != "mac" && usbOs != "linux" && usbOs != "off") + ">Windows (Win+R)</option>"
            + "<option value=\"mac\"" + sel(usbOs == "mac") + ">macOS (Cmd+Space)</option>"
            + "<option value=\"linux\"" + sel(usbOs == "linux") + ">Linux (Alt+F2)</option>"
            + "<option value=\"off\"" + sel(usbOs == "off") + ">Off</option>"
            + "</select></label><label class=\"field\"><span>When no message is shown:</span><select name=\"eam-usb-empty\" class=\"grow\">"
            + "<option value=\"archive\"" + sel(usbEmpty != "none") + ">Open the archive</option>"
            + "<option value=\"none\"" + sel(usbEmpty == "none") + ">Do nothing</option>"
            + "</select></label></div></fieldset>";
#else
        const String usbOpenHtml;
#endif
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
            [deviceName, deviceIp, wifiRssi, latitude, longitude, radius, radiusUnit, openskyClientId, openskySecret, dataSource, localUrl, localDetails, scanlineEnabled, fadeEnabled, infoTextEnabled, triangleEnabled, airportsEnabled, trailEnabled, altColorEnabled, highlightEnabled, autoDimEnabled, nightClockOn, brightness, tzOffset, tzAuto, radarUp, watchlist, ntfyTopic, milShow, milAlert, heliShow, spcShow, emgAlert, tonesOn, milVisual, emgVisual, visualNight, logbookOn, lbEnabled, lbName, lbLink, lbStanding, followTarget, followTrack, followUp, followDown, followLost, startSection, creditsLink, airportsMin, loc0Name, loc0Lat, loc0Lon, loc1Name, loc1Lat, loc1Lon, loc2Name, loc2Lat, loc2Lon, lookupOn, lookupAlert, lookupDist, mqttOn, mqttHost, mqttPort, mqttUser, mqttPass, mqttBase, mqttDisco, infoFieldsHtml
#ifdef FEATURE_CLOUD_FEED
             , cloudUrlCfg, cloudKeyCfg, enrolled, refused, deviceIdCfg
#endif
            ]
            (const String& var) -> String {
                if (var == "LATITUDE")       return latitude;
                if (var == "LONGITUDE")      return longitude;
                if (var == "RADIUS")         return radius;
                if (var == "RADIUS_UNIT_KM")  return radiusUnit == "km"  ? "selected" : "";
                if (var == "RADIUS_UNIT_MI")  return radiusUnit == "nmi" || radiusUnit == "km" ? "" : "selected";
                if (var == "RADIUS_UNIT_NMI") return radiusUnit == "nmi" ? "selected" : "";
                if (var == "OPENSKY_ID")     return openskyClientId;
                if (var == "OPENSKY_SECRET") return openskySecret;
#ifdef FEATURE_CLOUD_FEED
                if (var == "DEVICE_ID")      return deviceIdCfg;
                if (var == "ENROLLED")       return enrolled ? "1" : "0";
                if (var == "REFUSED")        return refused ? "1" : "0";
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
                // Exact matches only -- anything unrecognised (including unset) leaves the
                // placeholder selected rather than quietly implying a choice.
                if (var == "LD_CLOUD")  return localDetails == "cloud"  ? "selected" : "";
                if (var == "LD_OFF")    return localDetails == "off"    ? "selected" : "";
                if (var == "LD_UNSET")  return localDetails == "cloud" ? "" : "selected";
                if (var == "SCANLINE")       return scanlineEnabled == "true" ? "checked" : "";
                if (var == "FADE")           return fadeEnabled == "true" ? "checked" : "";
                if (var == "INFOTEXT")       return infoTextEnabled == "true" ? "checked" : "";
                if (var == "TRIANGLE")       return triangleEnabled == "true" ? "checked" : "";
                if (var == "AIRPORTS")       return airportsEnabled == "true" ? "checked" : "";
                if (var == "TRAIL")          return trailEnabled == "true" ? "checked" : "";
                if (var == "ALTCOLOR")       return altColorEnabled == "true" ? "checked" : "";
                if (var == "HIGHLIGHT")      return highlightEnabled == "true" ? "checked" : "";
                if (var == "AUTODIM")        return autoDimEnabled == "true" ? "checked" : "";
                if (var == "BRIGHTNESS")     return brightness;
                if (var == "TZ_OFFSET")      return tzOffset;
                if (var == "TZ_AUTO")        return tzAuto;
                if (var == "RADAR_UP")       return radarUp;
                if (var == "NIGHT_CLOCK")    return nightClockOn == "true" ? "checked" : "";
                if (var == "WATCHLIST")      return watchlist;
                if (var == "NTFY_TOPIC")     return ntfyTopic;
                if (var == "MIL_SHOW")       return milShow == "true" ? "checked" : "";
                if (var == "MIL_ALERT")      return milAlert == "true" ? "checked" : "";
                if (var == "HELI_SHOW")      return heliShow == "true" ? "checked" : "";
                if (var == "SPC_SHOW")       return spcShow == "true" ? "checked" : "";
                if (var == "EMG_ALERT")      return emgAlert == "true" ? "checked" : "";
                if (var == "TONES")          return tonesOn == "true" ? "checked" : "";
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
                if (var == "LB_ENABLED")     return lbEnabled == "true" ? "checked" : "";
                if (var == "FOLLOW")         return followTarget;
                if (var == "FOLLOW_TRACK")   return followTrack == "true" ? "checked" : "";
                if (var == "FOLLOW_UP")      return followUp    == "true" ? "checked" : "";
                if (var == "FOLLOW_DOWN")    return followDown  == "true" ? "checked" : "";
                if (var == "FOLLOW_LOST")    return followLost  == "true" ? "checked" : "";
                if (var == "LB_NAME")        return lbName;
                if (var == "LB_LINK")        return lbLink;
                if (var == "LB_STANDING")    return lbStanding;
                if (var == "START_SECTION")  return startSection;
                if (var == "CREDITS_LINK")   return creditsLink;
                if (var == "AIRPORTS_MIN_ALL")   return airportsMin == "all" ? "selected" : "";
                if (var == "AIRPORTS_MIN_MED")   return airportsMin == "med" ? "selected" : "";
                if (var == "AIRPORTS_MIN_LARGE") return airportsMin == "large" ? "selected" : "";
                if (var == "LOC0_NAME") return loc0Name;
                if (var == "LOC0_LAT")  return loc0Lat;
                if (var == "LOC0_LON")  return loc0Lon;
                if (var == "LOC1_NAME") return loc1Name;
                if (var == "LOC1_LAT")  return loc1Lat;
                if (var == "LOC1_LON")  return loc1Lon;
                if (var == "LOC2_NAME") return loc2Name;
                if (var == "LOC2_LAT")  return loc2Lat;
                if (var == "LOC2_LON")  return loc2Lon;
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
            [deviceName, deviceIp, wifiRssi, eamBaseUrl, latitude, longitude, abncpSource, openskyClientId, openskySecret, abncpWatch, ntfyTopic, alertNew, alertTempo, alertAbncp, alertSpace, eamPalette, eamRefresh, colonBlink, autoDimEnabled, brightness, eamScreens, usbOpenHtml]
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
                if (var == "USB_OPEN")       return usbOpenHtml;
                if (var == "FW_VERSION")     return String(FW_VERSION);
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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
                // Free function, so no capture list changes -- every edition's
                // processor answers this identically. See BuildIdentity.h.
                if (var == "BUILD_ID")       return BuildIdentity::Summary();
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

        // Coordinate fields go through CoordParse first, so NVS only ever holds a
        // canonical decimal string. The page's JS has normally done this already
        // and the value arrives clean; this is the path a JS-less browser or a
        // curl POST takes. An unreadable value is NOT stored -- keeping whatever
        // was there beats overwriting a working location with a typo -- and the
        // field name is collected so the response can name it.
        String badCoord;
        auto TrySaveCoord = [request, &prefs, &badCoord](const char* paramName, bool isLat) {
            const auto* param = request->getParam(paramName, true);
            if (param == nullptr)
                return false;
            const String raw = param->value();
            if (raw.length() == 0) { // clearing a field is a legitimate edit
                prefs.putString(paramName, raw);
                return true;
            }
            double v = 0.0;
            if (!CoordParse::Parse(raw, isLat, v)) {
                // A PASTED PAIR IN ONE BOX. The page JS splits "44.058, -121.315"
                // across both fields before it ever reaches here, so this is again
                // the JS-less browser / curl path -- where Parse() refuses the
                // comma outright, and the save failed while the customer was
                // looking at a value the page would have understood perfectly.
                //
                // Takes only the half that belongs to THIS box. Writing the
                // sibling from here would let one field overwrite another the
                // customer had just typed correctly; filling both boxes is the
                // page JS's job, and it already does it.
                double pairLat = 0.0, pairLon = 0.0;
                if (CoordParse::SplitPair(raw, pairLat, pairLon)) {
                    v = isLat ? pairLat : pairLon;
                    Serial.printf("[POST] %s: read a pasted pair, kept the %s half\n",
                                  paramName, isLat ? "latitude" : "longitude");
                } else {
                    if (badCoord.isEmpty()) badCoord = paramName;
                    Serial.printf("[POST] rejected %s: could not parse a coordinate\n", paramName);
                    return false;
                }
            }
            prefs.putString(paramName, CoordParse::Format(v));
            return true;
            };

        // CHECKBOX SEMANTICS. An unchecked box is simply absent from the body, so
        // "absent means false" is the only way a browser can ever turn one OFF.
        // That is correct for a browser -- and a trap for anything else: a
        // hand-rolled POST that sets one field silently clears EVERY toggle on
        // the page. That is not hypothetical; it happened on 2026-08-02, when a
        // bench script POSTing only lat/lon/radius turned off the airport
        // overlay, trails, fade, scanline and the logbook, and the resulting
        // frame-time change was misread as a rendering finding for most of a day.
        //
        // The form carries a hidden `cfg-form` marker. With it present the body
        // is a whole form and absent means false, exactly as before. Without it
        // the POST is partial, and a toggle nobody mentioned is left ALONE
        // rather than silently cleared.
        // THE FORM DECLARES ITS OWN VOCABULARY, and only names it declared may be
        // written false. See include/ConfigVocabulary.h for the stale-tab wipe this
        // prevents. An ABSENT list means nothing is written false -- deliberate: a
        // stale tab is exactly the client that will not send the field, so falling
        // back to the old whole-form behaviour would exempt the only case the fix
        // exists for. It is also the right failure mode if the JS ever breaks.
        const String cfgVocab = request->hasParam("cfg-toggles", true)
                              ? request->getParam("cfg-toggles", true)->value() : String();
        const bool haveVocab = !cfgVocab.isEmpty();
        int togglesWritten = 0, togglesLeftAlone = 0;
        auto SaveToggle = [&](const char* name) {
            if (request->hasParam(name, true)) {
                prefs.putString(name, "true"); ++togglesWritten; return;
            }
            if (haveVocab && cfgvocab::Declares(cfgVocab.c_str(), name)) {
                prefs.putString(name, "false"); ++togglesWritten; return;
            }
            ++togglesLeftAlone;   // this form never mentioned it: leave it alone
            };

        prefs.begin("config", false);

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
        // Saved location profiles (home / work / trip).
        TrySaveParam("loc0-name"); TrySaveCoord("loc0-lat", true); TrySaveCoord("loc0-lon", false);
        TrySaveParam("loc1-name"); TrySaveCoord("loc1-lat", true); TrySaveCoord("loc1-lon", false);
        TrySaveParam("loc2-name"); TrySaveCoord("loc2-lat", true); TrySaveCoord("loc2-lon", false);
        TrySaveParam("radius");
        TrySaveParam("radius-unit");
        TrySaveParam("brightness");
        // tz-offset is NOT a TrySaveParam, because "auto" has to be representable.
        // An empty field means auto, and auto is the ABSENCE of the key -- storing
        // "" would work for LocalOffset::Resolve (it treats "" and absent alike) but
        // would make isKey() true, so the page would render an empty box with no
        // placeholder and the state would no longer be readable. Absence is the
        // representation; keep it exact.
        if (const auto* tzParam = request->getParam("tz-offset", true)) {
            const String tzValue = tzParam->value();
            if (tzValue.length() == 0) {
                if (prefs.isKey("tz-offset")) prefs.remove("tz-offset");
            } else {
                prefs.putString("tz-offset", tzValue);
            }
        }
        TrySaveParam("watchlist");
        TrySaveParam("ntfy-topic");
        // Regenerate LAST, so it beats whatever was sitting in the box -- the
        // browser posts the old value alongside the tick, and a customer who
        // asked for a new topic must not get the old one back because of field
        // ordering. On the device with esp_random(), not in the browser, so it
        // is the same generator that produced the first one (14.1).
        if (request->hasParam("ntfy-regen", true)) {
            prefs.putString("ntfy-topic", ntfytopic::Generate());
            Serial.println("[ntfy] topic regenerated on request -- re-subscribe your phone");
        }
        TrySaveParam("follow");
        TrySaveParam("opensky-id");
        TrySaveParam("data-source");
        TrySaveParam("local-url");
        TrySaveParam("local-details");
#ifdef FEATURE_CLOUD_FEED
        TrySaveParam("cloud-url");
        // cloud key: same masked-value handling as the OpenSky secret (the GET
        // serves it as asterisks; only a genuinely new value overwrites).
        //
        // An EMPTY box is stored as empty on purpose: this key is only ever an
        // OVERRIDE. AircraftManager falls back to "cloud-key-fac" -- written once
        // during assembly and never exposed here -- so clearing this field restores
        // the key the device shipped with instead of destroying it. That inverts
        // the failure: the obvious customer reflex ("clear it and try again") is
        // now the repair, and no browser action can produce a device that needs a
        // key mailed to it.
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

        SaveToggle("scanline");
        SaveToggle("fade");
        SaveToggle("triangle");
        SaveToggle("airports");
        TrySaveParam("airports-min");
        SaveToggle("trail");
        SaveToggle("altcolor");
        SaveToggle("highlight");
        SaveToggle("autodim");
        SaveToggle("night-clock");
        SaveToggle("infotext");
        SaveToggle("mil-show");
        SaveToggle("mil-alert");
        SaveToggle("visual-night");
        SaveToggle("emg-alert");
        SaveToggle("tones");
        SaveToggle("heli-show");
        SaveToggle("spc-show");
        SaveToggle("logbook");
        SaveToggle("lb-enabled");
        SaveToggle("follow-track");
        SaveToggle("follow-up");
        SaveToggle("follow-down");
        SaveToggle("follow-lost");
        TrySaveParam("lb-name");
        SaveToggle("lookup");
        SaveToggle("lookup-alert");
        SaveToggle("mqtt");
        SaveToggle("mqtt-disco");

        // an unchecked checkbox isn't sent in the form body, so hasParam() is the
        // on/off signal for each individual info field
        for (size_t i = 0; i < AIRCRAFT_INFO_FIELD_COUNT; ++i)
            SaveToggle(AIRCRAFT_INFO_FIELDS[i].key);
#elif defined(FEATURE_EAM)
        // FEATURE_EAM: persist the EAM config fields.
        TrySaveParam("eam-base-url");
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
        TrySaveParam("abncp-source");
        TrySaveParam("opensky-id");
        TrySaveParam("abncp-watch");
        TrySaveParam("ntfy-topic");
        TrySaveParam("eam-palette");
        TrySaveParam("eam-refresh");
        TrySaveParam("brightness");
        TrySaveParam("eam-screens");
        TrySaveParam("eam-usb-os");      // FEATURE_USB_OPEN; absent from the form otherwise
        TrySaveParam("eam-usb-empty");

        // OpenSky secret: don't overwrite the stored value with the masked placeholder
        const auto* eamSecret = request->getParam("opensky-secret", true);
        if (eamSecret != nullptr) {
            const String& secret = eamSecret->value();
            if (!IsMaskedValue(secret))
                prefs.putString("opensky-secret", secret);
        }

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        SaveToggle("eam-alert-new");
        SaveToggle("eam-alert-tempo");
        SaveToggle("eam-alert-abncp");
        SaveToggle("eam-alert-space");
        SaveToggle("eam-colon-blink");
        SaveToggle("autodim");
#elif defined(FEATURE_SPACE)
        // FEATURE_SPACE: persist the Spacescope config fields.
        TrySaveParam("space-base-url");
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
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
            // Rebuilt wholesale from the checkboxes, so it carries the same
            // partial-POST hazard as the toggles: without the whole form this
            // would collapse the screen list to "clock". Guarded the same way.
            //
            // DECLARED HERE, IN THE ONLY BRANCH THAT USES IT. #286 replaced the
            // toggle mechanism this flag used to serve, converted the radar
            // path's two uses, and left this one referencing a variable that no
            // longer existed -- so every edition compiled except Space, and the
            // only build that compiles every edition is a release. It went
            // unseen from 2026-08-31 until the v12 tag, with a red leg on every
            // push run to main in between that nothing read.
            //
            // NOT haveVocab, which is the nearest variable in scope and would
            // have compiled: that answers "which toggles does this page know
            // about" and is appended by JS. This asks "whole form or fragment",
            // and every edition page posts the hidden cfg-form input including
            // a JS-less one, which sends no cfg-toggles at all.
            const bool wholeForm = request->hasParam("cfg-form", true);
            if (wholeForm)
                prefs.putString("space-screens", csv.isEmpty() ? String("clock") : csv);
        }

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        SaveToggle("sp-alert-launch");
        SaveToggle("sp-alert-aurora");
        SaveToggle("sp-alert-flare");
        SaveToggle("sp-alert-iss");
        SaveToggle("sp-alert-dsn");
        SaveToggle("sp-alert-neo");
        SaveToggle("sp-chime");
        SaveToggle("autodim");
#elif defined(FEATURE_SEISMIC)
        // FEATURE_SEISMIC: persist the Seismic edition config fields.
        TrySaveParam("se-base-url");
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
        TrySaveParam("se-min-mag");
        TrySaveParam("se-radius-km");
        TrySaveParam("se-big-mag");
        TrySaveParam("se-near-mag");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        SaveToggle("se-alert-big");
        SaveToggle("se-alert-near");
        SaveToggle("se-alert-tsnmi");
        SaveToggle("autodim");
#elif defined(FEATURE_BIRDING)
        // FEATURE_BIRDING: persist the Birding edition config fields.
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
        TrySaveParam("bd-radius-km");
        TrySaveParam("bd-back-days");
        TrySaveParam("bd-targets");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");
        SaveToggle("bd-alert-rare");
        SaveToggle("bd-alert-target");
        SaveToggle("autodim");

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
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
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
        SaveToggle("fi-v-tide");
        SaveToggle("fi-v-flow");
        SaveToggle("fi-v-temp");
        SaveToggle("fi-v-solunar");
        SaveToggle("fi-v-weather");
        SaveToggle("fi-v-moon");
        SaveToggle("fi-v-catch");
        SaveToggle("fi-v-clock");
        SaveToggle("fi-a-flow");
        SaveToggle("fi-a-temp");
        SaveToggle("fi-a-solunar");
        SaveToggle("fi-a-baro");
        SaveToggle("fi-a-tide");
        SaveToggle("fi-chime");
        SaveToggle("autodim");
#elif defined(FEATURE_CLAUDESCOPE)
        // FEATURE_CLAUDESCOPE: persist the Claudescope config fields. All feeds are keyless -- no
        // masked secret to guard (the OAuth token lives on the sidecar host, never here).
        TrySaveParam("cl-base-url");
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
        TrySaveParam("cl-tz-offset");
        TrySaveParam("cl-session-pct");
        TrySaveParam("cl-week-pct");
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        SaveToggle("cl-alert-sess");
        SaveToggle("cl-alert-week");
        SaveToggle("autodim");
#elif defined(FEATURE_SPEED)
        // FEATURE_SPEED: persist the Speedscope config fields. The camera endpoints are keyless (no secret).
        TrySaveParam("sc-host");
        TrySaveParam("sc-base-url");
        TrySaveParam("sc-limit");
        TrySaveParam("sc-alert-speed");
        TrySaveParam("sc-tz-offset");
        TrySaveCoord("latitude", true);
        TrySaveCoord("longitude", false);
        TrySaveParam("ntfy-topic");
        TrySaveParam("brightness");

        // checkboxes: absent in the body when unchecked, so hasParam() is the on/off signal
        SaveToggle("sc-v-last");
        SaveToggle("sc-v-live");
        SaveToggle("sc-v-list");
        SaveToggle("sc-v-stats");
        SaveToggle("sc-v-device");
        SaveToggle("sc-v-clock");
        SaveToggle("sc-a-speeder");
        SaveToggle("sc-a-record");
        SaveToggle("sc-a-offline");
        SaveToggle("autodim");
#endif
        // Read the STORED location back before closing, so the warning below
        // reflects what the device will actually run with -- not merely what this
        // form post contained. A partial save from a page where the location was
        // already set must not cry wolf.
        const String savedLat = prefs.getString("latitude", "");
        const String savedLon = prefs.getString("longitude", "");
        prefs.end();

        // No reboot: flag the change and let loop() re-read settings on the main
        // task. NVS is already committed by the putString() calls above, so the
        // reload will see the new values.
        configChanged = true;

        // A coordinate we could not read is reported rather than swallowed. The
        // page's JS normally catches this first and never submits; reaching here
        // means a JS-less client, so the reply has to carry the whole message
        // including an example of a good value. The word MISSING keeps the
        // existing red-and-bold styling in the status bar.
        if (!badCoord.isEmpty()) {
            const bool isLat = badCoord.endsWith("lat") || badCoord == "latitude";
            request->send(200, "text/html",
                          String("Saved the other settings, but the ") + badCoord +
                          " value was MISSING or unreadable, so it was left as it was. Examples: " +
                          (isLat ? "44.058173, or 44.058 N, or 44 3 29.4 N"
                                 : "-121.315308, or 121.315 W, or 121 18 55.1 W"));
            return;
        }

        // TELL THE USER WHEN THE DEVICE CANNOT WORK. Without a location there is no
        // tile to request, so the radar draws nothing -- and "Saved" on a screen
        // that then stays empty reads as a broken product rather than an unfinished
        // setup. This is the exact state a factory-fresh board is in: provisioning
        // writes the access key and nothing else, so location is the one field the
        // customer MUST supply, and it is the one most likely to be skipped.
        //
        // Editions that plot something near you only; Space/EAM/Claudescope/Speed
        // have no geography and must not be nagged about it.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
        if (savedLat.isEmpty() || savedLon.isEmpty()) {
            Serial.printf("[POST] saved, but location incomplete (lat=%s lon=%s)\n",
                          savedLat.isEmpty() ? "unset" : savedLat.c_str(),
                          savedLon.isEmpty() ? "unset" : savedLon.c_str());
            request->send(200, "text/html",
                          "Saved - but LOCATION IS MISSING, so nothing will appear on screen. "
                          "Enter your latitude and longitude above and save again. "
                          "(Tip: paste \"44.10, -121.30\" into the latitude box and it splits itself.)");
            return;
        }
#endif
        // SAY WHAT WAS ACTUALLY WRITTEN. Without this the trade is an invisible
        // wipe for an invisible no-op: the customer unticks a setting, it comes
        // back on, and they file it under "flaky" exactly as before -- just with a
        // smaller blast radius. The detection-profile complaint that made the wipe
        // a launch item applies to the fix too.
        if (!haveVocab) {
            request->send(200, "text/html",
                          "Saved - settings applied. This page did not say which "
                          "settings it knows about, so none were turned off. If a "
                          "toggle you unticked is still on, reload the page and save "
                          "again.");
            return;
        }
        if (togglesLeftAlone > 0) {
            char vbuf[192];
            snprintf(vbuf, sizeof(vbuf),
                     "Saved - %d setting%s applied. %d setting%s this page does not "
                     "know about %s left unchanged - reload to see %s.",
                     togglesWritten, togglesWritten == 1 ? "" : "s",
                     togglesLeftAlone, togglesLeftAlone == 1 ? "" : "s",
                     togglesLeftAlone == 1 ? "was" : "were",
                     togglesLeftAlone == 1 ? "it" : "them");
            request->send(200, "text/html", vbuf);
            return;
        }
        request->send(200, "text/html", "Saved - settings applied.");
        }
    );

    // Forget WiFi credentials and reboot into the WiFiManager setup portal. The
    // response is sent first; the restart is deferred a moment so it can flush.
#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
    /* -----------------------------------------------------------------------
     * ENROLLMENT KEY LANDING — the only path that may write "cloud-key-fac".
     *
     * A DEDICATED ROUTE RATHER THAN A FIELD ON /save, and the reason matters.
     * "cloud-key-fac" is the read-only factory identity: never rendered, never
     * writable from the settings form, which is precisely what makes "clear the
     * Access key box and save" the documented REPAIR for a mangled key rather
     * than an unrecoverable act. Adding it to the general form would put the one
     * value a customer cannot recover behind the one button they press when
     * confused. So enrollment lands here, where the only thing that can be
     * written is a well-formed key for THIS board.
     *
     * Three guards, none of them ceremony:
     *   - the CSRF header, same as every other POST;
     *   - the key must be exactly 64 lowercase hex (an HMAC-SHA256 digest), so
     *     no error page, JSON blob or truncated paste can land in the slot;
     *   - the id must be OUR id. A customer with two boards open in two tabs
     *     will otherwise paste board A's key into board B, and the failure is
     *     silent -- the key is valid, just not for this device, and the board
     *     simply never authenticates.
     *
     * The device cannot verify the key itself (that needs the fleet secret it
     * deliberately does not hold), so this is not authentication -- it is the
     * set of mistakes worth catching at the point of paste.
     * -------------------------------------------------------------------- */
    server.on("/enroll-key", HTTP_POST, [&](AsyncWebServerRequest* request) {
        if (RejectCrossOrigin(request)) return;

        const auto* keyParam = request->getParam("key", true);
        const auto* idParam  = request->getParam("id", true);
        String key = keyParam ? keyParam->value() : String();
        String id  = idParam ? idParam->value() : String();
        key.trim();
        id.trim();
        id.toLowerCase();

        bool wellFormed = key.length() == 64;
        for (size_t i = 0; wellFormed && i < key.length(); ++i) {
            const char c = key[i];
            wellFormed = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }
        if (!wellFormed) {
            Serial.println("[enroll] rejected: key is not 64 lowercase hex");
            request->send(400, "text/plain", "that does not look like a device key");
            return;
        }
        if (id.length() && id != DeviceIdentity::LeaderboardId()) {
            Serial.printf("[enroll] rejected: key is for %s, this board is %s\n",
                          id.c_str(), DeviceIdentity::LeaderboardId().c_str());
            request->send(409, "text/plain", "that key belongs to a different device");
            return;
        }

        Preferences prefs;
        prefs.begin("config", false);
        prefs.putString("cloud-key-fac", key);
        // Clear any stale override so the freshly enrolled identity is what the
        // device actually uses. Without this a board carrying an old pasted key
        // in "cloud-key" would enrol successfully and keep authenticating with
        // the wrong credential -- which is the exact confusion this whole
        // feature exists to end, reproduced one layer down.
        prefs.putString("cloud-key", "");
        prefs.end();
        // Same mechanism as /save: raise the flag and let loop() re-read on the
        // main task. No reboot -- AircraftManager re-reads the key when it
        // re-initialises, and rebooting from an async callback to pick up a
        // value NVS has already committed would be theatre with a failure mode.
        Serial.println("[enroll] device key stored");
        configChanged = true;
        request->send(200, "text/plain", "verified");
    });
#endif

    server.on("/reset-wifi", HTTP_POST, [&](AsyncWebServerRequest* request) {
        if (RejectCrossOrigin(request)) return; // CSRF guard (see RejectCrossOrigin)
        Serial.println("[POST] Clearing WiFi credentials and restarting...");
        request->send(200, "text/html", "WiFi cleared - restarting into setup mode. Reconnect to the device's setup network.");
        RequestReset(factoryreset::Tier::Wifi);
        }
    );

    /* -----------------------------------------------------------------------
     * FACTORY RESET -- a separate route, and a separate confirmation.
     *
     * NOT a parameter on /reset-wifi. The two tiers differ by everything the
     * customer cares about, and sharing an endpoint would mean one typo in a
     * query string is the difference between "forget my wifi" and "erase my
     * logbook". Separate paths cannot be confused for each other by accident.
     *
     * The typed word is checked HERE as well as in the page. The page's gate is
     * what makes the button hard to press by mistake; this one is what makes a
     * stray POST -- a replayed request, a curl from history, a bookmarklet --
     * not a wipe. Neither substitutes for the other.
     * --------------------------------------------------------------------- */
    server.on("/factory-reset", HTTP_POST, [&](AsyncWebServerRequest* request) {
        if (RejectCrossOrigin(request)) return; // CSRF guard (see RejectCrossOrigin)
        const AsyncWebParameter* confirm = request->getParam("confirm", true);
        if (confirm == nullptr || confirm->value() != "RESET") {
            Serial.println("[POST] factory reset REFUSED -- confirmation word absent or wrong");
            request->send(400, "text/plain",
                          "Confirmation required: type RESET to confirm. Nothing was erased.");
            return;
        }
        Serial.println("[POST] Factory reset confirmed -- erasing and restarting...");
        request->send(200, "text/html",
                      "Factory reset - restarting into setup mode. Reconnect to the device's setup network.");
        RequestReset(factoryreset::Tier::Factory);
        }
    );

#if !defined(FEATURE_EAM) && !defined(FEATURE_SPACE) && !defined(FEATURE_SEISMIC) && !defined(FEATURE_BIRDING) && !defined(FEATURE_FISHING) && !defined(FEATURE_CLAUDESCOPE) && !defined(FEATURE_SPEED)
    // Spotting-logbook export (radar edition). Serves the persisted lifelist straight
    // from NVS as JSON -- read-only, so it's safe from the async task alongside the
    // loop-task logbook writer (at most one debounce interval stale).
    //
    // CHUNKED, not one String. This is the data behind the collection view on the
    // page above, so it is now fetched on every visit rather than only when
    // somebody clicks "export" -- and at full caps the document is ~25 KB. Handing
    // beginResponse a String that size asks for one contiguous block on a device
    // whose largest free block sits around 36-44 KB with TLS also competing for
    // it. The stream keeps at most one serialized store (~5 KB) alive at a time,
    // so the cost stops scaling with the size of the logbook.
    //
    // The stream owns an open read-only Preferences handle, so it is kept in a
    // shared_ptr the lambda captures by value: ESPAsyncWebServer calls the filler
    // repeatedly and then drops it, which is exactly when the handle should close.
    // ---- /diag/fb : the glass, as bytes ----------------------------------
    //
    // Three display defects in one week were settled by pointing a phone camera
    // at a 1.28 inch circle. This is the instrument that should have existed:
    // the backbuffer, straight off the device, decoded by whoever asked.
    //
    // UNCONDITIONAL, deliberately. It exposes only what is already on the glass,
    // over the same unauthenticated LAN server that already serves the config
    // page and the logbook, and it is a support tool before it is a bench one --
    // "open <device>.local/diag/fb.html and send me that" is a sentence support
    // can say to anybody.
    //
    // IT SNAPSHOTS RATHER THAN STREAMING THE LIVE SPRITE, and the reason is the
    // header. The frame is drawn on the loop task while this runs on async_tcp,
    // so a read can straddle a redraw. The torn/not-torn answer therefore has to
    // be known BEFORE the first byte of the body leaves, because that is when
    // headers are written -- streaming the sprite directly would mean deciding
    // whether the frame tore only after it was already sent.
    //
    // So: sample the counter, copy 115 KB PSRAM-to-PSRAM, sample again. The copy
    // is the thing bracketed, not the socket write. No lock anywhere: holding the
    // draw loop for the length of a network write would trade a cosmetic fault
    // for a real one.
    server.on("/diag/fb", HTTP_GET, [](AsyncWebServerRequest* request) {
        LGFX_Sprite* fb = framebuf::Backbuffer();
        if (fb == nullptr || fb->getBuffer() == nullptr) {
            // Before setup() finished, or createSprite failed. 503 rather than a
            // crash, and rather than an empty 200 that would read as a black screen.
            request->send(503, "text/plain", "framebuffer not available");
            return;
        }
        const size_t total = (size_t)fb->bufferLength();
        const int    w     = fb->width();
        const int    h     = fb->height();

        uint8_t* snap = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
        if (snap == nullptr) {
            request->send(503, "text/plain", "out of PSRAM for a frame snapshot");
            return;
        }
        const uint32_t seqBefore = framebuf::Sequence();
        memcpy(snap, fb->getBuffer(), total);
        const uint32_t seqAfter = framebuf::Sequence();
        const bool torn = (seqBefore != seqAfter);

        // shared_ptr with a PSRAM-aware deleter: the chunk callback outlives this
        // scope, and the buffer must not leak if the client disconnects mid-send.
        std::shared_ptr<uint8_t> buf(snap, [](uint8_t* p) { heap_caps_free(p); });
        auto sent = std::make_shared<size_t>(0);

        AsyncWebServerResponse* r = request->beginChunkedResponse(
            "application/octet-stream",
            [buf, total, sent](uint8_t* out, size_t maxLen, size_t) -> size_t {
                const size_t left = total - *sent;
                const size_t n = left < maxLen ? left : maxLen;
                if (n > 0) {
                    memcpy(out, buf.get() + *sent, n);
                    *sent += n;
                }
                return n;
            });
        // Dimensions and pixel format travel with the bytes: a raw dump whose
        // geometry has to be known in advance is a dump that goes wrong silently
        // the day a SKU with a different panel asks for it.
        r->addHeader("X-Blipscope-Frame-Width", String(w));
        r->addHeader("X-Blipscope-Frame-Height", String(h));
        // BIG-ENDIAN, and this was wrong on the first try. LovyanGFX keeps sprite
        // pixels in the PANEL's byte order, not the CPU's, so a 16bpp sprite on an
        // SPI display is MSB-first. Decoded little-endian the whole radar came back
        // RED instead of green -- green 0x07E0 read backwards is 0xE007, which is a
        // strong red with a little blue, and looks exactly like a plausible
        // colour-scheme bug rather than a byte-order one.
        //
        // Caught by this endpoint on its first real use, which is the argument for
        // it: a camera would have shown green and agreed with the wrong header.
        r->addHeader("X-Blipscope-Frame-Format", "RGB565BE");
        r->addHeader("X-Blipscope-Frame-Torn", torn ? "true" : "false");
        r->addHeader("X-Blipscope-Frame-Seq", String(seqAfter));
        r->addHeader("Cache-Control", "no-store");
        request->send(r);
    });

    // ---- /diag/fb.html : the same thing, for a person --------------------
    server.on("/diag/fb.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", FB_VIEWER_HTML);
    });

    server.on("/logbook.json", HTTP_GET, [this](AsyncWebServerRequest* request) {
        // Ask the loop task to flush a dirty logbook. It cannot help THIS
        // response -- the stream below is already reading NVS on this task -- but
        // it makes the next read current. Dirty-only and rate-limited on the
        // logbook side; this end just raises the flag.
        logbookFlushRequested = true;
        auto stream = std::make_shared<Logbook::JsonStream>();
        AsyncWebServerResponse* r = request->beginChunkedResponse(
            "application/json",
            [stream](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
                return stream->Read(buffer, maxLen);
            });
        r->addHeader("Cache-Control", "no-store");
        // The collection view fetches this inline; the "download a copy" link
        // asks for ?download=1 and gets the attachment disposition instead.
        if (request->hasParam("download"))
            r->addHeader("Content-Disposition", "attachment; filename=\"logbook.json\"");
        request->send(r);
    });
#endif

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

    // PROVE THE BIND. Checked AFTER begin() on purpose -- before it, a free port only
    // tells you the portal has let go, not that we took it.
    listening = AnyListenerOnPort(listenPort);
    if (listening) {
        Serial.printf("[web] config server listening on :%u (http://%s.local)\n",
                      (unsigned)listenPort, DeviceIdentity::Name().c_str());
    } else {
        Serial.printf("[web] ERROR: config server did NOT bind :%u -- the config page is "
                      "UNREACHABLE this boot. Power-cycle to recover.\n",
                      (unsigned)listenPort);
    }
}

bool ConfigurationWebServer::ConsumeConfigChanged()
{
    if (!configChanged)
        return false;
    configChanged = false;
    return true;
}

bool ConfigurationWebServer::ConsumeLogbookFlushRequest()
{
    if (!logbookFlushRequested)
        return false;
    logbookFlushRequested = false;
    return true;
}

factoryreset::Tier ConfigurationWebServer::ConsumeResetTier()
{
    const factoryreset::Tier t = (factoryreset::Tier)resetTierRequested;
    resetTierRequested = 0;
    return t;
}

void ConfigurationWebServer::RequestReset(factoryreset::Tier tier)
{
    // Larger wins. Two requests cannot realistically overlap here, but reading
    // the flag as "at least this much" costs nothing and removes the one
    // ordering in which a factory reset is downgraded to a wifi reset.
    resetTierRequested = (uint8_t)factoryreset::Larger((factoryreset::Tier)resetTierRequested, tier);
}

bool ConfigurationWebServer::HasStoredKey(const char* key)
{
    Preferences prefs;
    prefs.begin("config", true);
    const bool present = prefs.isKey(key);
    prefs.end();
    return present;
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
