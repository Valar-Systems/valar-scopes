/* ===========================================================================
 * THE ENROL PAGE — the one place in the system where a human is provably present.
 *
 * WHY THIS IS A HOSTED PAGE AND NOT THE DEVICE'S OWN CONFIG PAGE. The obvious
 * design puts the Turnstile widget straight onto the device's settings page,
 * where the customer already is. It cannot work:
 *
 *   - The device serves that page over mDNS at http://<device-name>.local, and
 *     the device NAME is user-settable, so the hostname is unknowable in advance
 *     and different on every board. A widget's domain allow-list would have to
 *     accept anything.
 *   - Cloudflare's own guidance is explicit: "Do not allow local hostnames in
 *     production." A hostname check that must accept `radar-desk.local` or a
 *     bare LAN IP is not validation, it is the shape of validation.
 *   - The gate exists to protect a relationship rather than a quota, so a
 *     nominal check is worse than an honest absence of one.
 *
 * So the solve happens HERE, on an origin we own and can pin, and the key is
 * handed back to the device page by postMessage — which crosses HTTPS -> HTTP
 * because no resource is loaded, only a message.
 *
 * THE POPUP IS A CONVENIENCE, NOT A BOUNDARY. This page works perfectly well
 * opened directly on a phone: the customer reads their device id off the config
 * page, solves here, and types the key back in. That is the documented fallback
 * for a LAN that cannot reach Cloudflare, and it is not a weaker path — the gate
 * is the SOLVE, and moving the browser does not remove it. Nothing in this file
 * should ever be changed on the theory that being opened by the device page is
 * what makes a request trustworthy.
 * ======================================================================== */

import { SUPPORT_EMAIL } from "./support";

/**
 * The enrol page.
 *
 * `sitekey` is injected rather than hardcoded so a staging deployment can carry
 * its own widget; an empty sitekey renders the page in its "cannot verify"
 * state rather than a broken widget, which is the same state a blocked network
 * produces and is therefore already handled.
 */
export function enrollHtml(sitekey: string): string {
  return `<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Verify your device - Blipscope</title>
<style>
:root{color-scheme:dark;--bg:#0f1115;--fg:#e8e6e1;--dim:#9aa0a6;--ok:#4ade80;--bad:#ff6b6b;--line:#2a2f3a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.55 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:24px}
.card{width:100%;max-width:34rem;border:1px solid var(--line);border-radius:10px;padding:26px 24px;background:#141821}
h1{margin:0 0 4px;font-size:1.28rem;letter-spacing:.01em}
p.sub{margin:0 0 20px;color:var(--dim);font-size:.93rem}
.id{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;background:#0b0e13;border:1px solid var(--line);
    border-radius:6px;padding:7px 10px;display:inline-block;letter-spacing:.06em}
.k{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.82rem;word-break:break-all;background:#0b0e13;
   border:1px solid var(--line);border-radius:6px;padding:12px;margin:12px 0;user-select:all}
.ok{color:var(--ok)} .bad{color:var(--bad)}
button{font:inherit;background:#1f6feb;color:#fff;border:0;border-radius:6px;padding:10px 16px;cursor:pointer}
button:disabled{opacity:.5;cursor:not-allowed}
.note{font-size:.87rem;color:var(--dim);margin-top:16px;border-top:1px solid var(--line);padding-top:14px}
</style>
<script src="https://challenges.cloudflare.com/turnstile/v0/api.js" async defer></script>
</head><body>
<div class="card">
  <h1>Verify your device</h1>
  <p class="sub">One check, once per board. It activates Blipscope Cloud on a board bought from Valar Systems.</p>

  <p>Device: <span class="id" id="devid">-</span></p>

  <div id="gate">
    <div class="cf-turnstile" data-sitekey="${sitekey}" data-action="turnstile-spin-v1" data-callback="onSolve"></div>
    <p class="sub" id="hint" style="margin-top:14px">Tick the box to continue.</p>
  </div>

  <div id="out" hidden></div>

  <div class="note" id="fallback" hidden>
    <b>Can't load the check?</b> Some networks block it. Open this same page on a phone
    (mobile data works), then type the key into your device's settings page.
  </div>
</div>
<script>
(function(){
  var p = new URLSearchParams(location.search);
  var id = (p.get('id')||'').trim().toLowerCase();
  var out = document.getElementById('out'), gate = document.getElementById('gate');
  document.getElementById('devid').textContent = id || 'missing';

  function show(html, cls){ out.hidden=false; out.className=cls||''; out.innerHTML=html; }

  // A REFUSAL A CUSTOMER CAN ACT ON IN ONE TAP (docs/device-activation.md). The id is the
  // one validated below (hex only), so it is safe in markup; the address is the server's
  // own constant (src/support.ts), so the JSON a refusal carries and this link agree.
  var SUPPORT = '${SUPPORT_EMAIL}';
  function supportBlock(subject, body){
    var href = 'mailto:'+SUPPORT+'?subject='+encodeURIComponent(subject)+'&body='+encodeURIComponent(body);
    return '<p class="sub">Device id: <span class="id">'+id+'</span></p>'+
           '<p class="sub"><a href="'+href+'">Email '+SUPPORT+'</a> - the message already carries this id.</p>';
  }

  if(!/^[0-9a-f]{8,32}$/.test(id)){
    gate.hidden = true;
    show('<p class="bad">No device id in this link. Open the settings page on your '+
         'Blipscope and use the Verify button, or add <code>?id=...</code> from the id shown there.</p>');
    return;
  }

  // THE WIDGET NEVER LOADING IS A FIRST-CLASS STATE, not an error to swallow.
  // A blocked or offline network is exactly the customer this fallback exists
  // for, and leaving them staring at an empty box would be the failure.
  setTimeout(function(){
    if(!window.turnstile){
      gate.hidden = true;
      document.getElementById('fallback').hidden = false;
      show('<p class="bad">The verification check could not load on this network.</p>');
    }
  }, 6000);

  // ONE SUBMIT PER PAGE LOAD. Turnstile refreshes an expiring token on its own
  // (data-refresh-expired defaults to "auto") and re-invokes this callback to
  // hand over the new one -- so an UNGUARDED onSolve is not a handler, it is a
  // ~5-minute timer. A tab left open on this page re-enrolled one board 1,988
  // times over 20 days, and three days running the fleet-wide counter sat at
  // 594/595/597 -- two tabs, beating steadily, around the clock.
  //
  // It was invisible because re-enrolment is idempotent BY DESIGN: the key is a
  // pure function of the id, so every repeat returned the identical key and the
  // device kept working perfectly. Nothing anywhere reported a fault.
  //
  // The guard is here rather than on the widget: data-refresh-expired="never"
  // would leave a stale page holding an expired token and hand the customer an
  // "unverified" error, which trades a silent loop for a visible failure in the
  // one flow that must not break. The server is deliberately NOT changed to
  // suppress repeats -- enrolment volume is the abuse signal (see enroll.ts),
  // and a server that quietly de-duplicates is a server that cannot see this
  // class of bug at all. It observed this one correctly; the client was wrong.
  var submitted = false;
  window.onSolve = function(token){
    if(submitted) return;
    submitted = true;
    document.getElementById('hint').textContent = 'Checking...';
    fetch('/blipscope/enroll', {
      method:'POST', headers:{'content-type':'application/json'},
      body: JSON.stringify({ id:id, token:token })
    }).then(function(r){ return r.json().then(function(j){ return {s:r.status, j:j}; }); })
      .then(function(res){
        gate.hidden = true;
        if(res.s !== 200){
          var err = res.j && res.j.error;
          if(err === 'not_listed'){
            show('<p class="bad"><b>This board is not registered as a Valar unit.</b></p>'+
                 '<p class="sub">Only boards bought from Valar Systems can activate Blipscope Cloud. '+
                 'If you bought this one from us, email us its device id and we will sort it out.</p>'+
                 supportBlock('Blipscope activation: '+id, 'My Blipscope will not activate. Device id: '+id)+
                 '<p class="sub">It can still show aircraft from your own receiver (dump1090 or readsb) '+
                 'or your OpenSky account: choose either on its settings page.</p>');
            return;
          }
          if(err === 'already_activated'){
            show('<p class="bad"><b>This board has already been activated.</b></p>'+
                 '<p class="sub">If it is yours and it was fully erased, email us its device id and we will '+
                 're-open it for you.</p>'+
                 supportBlock('Blipscope re-activation: '+id, 'Please re-open my Blipscope. Device id: '+id));
            return;
          }
          var msg = err === 'revoked'
            ? 'This device has been revoked. Please get in touch.'
            : err === 'unverified'
              ? 'The check did not pass. Reload and try again.'
              : 'Verification is unavailable right now. Please try again later.';
          show('<p class="bad">'+msg+'</p>');
          return;
        }
        // HAND IT BACK TO THE DEVICE PAGE if this window was opened by one.
        // targetOrigin '*' is deliberate and safe here: the opener is an
        // http://<name>.local origin we cannot know in advance, and the payload
        // is a key that is ALREADY scoped to a device id the customer owns and
        // that is published on the leaderboard. Nothing secret is broadened by
        // this that the customer does not already hold.
        var handed = false;
        try {
          if(window.opener && !window.opener.closed){
            window.opener.postMessage({ type:'blipscope-enroll', id:id, key:res.j.key }, '*');
            handed = true;
          }
        } catch(e){}
        show('<p class="ok"><b>Verified.</b></p>'+
             (handed
               ? '<p class="sub">Sent back to your device. You can close this window.</p>'
               : '<p class="sub">Copy this key into the <b>Access key</b> box on your device\\'s settings page:</p>'+
                 '<div class="k">'+res.j.key+'</div>'));
      })
      .catch(function(){
        gate.hidden = true;
        document.getElementById('fallback').hidden = false;
        show('<p class="bad">Could not reach the verification service.</p>');
      });
  };
})();
</script>
</body></html>`;
}
