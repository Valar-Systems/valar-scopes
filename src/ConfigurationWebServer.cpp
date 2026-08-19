#include "ConfigurationWebServer.h"
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
    R"(fetch(this.action,{method:'POST',headers:{'X-Blipscope':'1'},body:new FormData(this)}).then(function(r){return r.text()}).then(function(t){)" \
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
    R"(window.addEventListener('message',function(e){var d=e.data;)" \
    R"(if(!d||d.type!=='blipscope-enroll'||!/^[0-9a-f]{64}$/.test(String(d.key||'')))return;)" \
    R"(var fd=new FormData();fd.append('key',d.key);fd.append('id',d.id||'');)" \
    R"(fetch('/enroll-key',{method:'POST',headers:{'X-Blipscope':'1'},body:fd}).then(function(r){)" \
    R"(if(r.ok){location.reload()}else{r.text().then(function(t){alert('Could not save the key: '+t)})}})});)" \
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
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>Configure Blipscope</title>
        <!-- inline SVG favicon (radar blip) so the tab is easy to spot; no extra flash asset / route needed.
             Colors use rgb() not #-hex on purpose: a "#" in a data URI must be percent-encoded, and any
             stray percent sign collides with this page's PLACEHOLDER template engine and shreds the whole
             form (write it as &#37; in visible text - and keep it out of comments too, like this one). -->
        <link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><rect width='16' height='16' rx='3' fill='rgb(17,24,39)'/><circle cx='8' cy='8' r='5.5' fill='none' stroke='rgb(34,197,94)' stroke-width='1'/><circle cx='8' cy='8' r='1.7' fill='rgb(34,197,94)'/></svg>">
        <style>:root{--ink:#22c55e;--line:#22c55e;--dim:#15803d;--btn:#22c55e}</style>
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
          .navb{text-align:left;background:none;border:1px solid transparent;color:var(--dim);
                padding:.45rem .6rem;border-radius:6px;cursor:pointer;font:inherit;line-height:1.3}
          .navb:hover{color:var(--ink)}
          .navb.on{color:var(--ink);border-color:var(--line);background:rgba(34,197,94,.10);font-weight:600}
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
            .side{flex-direction:row;overflow-x:auto;position:static;gap:.3rem;padding-bottom:.3rem}
            .navb{white-space:nowrap;flex:0 0 auto;padding:.4rem .7rem}
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
                    <button type="button" class="navb" data-go="collection">Collection</button>
                    <button type="button" class="navb" data-go="location">Location &amp; Radar</button>
                    <button type="button" class="navb" data-go="network">Network</button>
                    <button type="button" class="navb" data-go="about">About</button>
                </nav>
                <div class="content">

                <div class="sec" data-sec="collection">
                    <div class="stand">%LB_STANDING%</div>
                    <div id="col"><span class="hint">Loading your collection&hellip;</span></div>
                    <div class="hint mt">
                        Seeing an aircraft is your antenna's doing. Claiming it is yours &mdash; open a
                        contact's card on the device and one tap claims its type, operator, country and
                        route airports at once. Only claims score.
                        <a href="/logbook.json?download=1">Download a copy</a> of everything below.
                    </div>
                </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <div class="sec" data-sec="location">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Right-click your spot in Google Maps and copy the numbers, then paste into either box &mdash; both fill in. &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                        <option value="km" %RADIUS_UNIT_KM%>km</option>
                        <option value="mi" %RADIUS_UNIT_MI%>mi</option>
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
                        <span class="hint">
                            For self-hosting, or for pointing this board at a staging server.
                            Leave blank to use the built-in default.
                        </span>
                    </details>
                    <span class="hint">
                        Managed Blipscope feed &mdash; no account needed.
                        Your access key is set during assembly: leave it as it is, and if
                        it ever gets changed by mistake, clear the box and save to restore it.
                        Aircraft data from <a href="https://adsb.fi" target="_blank" rel="noopener">adsb.fi</a>
                        and <a href="https://adsb.lol" target="_blank" rel="noopener">adsb.lol</a>.
                        adsb.lol data &copy; adsb.lol contributors, licensed under
                        <a href="https://opendatacommons.org/licenses/odbl/1-0/" target="_blank" rel="noopener">ODbL 1.0</a>.
                        Military airframe data from the <a href="https://github.com/Mictronics/aircraft-database" target="_blank" rel="noopener">Mictronics aircraft database</a>,
                        licensed under <a href="https://opendatacommons.org/licenses/by/1-0/" target="_blank" rel="noopener">ODC-By 1.0</a>.
                        Curated military airframe data from <a href="https://github.com/sdr-enthusiasts/plane-alert-db" target="_blank" rel="noopener">plane-alert-db</a>,
                        licensed under <a href="https://opendatacommons.org/licenses/odbl/1-0/" target="_blank" rel="noopener">ODbL 1.0</a>.
                        Aircraft photographs from <a href="https://commons.wikimedia.org" target="_blank" rel="noopener">Wikimedia Commons</a>.
                        %CREDITS_LINK%
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
                    <label class="field">
                        <span>Aircraft details:</span>
                        <select name="local-details" id="local-details" class="grow">
                            <option value="" disabled %LD_UNSET%>Choose one &mdash; no default</option>
                            <option value="cloud" %LD_CLOUD%>Blipscope Cloud</option>
                            <option value="adsbdb" %LD_ADSBDB%>adsbdb direct</option>
                            <option value="off" %LD_OFF%>Off &mdash; receiver data only</option>
                        </select>
                    </label>
                    <span class="hint">
                        Your receiver supplies the positions either way; this only decides where the
                        detail card (type, airline, route, photo) comes from. There is deliberately
                        no default &mdash; it is your call, and it will never change on its own.
                        <b>Until you choose, details stay off.</b><br>
                        <b>Blipscope Cloud</b> &mdash; sends the tapped aircraft's ICAO hex, callsign
                        and position, plus your device model, firmware version and access key. Your
                        receiver's address is never sent, and neither is your own location &mdash;
                        but an aircraft you tapped is by definition near you, so
                        <i>treat this as coarse location rather than none</i>. This <i>replaces</i>
                        the adsbdb connection rather than adding to it, so details cost one internet
                        host instead of two, and it is the only option with photos.<br>
                        <b>adsbdb direct</b> &mdash; queries the public api.adsbdb.com, plus a second
                        host for thumbnails. We are never contacted; a third party is. No photo
                        library, no caching.<br>
                        <b>Off</b> &mdash; contacts nothing at all. The card shows only what your own
                        receiver reported.
                    </span>
                </div>

                </div><!-- /sec -->

                <div class="sec" data-sec="location">
                <fieldset>
                    <legend>Display</legend>
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
                        <label class="check"><input name="emg-alert" type="checkbox" %EMG_ALERT%><span>Alert on emergency squawk (ntfy)</span></label>
                        <label class="check"><input name="tones" type="checkbox" %TONES%><span>Alert tones (speaker models)</span></label>
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

                </div><!-- /sec -->

                <!-- The logbook toggle is the master switch for what the Collection
                     tab renders, and the leaderboard opt-in publishes that same
                     collection. Both sat under "Location & Radar", so turning the
                     logbook off emptied a tab you were not looking at. A section is
                     a SET of blocks, so moving them costs nothing but this boundary
                     -- no markup moves and the form stays one whole-form POST. -->
                <div class="sec" data-sec="collection">

                <details class="auto">
                    <summary>Spotting logbook <input name="logbook" type="checkbox" %LOGBOOK%></summary>
                    <span class="hint">
                        Keeps a running &ldquo;lifelist&rdquo; of every unique aircraft type, airline, country,
                        and route airport you've seen overhead (shown on the Stats screen), with first-seen dates,
                        per-type counts, and lifetime records. Anything you haven't claimed yet shows a gold
                        &ldquo;NEW&rdquo; on the radar &mdash; <b>tap it to claim it</b>. Seeing an aircraft is
                        your antenna's doing; claiming it is yours, and only claims score.
                        It looks up each contact's type/airline, so it adds a little network traffic.
                        Download a copy any time: <a href="/logbook.json?download=1">logbook.json</a>.
                    </span>
                </details>

                <details class="auto">
                    <summary>Spotting leaderboard <input name="lb-enabled" type="checkbox" %LB_ENABLED%></summary>
                    <label class="field">
                        <span>Spotter name:</span>
                        <input name="lb-name" value='%LB_NAME%' maxlength="24" placeholder="e.g. Redmond Radar" class="grow">
                    </label>
                    <span class="hint mt">
                        Opt in to the public %LB_LINK% &mdash;
                        compete on unique types, airlines, and countries seen overhead. <b>Counts only leave your device</b>
                        (plus your type list, for rarity scoring): never your location, never which flights you saw. Off by default;
                        requires the Blipscope Cloud feed. First device to claim a name owns it.
                    </span>
                </details>

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
                    <span class="hint mt">
                        Publishes a retained &ldquo;&lt;base&gt;/summary&rdquo; (count, nearest aircraft, overhead &amp; military flags)
                        to your broker every few seconds. With auto-discovery on, Home Assistant creates the sensors automatically.
                    </span>
                </details>

                </div><!-- /sec -->

                <!-- Every section that contains a control needs the savebar. Moving
                     the logbook/leaderboard blocks to Collection without adding it
                     here left that tab with toggles and no way to apply them. -->
                <div class="sec" data-sec="collection location network">
                <div class="savebar">
                    <input type="submit" value="Save" class="btn">
                    <span id="result"></span>
                </div>
                </div><!-- /sec -->
            </form>

                <div class="sec" data-sec="about">
                    <div class="grid2">
                        <div class="kv"><span>Device</span><b>%DEVICE_NAME%.local</b></div>
                        <div class="kv"><span>Address</span><b>%DEVICE_IP%</b></div>
                        <div class="kv"><span>WiFi signal</span><b>%WIFI_RSSI% dBm</b></div>
                        <div class="kv"><span>Firmware</span><b>v%FW_VERSION%</b></div>
                    </div>
                    <div class="foot mt">
                        <a href="https://github.com/Valar-Systems/valar-scopes/wiki" target="_blank" rel="noopener">Help &amp; documentation</a>
                        %CREDITS_LINK%
                    </div>
                    <div class="hint mt">
                        Reset WiFi makes the device forget this network and restart into its setup
                        portal. Your location, settings and spotting logbook are kept.
                    </div>
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
                    <div class="hint">
                        <b style="color:#ff4d4d">Factory reset</b> erases everything this device
                        knows about you &mdash; your spotting logbook, location and radius,
                        leaderboard opt-in and display name, and the WiFi network. It restarts
                        into setup mode. This cannot be undone.
                    </div>
                    <div class="mt"><button type="button" id="factoryopen" class="btn-danger">Factory reset&hellip;</button></div>
                    <div id="factorypanel" class="mt" style="display:none;border:1px solid #ff4d4d;border-radius:6px;padding:12px">
                        <div class="hint">
                            <b>Save your logbook first.</b>
                            <a href="/logbook.json?download=1">Download a copy</a> &mdash; once this
                            device is erased there is no other copy of it.
                        </div>
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

            // dim the per-field list when the master Aircraft Info toggle is off.
            // purely cosmetic -- the inputs stay enabled so their state still saves.
            const infoMaster = document.querySelector('input[name="infotext"]');
            const infoFields = document.getElementById('info-fields');
            function syncInfoFields() {
                infoFields.style.opacity = infoMaster.checked ? '1' : '0.4';
            }
            infoMaster.addEventListener('change', syncInfoFields);
            syncInfoFields();

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
            const section = function (title, items, keyName, claimedN, truncAt) {
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
                h += '<div>';
                for (const it of sorted) {
                    const when = it.claimed ? ('claimed ' + (it.claimedOn || 'date unknown'))
                        : ('seen ' + (it.first || 'date unknown') + ' - not claimed yet');
                    const n = it.count ? (' x' + it.count) : '';
                    // MARK A TRUNCATED NAME. Names are cut to MAX_OP_LEN /
                    // MAX_CN_LEN at STORE time, so the full text is already gone
                    // by the time it reaches here -- "CSC DELAWARE TRUST CO TR" is
                    // genuinely all the device has. An ellipsis is the honest
                    // option left: the reader can see the name is clipped instead
                    // of being shown a wrong one as if it were complete.
                    //
                    // Length-equals-cap is a heuristic, so a name that happens to
                    // be exactly cap characters gets a spurious ellipsis. That is
                    // a strictly smaller error than the current one, and it is
                    // cosmetic in a way the current one is not.
                    //
                    // The widening this used to defer -- "the stored name IS the
                    // map key, so changing the cut re-spells every entry and
                    // orphans the claims filed under the old spelling" -- was done
                    // in v5, 24 -> 40, with the lazy re-keying migration that makes
                    // it safe (adoptTruncatedOperator in Logbook.cpp). The ellipsis
                    // stays, because 40 still clips some registered owners; it just
                    // fires far less often now.
                    let name = String(it[keyName]);
                    if (truncAt && name.length >= truncAt) name += '…';
                    h += chip(name + n, it.claimed, when);
                }
                return h + '</div>';
            };
            const loadCollection = function () {
                if (colLoaded) return;
                colLoaded = true;
                fetch('/logbook.json').then(function (r) { return r.json(); }).then(function (d) {
                    const c = d.counts || {}, k = d.claimed || {};
                    let h = '<div class="hint">Seeing an aircraft is your antenna. Claiming it is you &mdash; ' +
                        'open a contact\'s card on the device to claim its type, operator, country and airports at once.</div>';
                    h += section('Types', d.types, 'code', k.types || 0, 0);
                    // "Operators", not "Airlines". adsbdb returns the REGISTERED
                    // OWNER, and outside airline traffic that is a person or a
                    // single-airframe LLC -- a real board's list reads ANDREW
                    // KLEMISH, BORKOSKI BRIAN, 84 ALPHA KILO LLC. Calling that
                    // "Airlines" was the least honest label on the page. The JSON
                    // key stays `airlines`: it is the wire field the leaderboard
                    // submit sends, so renaming it is a Worker change, not a copy
                    // change.
                    h += section('Operators', d.airlines, 'name', k.airlines || 0, 40); // MAX_OP_LEN
                    h += section('Countries', d.countries, 'name', k.countries || 0, 32); // MAX_CN_LEN
                    h += section('Airports', d.airports, 'code', k.airports || 0, 0);
                    const rec = d.records || {};
                    const bits = [];
                    if (rec.high) bits.push('Highest ' + esc(rec.high.callsign) + ' ' + rec.high.value + ' ' + esc(rec.high.unit));
                    if (rec.fast) bits.push('Fastest ' + esc(rec.fast.callsign) + ' ' + rec.fast.value + ' ' + esc(rec.fast.unit));
                    if (rec.near) bits.push('Closest ' + esc(rec.near.callsign) + ' ' + rec.near.value + ' ' + esc(rec.near.unit));
                    if (bits.length) h += '<div style="margin:.9rem 0 .2rem"><b>Records</b></div><div class="hint">' + bits.join('<br>') + '</div>';
                    h += '<div class="hint" style="margin-top:.9rem">' + (d.contacts || 0) + ' contacts seen in total.</div>';
                    if (!d.types || !d.types.length) {
                        h = '<span class="hint">Nothing logged yet. Turn on the spotting logbook above, ' +
                            'and give the device a while to see some traffic.</span>';
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
            function showSection(name) {
                for (const el of secs) {
                    el.classList.toggle('on', (el.dataset.sec || '').split(' ').indexOf(name) >= 0);
                }
                for (const b of navs) b.classList.toggle('on', b.dataset.go === name);
                if (name === 'collection') loadCollection();
                try { history.replaceState(null, '', '#' + name); } catch (e) { /* file:// etc. */ }
            }
            for (const b of navs) {
                b.addEventListener('click', function () { showSection(b.dataset.go); });
            }
            // The landing section is decided ON THE DEVICE and arrives in the markup
            // (body[data-start]), not computed here: a first-run customer with no
            // location set must land on Location & Radar, and doing that in JS would
            // paint the Collection first and then jump -- which over a slow AP link is
            // the moment somebody decides the page is broken. A #hash still wins, so
            // links into a section keep working.
            const startFromHash = (location.hash || '').replace('#', '');
            const valid = ['collection', 'location', 'network', 'about'];
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
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional. Used for propagation day/night and the command-post bearing/distance. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional, but unlocks the location-aware screens: next visible ISS pass, local aurora odds, and the solar night auto-dim. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                <span title="Build env and compiled features">%BUILD_ID%</span>
            </div>

            <form id="cfg" action="/save" method="POST">
                <input type="hidden" name="cfg-form" value="1">

                <div class="row">
                    <label class="field">
                        <span>Latitude:</span>
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Your location centres the quake radar, the "near me" feed and alerts, and the solar night auto-dim. Without it you still get the worldwide list and stats. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Your location centres the sightings radar, the nearby feeds, and alerts. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Location drives on-device solunar/sun/moon, the keyless weather feed, and the night auto-dim. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                        <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                    </label>
                    <label class="field">
                        <span>Longitude:</span>
                        <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                    </label>
                </div>
                <span class="hint">Optional. Location drives only the night auto-dim and the local clock; usage numbers work without it. Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>

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
                            <input name="latitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LATITUDE%' class="grow">
                        </label>
                        <label class="field">
                            <span>Longitude:</span>
                            <input name="longitude" type="text" inputmode="decimal" autocapitalize="off" autocorrect="off" spellcheck="false" value='%LONGITUDE%' class="grow">
                        </label>
                    </div>
                    <span class="hint mt">Location is optional &mdash; it only drives the night auto-dim (sunset/sunrise at your spot). Tip: paste into either box and both fill in &mdash; &ldquo;44.058, -121.315&rdquo;, &ldquo;44.058&deg;N 121.315&deg;W&rdquo; and &ldquo;44&deg; 3&rsquo; 29&Prime; N&rdquo; all work. No minus key? Write &ldquo;121.315 W&rdquo;.</span>
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
        const String radiusUnit = HtmlEscape(prefs.isKey("radius-unit") ? prefs.getString("radius-unit", "km") : "km");
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
        const String emgAlert = HtmlEscape(prefs.isKey("emg-alert") ? prefs.getString("emg-alert", "false") : "false");
        const String tonesOn = HtmlEscape(prefs.isKey("tones") ? prefs.getString("tones", "true") : "true");
        // visual alerts: defaults mirror AircraftManager::Initialise (emergency = ring, military = off)
        const String milVisual = HtmlEscape(prefs.isKey("mil-visual") ? prefs.getString("mil-visual", "off") : "off");
        const String emgVisual = HtmlEscape(prefs.isKey("emg-visual") ? prefs.getString("emg-visual", "ring") : "ring");
        const String visualNight = HtmlEscape(prefs.isKey("visual-night") ? prefs.getString("visual-night", "false") : "false");
        const String logbookOn = HtmlEscape(prefs.isKey("logbook") ? prefs.getString("logbook", "false") : "false");
        const String lbEnabled = HtmlEscape(prefs.isKey("lb-enabled") ? prefs.getString("lb-enabled", "false") : "false");
        const String lbName = HtmlEscape(prefs.getString("lb-name", ""));
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
            [deviceName, deviceIp, wifiRssi, latitude, longitude, radius, radiusUnit, openskyClientId, openskySecret, dataSource, localUrl, localDetails, scanlineEnabled, fadeEnabled, infoTextEnabled, triangleEnabled, airportsEnabled, trailEnabled, altColorEnabled, highlightEnabled, autoDimEnabled, nightClockOn, brightness, tzOffset, radarUp, watchlist, ntfyTopic, milShow, milAlert, heliShow, spcShow, emgAlert, tonesOn, milVisual, emgVisual, visualNight, logbookOn, lbEnabled, lbName, lbLink, lbStanding, startSection, creditsLink, airportsMin, loc0Name, loc0Lat, loc0Lon, loc1Name, loc1Lat, loc1Lon, loc2Name, loc2Lat, loc2Lon, lookupOn, lookupAlert, lookupDist, mqttOn, mqttHost, mqttPort, mqttUser, mqttPass, mqttBase, mqttDisco, infoFieldsHtml
#ifdef FEATURE_CLOUD_FEED
             , cloudUrlCfg, cloudKeyCfg, enrolled, refused, deviceIdCfg
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
                if (var == "LD_ADSBDB") return localDetails == "adsbdb" ? "selected" : "";
                if (var == "LD_OFF")    return localDetails == "off"    ? "selected" : "";
                if (var == "LD_UNSET")  return (localDetails == "cloud" || localDetails == "adsbdb"
                                                || localDetails == "off") ? "" : "selected";
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
                if (badCoord.isEmpty()) badCoord = paramName;
                Serial.printf("[POST] rejected %s: could not parse a coordinate\n", paramName);
                return false;
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
        const bool wholeForm = request->hasParam("cfg-form", true);
        auto SaveToggle = [request, &prefs, wholeForm](const char* name) {
            if (request->hasParam(name, true))
                prefs.putString(name, "true");
            else if (wholeForm)
                prefs.putString(name, "false");
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
        TrySaveParam("tz-offset");
        TrySaveParam("watchlist");
        TrySaveParam("ntfy-topic");
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
    server.on("/logbook.json", HTTP_GET, [](AsyncWebServerRequest* request) {
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