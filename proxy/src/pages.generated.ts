// GENERATED FILE -- DO NOT EDIT.
// Source: proxy/pages/*.html; regenerate with `node scripts/embed-pages.mjs`.
// CI fails if this is out of date (scripts/embed-pages.mjs --check), so editing
// it here instead of the .html would be overwritten and is never what you want.

export const indexHtml = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Valar Scopes</title>
<meta name="description" content="Desk instruments built on one open firmware. Live surfaces for each edition.">
<style>
  @font-face{font-family:'Inter';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/inter.woff2) format('woff2')}
  @font-face{font-family:'JetBrains Mono';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/mono.woff2) format('woff2')}
  @font-face{font-family:'Space Grotesk';font-style:normal;font-weight:500 700;
    font-display:swap;src:url(/fonts/grotesk.woff2) format('woff2')}
  :root{
    --bg:#101215; --panel:#171a1e; --panel-2:#1c2025; --edge:#2a2f36;
    --ink:#eceef1; --ink-2:#a8aeb7; --ink-3:#6f7680;
    --live:#3ddc84; --amber:#e0a33a; --gold:#d9b45a; --nardo:#8f949b;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  @media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
  body{background:var(--bg);color:var(--ink);font-family:'Inter',system-ui,sans-serif;
       font-size:16px;line-height:1.6;-webkit-font-smoothing:antialiased}
  .wrap{max-width:760px;margin:0 auto;padding:0 22px}
  a{color:var(--live);text-decoration:none;border-bottom:1px solid rgba(61,220,132,.3)}
  a:hover{border-bottom-color:var(--live)}
  a:focus-visible{outline:2px solid var(--live);outline-offset:3px;border-radius:2px}

  /* ---------- masthead ---------- */
  header{padding:52px 0 26px;text-align:center}
  .eyebrow{font-family:'JetBrains Mono',monospace;font-size:11px;letter-spacing:.22em;
           text-transform:uppercase;color:var(--ink-3);margin-bottom:14px}
  h1{font-family:'Space Grotesk',sans-serif;font-size:36px;font-weight:700;
     letter-spacing:.01em;line-height:1.08}
  .rule{font-size:15px;color:var(--ink-2);max-width:48ch;margin:14px auto 0}

  /* ---------- edition cards ---------- */
  .editions{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin:34px 0 30px}
  @media (max-width:620px){.editions{grid-template-columns:1fr}}
  .ed{background:var(--panel);border:1px solid var(--edge);border-radius:8px;
      padding:20px 19px;display:flex;flex-direction:column}
  .ed .tag{font-family:'JetBrains Mono',monospace;font-size:10.5px;letter-spacing:.16em;
           text-transform:uppercase;color:var(--live);margin-bottom:9px}
  .ed h2{font-family:'Space Grotesk',sans-serif;font-size:20px;font-weight:700;
         margin-bottom:7px;letter-spacing:.01em}
  .ed p{font-size:14px;color:var(--ink-2);line-height:1.5;flex:1}
  .ed .links{display:flex;flex-wrap:wrap;gap:7px;margin-top:16px}
  .ed .links a{background:var(--panel-2);border:1px solid var(--edge);border-radius:5px;
               padding:6px 11px;font-size:13px;color:var(--ink-2);font-weight:500}
  .ed .links a:hover{color:var(--ink);border-color:var(--nardo)}
  .ed .links a.primary{background:var(--live);border-color:var(--live);color:var(--bg);font-weight:600}
  .ed .links a.primary:hover{background:#4ee895;border-color:#4ee895}

  /* ---------- more / footer ---------- */
  .more{background:var(--panel);border:1px solid var(--edge);border-radius:8px;
        padding:17px 19px;margin-bottom:30px}
  .more h3{font-family:'Space Grotesk',sans-serif;font-size:15px;font-weight:600;margin-bottom:6px}
  .more p{font-size:13.5px;color:var(--ink-2);line-height:1.5}
  footer{border-top:1px solid var(--edge);padding:20px 0 44px;text-align:center;
         font-size:13px;color:var(--ink-3)}
  footer a{color:var(--ink-3);border-bottom-color:rgba(111,118,128,.35)}
  footer a:hover{color:var(--ink-2);border-bottom-color:var(--ink-3)}
  footer .sep{margin:0 9px;color:var(--edge)}
</style>
</head>
<body>
<div class="wrap">

  <header>
    <div class="eyebrow">Valar Systems</div>
    <h1>Valar Scopes</h1>
    <p class="rule">Small desk instruments built from one open firmware. Each edition
       watches something different and shows it on a round screen.</p>
  </header>

  <!--
    ONLY editions with a live surface ON THIS DOMAIN belong here. Blipscope is
    served by this Worker; Missileer is reverse-proxied to valar-eam-feed (see
    src/missileer.ts). Every other edition is firmware with no web surface, so it
    is covered by the "More editions" block below rather than given a card whose
    links would 404. When an edition gains a surface here, give it a card and add
    its paths to KNOWN_ROUTES in src/metrics.ts.
  -->
  <div class="editions">

    <section class="ed">
      <div class="tag">Aviation</div>
      <h2>Blipscope</h2>
      <p>A desk flight radar. Live aircraft around you, with type, airline, route
         and a photo when you tap one.</p>
      <div class="links">
        <a class="primary" href="/blipscope/support">Support</a>
        <a href="/blipscope/leaderboard">Leaderboard</a>
      </div>
    </section>

    <section class="ed">
      <div class="tag">EAM Monitor</div>
      <h2>Missileer</h2>
      <p>An HFGCS Emergency Action Message monitor — live traffic, codewords and
         the standing alert tempo.</p>
      <!--
        No Support link yet, deliberately. /missileer/* is proxied to
        valar-eam-feed, so /missileer/support has to ship in THAT repo; linking it
        from here first would point at a 404 the origin generates. Add the link in
        the same change that publishes the page.
      -->
      <div class="links">
        <a href="/missileer/leaderboard">Leaderboard</a>
        <a href="https://github.com/Valar-Systems/valar-scopes/wiki/Missileer">Guide</a>
      </div>
    </section>

  </div>

  <div class="more">
    <h3>More editions</h3>
    <p>Orbitscope (space), Quakescope (earthquakes), Quillscope (birding),
       Reelscope (fishing), Claudescope (Claude usage) and Speedscope (speed radar)
       run on the same hardware. They have no web surface here — each one is
       documented on the
       <a href="https://github.com/Valar-Systems/valar-scopes/wiki">Valar Scopes wiki</a>.</p>
  </div>

  <footer>
    <!-- LAUNCH: restore this link. The product page is DRAFT and 404s until
         the store opens, and a dead "Order a kit" on the support page is
         worse than no link at all. See RELEASING.md launch checklist. -->
    <a href="mailto:support@valarsystems.com">Contact us</a>
    <span class="sep">·</span>
    <a href="https://github.com/Valar-Systems/valar-scopes">Source</a>
    <span class="sep">·</span>
    <a href="/credits">Photo credits</a>
  </footer>

</div>
</body>
</html>
`;

export const leaderboardHtml = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blipscope Spotting Leaderboard</title>
<meta name="description" content="Who's claimed the most aircraft. Points come from noticing, not from antennas.">
<style>
  @font-face{font-family:'Inter';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/inter.woff2) format('woff2')}
  @font-face{font-family:'JetBrains Mono';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/mono.woff2) format('woff2')}
  @font-face{font-family:'Space Grotesk';font-style:normal;font-weight:500 700;
    font-display:swap;src:url(/fonts/grotesk.woff2) format('woff2')}
  :root{
    --bg:#101215; --panel:#171a1e; --panel-2:#1c2025; --edge:#2a2f36;
    --ink:#eceef1; --ink-2:#a8aeb7; --ink-3:#6f7680;
    --live:#3ddc84; --amber:#e0a33a; --gold:#d9b45a; --nardo:#8f949b;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  @media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
  body{background:var(--bg);color:var(--ink);font-family:'Inter',system-ui,sans-serif;
       font-size:16px;line-height:1.6;-webkit-font-smoothing:antialiased}
  .wrap{max-width:760px;margin:0 auto;padding:0 22px}
  a{color:var(--live);text-decoration:none;border-bottom:1px solid rgba(61,220,132,.3)}
  a:hover{border-bottom-color:var(--live)}
  a:focus-visible{outline:2px solid var(--live);outline-offset:3px;border-radius:2px}

  /* ---------- masthead ---------- */
  header{padding:52px 0 26px;text-align:center}
  .eyebrow{font-family:'JetBrains Mono',monospace;font-size:11px;letter-spacing:.22em;
           text-transform:uppercase;color:var(--ink-3);margin-bottom:14px}
  h1{font-family:'Space Grotesk',sans-serif;font-size:36px;font-weight:700;
     letter-spacing:.01em;line-height:1.08}
  .rule{font-size:15px;color:var(--ink-2);max-width:46ch;margin:14px auto 0}
  .rule b{color:var(--ink);font-weight:600}

  /* ---------- how it works ---------- */
  .how{display:grid;grid-template-columns:repeat(3,1fr);gap:1px;background:var(--edge);
       border:1px solid var(--edge);border-radius:6px;overflow:hidden;margin:30px 0 34px}
  .how div{background:var(--panel);padding:16px 15px}
  .how .n{font-family:'JetBrains Mono',monospace;font-size:11px;color:var(--live);
          letter-spacing:.1em;margin-bottom:7px}
  .how p{font-size:13.5px;color:var(--ink-2);line-height:1.45}
  .how b{color:var(--ink);font-weight:600}
  @media (max-width:600px){.how{grid-template-columns:1fr}}

  /* ---------- season bar ---------- */
  .bar{display:flex;align-items:center;justify-content:space-between;gap:14px;
       padding-bottom:12px;border-bottom:1px solid var(--edge);margin-bottom:4px;flex-wrap:wrap}
  .tabs{display:flex;gap:4px}
  .tab{background:none;border:none;cursor:pointer;font-family:'Inter',sans-serif;
       font-size:13.5px;font-weight:500;color:var(--ink-3);padding:7px 13px;border-radius:4px}
  .tab:hover{color:var(--ink-2);background:var(--panel)}
  .tab[aria-selected="true"]{color:var(--bg);background:var(--nardo);font-weight:600}
  .tab:focus-visible{outline:2px solid var(--live);outline-offset:2px}
  .season{font-family:'JetBrains Mono',monospace;font-size:11.5px;color:var(--ink-3);
          letter-spacing:.08em}

  /* ---------- board ---------- */
  .board{margin:22px 0 0}
  .row{display:grid;grid-template-columns:38px 1fr auto;gap:14px;align-items:center;
       padding:13px 14px;border-bottom:1px solid var(--edge)}
  .row:first-child{border-top:1px solid var(--edge)}
  .rank{font-family:'JetBrains Mono',monospace;font-size:14px;color:var(--ink-3);
        text-align:right;font-weight:500}
  .row.top .rank{color:var(--gold)}
  .who{display:flex;align-items:center;gap:8px;min-width:0}
  .name{font-weight:600;font-size:15.5px;white-space:nowrap;overflow:hidden;
        text-overflow:ellipsis;color:var(--ink);border-bottom:none}
  a.name:hover{color:var(--live)}
  a.name:focus-visible{outline:2px solid var(--live);outline-offset:2px;border-radius:2px}
  .rare{font-family:'JetBrains Mono',monospace;font-size:10.5px;color:var(--ink-3);
        border:1px solid var(--edge);border-radius:3px;padding:1px 6px;flex:none}
  .stats{font-family:'JetBrains Mono',monospace;font-size:11.5px;color:var(--ink-3);
         margin-top:2px;letter-spacing:.02em}
  .pts{font-family:'JetBrains Mono',monospace;font-size:17px;font-weight:700;
       text-align:right;white-space:nowrap}
  .row.top .pts{color:var(--gold)}
  @media (max-width:520px){
    .row{grid-template-columns:30px 1fr auto;gap:10px;padding:12px 8px}
    .name{font-size:14.5px}.pts{font-size:15px}
  }

  /* ---------- category leaders ---------- */
  .cats{display:grid;grid-template-columns:repeat(2,1fr);gap:1px;background:var(--edge);
        border:1px solid var(--edge);border-radius:6px;overflow:hidden;margin:34px 0 0}
  .cat{background:var(--panel);padding:15px 16px}
  .cat .k{font-family:'JetBrains Mono',monospace;font-size:10.5px;letter-spacing:.14em;
          text-transform:uppercase;color:var(--ink-3);margin-bottom:6px}
  .cat .v{font-weight:600;font-size:15px}
  .cat .c{font-family:'JetBrains Mono',monospace;font-size:12px;color:var(--live);margin-top:2px}
  @media (max-width:520px){.cats{grid-template-columns:1fr}}

  h2{font-family:'Space Grotesk',sans-serif;font-size:12px;font-weight:700;
     letter-spacing:.19em;text-transform:uppercase;color:var(--ink-3);
     margin:44px 0 16px}

  /* ---------- empty / loading ---------- */
  .msg{border:1px dashed var(--edge);border-radius:6px;padding:30px 24px;text-align:center;
       color:var(--ink-2);font-size:15px}
  .msg b{display:block;color:var(--ink);font-weight:600;font-size:16px;margin-bottom:6px}
  .skeleton{height:48px;background:linear-gradient(90deg,var(--panel) 25%,var(--panel-2) 50%,var(--panel) 75%);
            background-size:200% 100%;animation:sk 1.4s linear infinite;
            border-bottom:1px solid var(--edge)}
  @keyframes sk{to{background-position:-200% 0}}

  footer{border-top:1px solid var(--edge);margin-top:52px;padding:26px 0 60px;
         color:var(--ink-3);font-size:13.5px;line-height:1.7}
</style>
</head>
<body>

<header>
  <div class="wrap">
    <div class="eyebrow">Blipscope</div>
    <h1>Spotting Leaderboard</h1>
    <p class="rule">Points come from <b>noticing</b>, not from antennas. Tap an aircraft on your scope and you claim what it’s carrying.</p>
  </div>
</header>

<main class="wrap">

  <div class="how">
    <div>
      <div class="n">01</div>
      <p>An aircraft you’ve never claimed shows a <b>gold ring</b> on your radar.</p>
    </div>
    <div>
      <div class="n">02</div>
      <p>Tap it. The card opens and you <b>claim its type, airline, country and airports</b> — all in one.</p>
    </div>
    <div>
      <div class="n">03</div>
      <p>Rarer catches score higher. <b>Untapped aircraft earn nothing</b>, however busy your sky.</p>
    </div>
  </div>

  <div class="bar">
    <div class="tabs" role="tablist" aria-label="Leaderboard range">
      <button class="tab" role="tab" aria-selected="true" data-scope="lifetime">Lifetime</button>
      <button class="tab" role="tab" aria-selected="false" data-scope="season">This season</button>
    </div>
    <div class="season" id="season">—</div>
  </div>

  <div class="board" id="board" aria-live="polite">
    <div class="skeleton"></div><div class="skeleton"></div><div class="skeleton"></div>
  </div>

  <h2 id="catsHead">Category leaders</h2>
  <div class="cats" id="cats"></div>

</main>

<footer>
  <div class="wrap">
    Opt in from your Blipscope’s config page. Counts only — no location or flight data leaves your device.<br>
    <a href="https://valarsystems.com/blipscope">About Blipscope</a> · Valar Systems, Bend, Oregon
  </div>
</footer>

<script>
const board = document.getElementById('board');
const cats  = document.getElementById('cats');
const seasonEl = document.getElementById('season');
let scope = 'lifetime';
let data = null;

const esc = s => String(s ?? '').replace(/[&<>"']/g, c =>
  ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));

const nf = n => Number(n ?? 0).toLocaleString();

function renderRows(rows){
  if (!rows || !rows.length){
    board.innerHTML = \`<div class="msg"><b>No claims yet this season</b>
      Nobody has tapped an aircraft yet. The first person to claim something takes the top spot.</div>\`;
    return;
  }
  // The rarity percentile only means anything once the fleet is big enough for a
  // fraction to be a fraction. On a bench board of one, every claimed type is
  // held by 100% of devices, so every row would read "top 100%" -- which reads as
  // a bug AND inverts the badge (a higher number means COMMONER). Below this the
  // badge shows the type code alone, which is true at any fleet size.
  const MIN_ROWS_FOR_PCT = 10;
  const showPct = rows.length >= MIN_ROWS_FOR_PCT;

  board.innerHTML = rows.map((r,i) => {
    const c = r.claimed ?? r.counts ?? {};
    const s = r.seen ?? {};
    const pair = (k,label) => c[k] == null ? '' :
      (s[k] != null ? \`\${nf(c[k])} of \${nf(s[k])} \${label}\` : \`\${nf(c[k])} \${label}\`);
    const bits = [pair('types','types'), pair('airlines','airlines'),
                  pair('countries','countries'), pair('airports','airports')].filter(Boolean);
    const rank = r.rank ?? (i+1);
    const nameHtml = r.id
      ? \`<a class="name" href="/blipscope/leaderboard/\${encodeURIComponent(r.id)}">\${esc(r.name || 'Unnamed scope')}</a>\`
      : \`<span class="name">\${esc(r.name || 'Unnamed scope')}</span>\`;
    const rare = r.rarestType
      ? \`<span class="rare" title="Rarest claim">\${esc(r.rarestType)}\${showPct && r.rarestPct != null ? \` · top \${r.rarestPct}%\` : ''}</span>\`
      : '';
    return \`<div class="row\${i===0?' top':''}">
      <div class="rank">\${rank}</div>
      <div>
        <div class="who">\${nameHtml}\${rare}</div>
        \${bits.length ? \`<div class="stats">\${bits.join('  ·  ')}</div>\` : ''}
      </div>
      <div class="pts">\${nf(r.points)}</div>
    </div>\`;
  }).join('');
}

function renderCats(leaders){
  const map = [
    ['types','Most types'],
    ['airlines','Most airlines'],
    ['countries','Most countries'],
    ['airports','Most airports']
  ];
  const cells = map.map(([k,label]) => {
    const l = leaders?.[k];
    if (!l || !l.name) return '';
    return \`<div class="cat">
      <div class="k">\${label}</div>
      <div class="v">\${esc(l.name)}</div>
      \${l.count != null ? \`<div class="c">\${nf(l.count)}</div>\` : ''}
    </div>\`;
  }).filter(Boolean).join('');
  const heading = document.getElementById('catsHead');
  if (cells){
    cats.innerHTML = cells;
    cats.style.display = '';
    heading.style.display = '';
  } else {
    cats.innerHTML = '';
    cats.style.display = 'none';
    heading.style.display = 'none';
  }
}

function paint(){
  if (!data) return;
  const rows = (scope === 'season' ? data.season?.rows : data.lifetime?.rows) ?? data.rows;
  const leaders = (scope === 'season' ? data.season?.leaders : data.lifetime?.leaders) ?? data.leaders;
  renderRows(rows);
  renderCats(leaders);
  seasonEl.textContent = data.season?.id ? \`Season \${data.season.id}\` : '';
}

document.querySelectorAll('.tab').forEach(t => {
  t.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(x => x.setAttribute('aria-selected','false'));
    t.setAttribute('aria-selected','true');
    scope = t.dataset.scope;
    paint();
  });
});

fetch('/blipscope/leaderboard.json')
  .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
  .then(j => { data = j; paint(); })
  .catch(() => {
    board.innerHTML = \`<div class="msg"><b>Couldn’t load the board</b>
      Try again in a moment.</div>\`;
    cats.innerHTML = '';
  });
</script>
</body>
</html>
`;

export const notfoundHtml = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Page not found — Valar Scopes</title>
<meta name="description" content="That page does not exist. Here is where to go instead.">
<meta name="robots" content="noindex">
<style>
  @font-face{font-family:'Inter';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/inter.woff2) format('woff2')}
  @font-face{font-family:'JetBrains Mono';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/mono.woff2) format('woff2')}
  @font-face{font-family:'Space Grotesk';font-style:normal;font-weight:500 700;
    font-display:swap;src:url(/fonts/grotesk.woff2) format('woff2')}
  :root{
    --bg:#101215; --panel:#171a1e; --edge:#2a2f36;
    --ink:#eceef1; --ink-2:#a8aeb7; --ink-3:#6f7680;
    --live:#3ddc84; --amber:#e0a33a;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  @media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
  body{background:var(--bg);color:var(--ink);font-family:'Inter',system-ui,sans-serif;
       font-size:16px;line-height:1.6;-webkit-font-smoothing:antialiased;
       min-height:100vh;display:flex;align-items:center;justify-content:center}
  .wrap{max-width:560px;padding:48px 22px;text-align:center}
  a{color:var(--live);text-decoration:none;border-bottom:1px solid rgba(61,220,132,.3)}
  a:hover{border-bottom-color:var(--live)}
  a:focus-visible{outline:2px solid var(--live);outline-offset:3px;border-radius:2px}
  code{font-family:'JetBrains Mono',monospace;font-size:.88em;background:var(--panel);
       border:1px solid var(--edge);border-radius:4px;padding:1px 5px;color:var(--ink-2);
       word-break:break-all}
  .eyebrow{font-family:'JetBrains Mono',monospace;font-size:11px;letter-spacing:.22em;
           text-transform:uppercase;color:var(--amber);margin-bottom:16px}
  h1{font-family:'Space Grotesk',sans-serif;font-size:30px;font-weight:700;
     letter-spacing:.01em;line-height:1.12;text-wrap:balance}
  p{font-size:15px;color:var(--ink-2);margin-top:14px}
  .paths{display:flex;flex-wrap:wrap;gap:9px;justify-content:center;margin-top:28px}
  .paths a{background:var(--panel);border:1px solid var(--edge);border-radius:6px;
           padding:10px 16px;font-size:14px;color:var(--ink);font-weight:500}
  .paths a:hover{border-color:var(--live)}
  .foot{margin-top:34px;font-size:13px;color:var(--ink-3)}
  .foot a{color:var(--ink-3);border-bottom-color:rgba(111,118,128,.35)}
  .foot a:hover{color:var(--ink-2)}
</style>
</head>
<body>
<div class="wrap">
  <div class="eyebrow">404 — Not found</div>
  <h1>That page doesn&rsquo;t exist</h1>
  <p>The address may have a typo in it, or it may have moved. Nothing is wrong
     with your device &mdash; this is just a page that isn&rsquo;t here.</p>
  <div class="paths">
    <a href="/blipscope/support">Blipscope support</a>
    <a href="/">All editions</a>
  </div>
  <p class="foot">
    Still stuck? Email
    <a href="mailto:support@valarsystems.com">support@valarsystems.com</a>
    &mdash; a real person reads it.
  </p>
</div>
</body>
</html>
`;

export const supportHtml = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blipscope Support</title>
<meta name="description" content="Setup, troubleshooting and help for Blipscope — the desk flight radar.">
<style>
  @font-face{font-family:'Inter';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/inter.woff2) format('woff2')}
  @font-face{font-family:'JetBrains Mono';font-style:normal;font-weight:400 700;
    font-display:swap;src:url(/fonts/mono.woff2) format('woff2')}
  @font-face{font-family:'Space Grotesk';font-style:normal;font-weight:500 700;
    font-display:swap;src:url(/fonts/grotesk.woff2) format('woff2')}
  :root{
    --bg:#101215; --panel:#171a1e; --panel-2:#1c2025; --edge:#2a2f36;
    --ink:#eceef1; --ink-2:#a8aeb7; --ink-3:#6f7680;
    --live:#3ddc84; --amber:#e0a33a; --gold:#d9b45a; --nardo:#8f949b; --alert:#ff4030;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  @media (prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
  body{background:var(--bg);color:var(--ink);font-family:'Inter',system-ui,sans-serif;
       font-size:16px;line-height:1.6;-webkit-font-smoothing:antialiased}
  .wrap{max-width:760px;margin:0 auto;padding:0 22px}
  a{color:var(--live);text-decoration:none;border-bottom:1px solid rgba(61,220,132,.3)}
  a:hover{border-bottom-color:var(--live)}
  a:focus-visible{outline:2px solid var(--live);outline-offset:3px;border-radius:2px}
  code{font-family:'JetBrains Mono',monospace;font-size:.88em;background:var(--panel-2);
       border:1px solid var(--edge);border-radius:4px;padding:1px 5px;color:var(--ink)}

  /* ---------- masthead ---------- */
  header{padding:52px 0 24px;text-align:center}
  .eyebrow{font-family:'JetBrains Mono',monospace;font-size:11px;letter-spacing:.22em;
           text-transform:uppercase;color:var(--ink-3);margin-bottom:14px}
  .eyebrow a{color:var(--ink-3);border:none}
  .eyebrow a:hover{color:var(--ink-2)}
  h1{font-family:'Space Grotesk',sans-serif;font-size:36px;font-weight:700;
     letter-spacing:.01em;line-height:1.08}
  .rule{font-size:15px;color:var(--ink-2);max-width:48ch;margin:14px auto 0}

  /* ---------- jump nav ---------- */
  nav.jump{display:flex;flex-wrap:wrap;gap:7px;justify-content:center;
           margin:26px 0 8px;padding-bottom:26px;border-bottom:1px solid var(--edge)}
  nav.jump a{background:var(--panel);border:1px solid var(--edge);border-radius:5px;
             padding:6px 12px;font-size:13px;color:var(--ink-2);font-weight:500}
  nav.jump a:hover{color:var(--ink);border-color:var(--nardo)}

  /* ---------- sections ---------- */
  section.blk{padding:32px 0 4px;border-bottom:1px solid var(--edge)}
  section.blk:last-of-type{border-bottom:none}
  h2{font-family:'Space Grotesk',sans-serif;font-size:22px;font-weight:700;
     margin-bottom:14px;letter-spacing:.01em;scroll-margin-top:18px}
  h3{font-family:'Inter',sans-serif;font-size:15.5px;font-weight:600;
     margin:20px 0 5px;color:var(--ink)}
  p{font-size:14.5px;color:var(--ink-2);margin-bottom:11px}
  p b{color:var(--ink);font-weight:600}
  ol,ul{margin:0 0 13px 20px}
  li{font-size:14.5px;color:var(--ink-2);margin-bottom:6px}
  li b{color:var(--ink);font-weight:600}

  /* A problem and its fix, kept visually paired so the page scans. */
  .fix{background:var(--panel);border:1px solid var(--edge);border-left:2px solid var(--amber);
       border-radius:0 6px 6px 0;padding:14px 16px;margin:0 0 11px}
  .fix .q{font-weight:600;color:var(--ink);font-size:14.5px;margin-bottom:5px}
  .fix p{font-size:14px;margin-bottom:0}
  .fix p + p{margin-top:8px}
  .fix ol,.fix ul{margin:8px 0 0 20px}
  .fix li{font-size:14px;margin-bottom:5px}

  /* The staleness ladder. Three rows because the device has three states, and
     the whole point is that they mean different things. */
  .ladder{display:grid;grid-template-columns:auto 1fr;gap:1px;background:var(--edge);
          border:1px solid var(--edge);border-radius:6px;overflow:hidden;margin:14px 0}
  .ladder .tag,.ladder .what{background:var(--panel);padding:11px 14px}
  .ladder .tag{font-family:'JetBrains Mono',monospace;font-size:12.5px;font-weight:700;
               white-space:nowrap;letter-spacing:.04em}
  .ladder .what{font-size:13.5px;color:var(--ink-2);line-height:1.45}
  .ladder .what b{color:var(--ink);font-weight:600}
  .t-amber{color:var(--amber)} .t-red{color:var(--alert)} .t-grey{color:var(--nardo)}
  @media (max-width:560px){
    .ladder{grid-template-columns:1fr}
    .ladder .tag{padding-bottom:0;background:var(--panel)}
  }

  .note{background:var(--panel-2);border:1px solid var(--edge);border-radius:6px;
        padding:14px 16px;margin:14px 0}
  .note p{font-size:13.5px;margin-bottom:0}
  .note p + p{margin-top:8px}

  /* Visually demoted: this section is not for the person who bought a finished
     device, and it should not compete with the sections that are. */
  section.builder{opacity:.92}
  section.builder h2{font-size:19px}
  section.builder .lede{font-family:'JetBrains Mono',monospace;font-size:11px;
       letter-spacing:.16em;text-transform:uppercase;color:var(--ink-3);margin-bottom:9px}

  footer{border-top:1px solid var(--edge);padding:22px 0 46px;text-align:center;
         font-size:13px;color:var(--ink-3);margin-top:34px}
  footer a{color:var(--ink-3);border-bottom-color:rgba(111,118,128,.35)}
  footer a:hover{color:var(--ink-2);border-bottom-color:var(--ink-3)}
  footer .sep{margin:0 9px;color:var(--edge)}
</style>
</head>
<body>
<div class="wrap">

  <header>
    <div class="eyebrow"><a href="/">Valar Scopes</a> — Aviation</div>
    <h1>Blipscope Support</h1>
    <p class="rule">Setting it up, and what to try when something looks wrong.</p>
  </header>

  <nav class="jump">
    <a href="#setup">Setup</a>
    <a href="#problems">Troubleshooting</a>
    <a href="#data">Flight data</a>
    <a href="#alerts">Phone alerts</a>
    <a href="#updates">Updates</a>
    <a href="#privacy">Privacy</a>
    <a href="#help">Still stuck</a>
  </nav>

  <section class="blk">
    <h2 id="setup">Setting it up</h2>
    <p>Your Blipscope arrives assembled and already running. It needs two things
       from you: your Wi-Fi, and where you are.</p>

    <h3>First boot</h3>
    <ol>
      <li>Plug it in over USB-C. It broadcasts its own Wi-Fi hotspot, named
          something like <code>Blipscope-A1B2C3</code> — <b>the exact name is shown
          on the screen</b>.</li>
      <li>Connect to that hotspot from a phone or laptop. A configuration page
          should open automatically; if it doesn't, open a browser.</li>
      <li>Enter your Wi-Fi name and password, then save. The device restarts and
          joins your network.</li>
    </ol>

    <h3>The config page</h3>
    <p>Once it is on your network, the config page is reachable from any device on
       the same network at the address shown on screen —
       <code>http://&lt;device-name&gt;.local</code>, for example
       <code>http://blipscope-a1b2c3.local</code>. It stays available whenever the
       device is on Wi-Fi, so you can change settings any time.</p>

    <h3>Setting your location</h3>
    <p>This is the one setting that matters most: it is the centre of your radar,
       and until it is right the scope is watching the wrong patch of sky.</p>
    <ol>
      <li>Go to <a href="https://www.gps-coordinates.org">gps-coordinates.org</a>
          and search your address.</li>
      <li>Copy the <b>latitude</b> and <b>longitude</b> it gives you.</li>
      <li>Paste them into <b>Location</b> on the config page and save.</li>
    </ol>
    <p>Then set <b>Radar radius</b> — how far out the scope reaches. It is capped at
       about 222 km / 138 mi to stay inside the data rate limits.</p>
  </section>

  <section class="blk">
    <h2 id="problems">Troubleshooting</h2>

    <div class="fix">
      <div class="q">The setup hotspot never appears</div>
      <p>Give it a moment — it is not instant. If it still hasn't shown up after
         about 30 seconds, leave your phone's Wi-Fi settings screen and go back
         into it to force a refresh. Devices cache the network list more
         aggressively than you would expect.</p>
    </div>

    <div class="fix">
      <div class="q">I can't reach <code>http://&lt;device-name&gt;.local</code></div>
      <p>That address relies on mDNS, which some networks and some Android
         versions don't resolve. Check the screen — the device shows its address
         once it has joined — and try the numeric IP from your router's client
         list instead. A guest network or client isolation will also block it:
         your phone and the device must be on the <b>same</b> network segment.</p>
    </div>

    <div class="fix">
      <div class="q">The radar looks empty, or the aircraft aren't moving</div>
      <p><b>Check your location first.</b> A blank or mistyped
         latitude/longitude points the scope at empty sky, and that looks exactly
         like a broken feed. See <a href="#setup">Setting your location</a>.</p>
      <p>If the location is right, <b>read the banner at the top of the radar</b>.
         Blipscope tells you when the picture has stopped being live rather than
         letting you believe a stale one:</p>
      <div class="ladder">
        <div class="tag t-amber">STALE DATA</div>
        <div class="what">A few updates missed. Routine on any source — it clears
          itself and needs nothing from you.</div>
        <div class="tag t-amber">STALE 3m</div>
        <div class="what">Over a minute old, now counting. <b>The sweep stops</b>
          — no rotating beam means what you are looking at is not current.</div>
        <div class="tag t-red">NO DATA — 12m</div>
        <div class="what">Nothing has arrived for ten minutes. Contacts turn
          <b class="t-grey">dim grey</b> and freeze where they last were. They are
          no longer a picture of anything.</div>
      </div>
      <p>All three clear themselves the moment data arrives — the scope snaps back
         to colour and the sweep restarts. If it stays red, the device has lost
         its internet connection; power-cycle it, and check your network is up.</p>
      <p>And sometimes the sky really is empty. With no banner and a running sweep,
         you are seeing everything there is — which at night, or away from the
         airways, can genuinely be nothing.</p>
    </div>

    <div class="fix">
      <div class="q">I need to move it to a different Wi-Fi network</div>
      <p>Two ways, depending on whether you can still reach the screen's menus.</p>
      <p><b>From the Stats screen.</b> Swipe to Stats and double-tap
         <code>[ Reset Wi-Fi ]</code>. The first tap arms it and the second
         confirms, so a stray touch can't wipe your network.</p>
      <p><b>At boot, if the screen isn't cooperating.</b> Unplug it, plug it back
         in, then <b>TOUCH &amp; HOLD for three seconds</b> — the screen prompts you
         during a short window as it starts. Keep holding until it reads
         <b>WI-FI RESET</b>. Letting go early cancels and keeps your settings.</p>
      <p>Either way the device forgets the network and restarts into its own setup
         hotspot, exactly as it did out of the box.</p>
    </div>

    <div class="fix">
      <div class="q">The screen is blank or stuck</div>
      <p>Unplug it, wait a few seconds, and plug it back in. Try a different USB-C
         power supply and cable while you are there — some USB-C cables carry power
         only, and a weak supply can brown out a device that looks fully dead.</p>
      <p>If it stays blank through a power-cycle on known-good power, that is
         hardware. Email us — see <a href="#help">Still stuck</a>.</p>
    </div>

    <div class="fix">
      <div class="q">Touch doesn't respond, or responds in the wrong place</div>
      <p>Power-cycle it first. If touch is consistently offset rather than dead,
         that is worth reporting — include your model and firmware version, both
         shown on the config page.</p>
    </div>

    <div class="fix">
      <div class="q">Detail cards show a type code but no name or photo</div>
      <p>Not everything is in the database. Aircraft whose type is reported
         unconfirmed, and rarer types generally, can come back without a name or
         photo — the position is still correct. If a <b>common</b> airliner shows
         nothing at all, that is a bug worth reporting.</p>
    </div>

    <div class="fix">
      <div class="q">No phone alerts are arriving</div>
      <p>First, check that you have actually switched an alert on. A topic with
         every trigger unticked and an empty watch list is silent by design — see
         <a href="#alerts">Phone alerts</a>.</p>
      <p>Then check the topic in the app matches the config page. Topics are
         <b>case-sensitive</b>; stray spaces around it are not a problem, the device
         trims those.</p>
      <p>Still nothing? Open <code>ntfy.sh/&lt;your-topic&gt;</code> in a browser and
         publish a message to yourself from that page. If it reaches your phone, the
         app and the topic are fine and the device is the thing to look at.</p>
    </div>
  </section>

  <section class="blk">
    <h2 id="data">Where your flight data comes from</h2>

    <h3>Blipscope Cloud — on by default, nothing to set up</h3>
    <p>Your device ships pointed at <b>Blipscope Cloud</b>, and for almost everyone
       that is the whole story. There is no account to make, no key to paste and
       nothing to renew.</p>
    <p>Behind it we aggregate the community ADS-B networks —
       <a href="https://adsb.fi">adsb.fi</a> and
       <a href="https://adsb.lol">adsb.lol</a> — through our own relay, so your
       device makes one request to us instead of competing for a public feed. We
       sponsor both because the data is theirs and volunteers pay for it.</p>
    <p>Aircraft types and operators come from the
       <a href="https://github.com/Mictronics/aircraft-database">Mictronics aircraft
       database</a> (ODC-By 1.0) and
       <a href="https://github.com/sdr-enthusiasts/plane-alert-db">plane-alert-db</a>
       (ODbL 1.0); aircraft photographs from
       <a href="https://commons.wikimedia.org">Wikimedia Commons</a>. Full per-image
       credits are at <a href="/credits">/credits</a>.</p>

    <h3>Your own ADS-B receiver — the best version of this</h3>
    <p>If you run a receiver — dump1090-fa, readsb, PiAware, tar1090 or an ADS-B
       Exchange feeder — point Blipscope at it. This is not a fallback; it is
       better than anything we can send you. Positions come off your own antenna
       with <b>no rate limits</b>, refreshing about once a second, so the radar is
       smoother and genuinely live rather than a picture that updates.</p>
    <p>It is also the strongest privacy position available: the radar never talks
       to us at all. See <a href="#privacy">Privacy</a>.</p>

    <h3>Your own OpenSky account — advanced</h3>
    <p>You can point the device at your own
       <a href="https://opensky-network.org">OpenSky Network</a> account instead of
       Blipscope Cloud. Most people have no reason to: it is slower than both
       options above and needs credentials. It exists for people who already have an
       OpenSky account and would rather use it.</p>
    <p>OpenSky moved to OAuth2 in 2026, so you need a <b>client ID</b> and <b>client
       secret</b> rather than your username and password. Create an API client on
       your OpenSky <b>Account</b> page; the secret is <b>not</b> displayed there —
       it arrives in a downloaded <code>credentials.json</code> you open in any text
       editor. Paste both into the config page.</p>
    <div class="note">
      <p>Treat <code>credentials.json</code> like a password. If you lose the
         secret, use <b>Reset Credential</b> on your OpenSky account page to issue a
         new one — which invalidates the old.</p>
    </div>

    <h3 id="routes">Flight routes — where “LHR → JFK” comes from</h3>
    <p>When a card shows an origin and destination, that comes from a database of
       scheduled airline routes looked up by callsign. It is not read off the
       aircraft: an ADS-B transmission carries a position and a callsign, never a
       flight plan.</p>
    <p><b>We changed where that database comes from.</b> Blipscope now uses a
       public-domain route database that we mirror and control, rather than the
       third-party service used previously. The reason is licensing: the old
       source’s terms could not support a commercial product, and we would rather
       own the data than ship on an unclear footing.</p>
    <p>Two things you may notice, both expected:</p>
    <ul>
      <li><b>Some flights show a different route than before.</b> The two databases
          disagree about certain flight numbers — airlines reassign them between
          seasons, and the two sources were built from different snapshots.</li>
      <li><b>Some flights now show no route at all.</b> If our database has no entry
          for a callsign, the card leaves those fields blank rather than guessing.</li>
    </ul>
    <p>A blank route is the honest answer. The previous data’s provenance wasn’t,
       and that is the trade we chose deliberately. Everything else on the card —
       aircraft type, operator, registration, the photo — is unaffected, and so is
       the radar itself.</p>
    <p>The route database is
       <a href="https://github.com/vradarserver/standing-data">vradarserver/standing-data</a>,
       released into the public domain under CC0 1.0. Corrections go upstream to
       them and reach your device on the next refresh.</p>
  </section>

  <section class="blk">
    <h2 id="alerts">Phone alerts</h2>
    <p>Blipscope can push a notification to your phone when something you care
       about flies over — a tail number you are watching, a military aircraft, an
       emergency squawk. It does this through <a href="https://ntfy.sh">ntfy</a>, a
       free, open-source notification service. <b>There is no account to make.</b></p>

    <h3>Setting it up</h3>
    <ol>
      <li>Install <b>ntfy</b> from the App Store or Google Play — or, if you would
          rather not install anything, open <a href="https://ntfy.sh/app">ntfy.sh/app</a>
          in a browser.</li>
      <li><b>Invent a topic name.</b> A topic is just a word you make up; it is how
          your device and your phone find each other. Make it long and unguessable —
          <code>blipscope-k7m2p9xq</code>, not <code>blipscope</code>. There is a
          reason for that, below.</li>
      <li>In the app, tap <b>+</b> and subscribe to that exact topic.</li>
      <li>On your device's config page, open <b>Watchlist &amp; alerts</b> and paste
          the same topic into <b>ntfy.sh topic (phone alerts)</b>.</li>
      <li><b>Switch on at least one alert</b> — see below — and press Save.</li>
    </ol>

    <div class="note">
      <p><b>The topic on its own does nothing.</b> It is only an address to send to.
         Save a topic but leave every alert switched off and the watch list empty,
         and your phone stays silent — nothing is broken, the device has simply not
         been told that anything is worth reporting.</p>
    </div>

    <h3>What it will tell you</h3>
    <p>Four triggers, all in <b>Watchlist &amp; alerts</b> on the config page, each
       independent of the others:</p>
    <ul>
      <li><b>Watch list</b> — a comma-separated list of callsigns, tail numbers,
          ICAO hex codes or type codes. Anything matching gets a ping, so
          <code>N4523K, BAW117, C172</code> covers one aeroplane, one flight and one
          whole type.</li>
      <li><b>Alert on military</b> — any contact the device recognises as military.
          Worked out on the device from the live feed; no account or lookup needed.</li>
      <li><b>Alert on emergency squawk</b> — a contact squawking 7500, 7600 or 7700.
          These name the squawk: <code>hijack</code>, <code>radio failure</code> or
          <code>general emergency</code>.</li>
      <li><b>“Look up!” overhead alert</b> — anything passing within the distance you
          set. Tick <b>also ntfy</b> beside it, or you get the on-screen ring only.</li>
    </ul>
    <p>A notification names the aircraft the way the detail card does — something
       like <code>BAW117 (B772) British Airways at 4,300 ft</code>.</p>

    <h3>How often it will fire</h3>
    <p><b>Once per aircraft per visit.</b> A contact crossing your radar is
       announced a single time however long it stays. If it leaves and comes back
       later that counts as a new visit, and it can announce itself again.</p>
    <p>Alerts also go out one at a time, a couple of seconds apart, so a busy sky
       never arrives as a burst of notifications.</p>

    <h3>Keep the topic to yourself</h3>
    <p><b>The topic name is the only thing protecting it.</b> ntfy topics are
       public — anyone who knows or guesses yours can subscribe and read your
       alerts. Those alerts describe aircraft passing over your house, which over
       enough of them says a good deal about where you live.</p>
    <p>So treat it as a password rather than a name. A long random string is plenty,
       nobody but you ever has to type it, and if you think it has got out, change it
       on the config page and re-subscribe on your phone.</p>
    <p>Worth knowing either way: alerts are delivered by <b>ntfy.sh</b>, which is not
       us. Even a device running against your own receiver, which otherwise never
       talks to anyone, sends its alerts through them.</p>

    <div class="note">
      <p>Every Valar Scopes edition uses this same field, so one topic and one
         subscription covers whichever device you are running. Only the triggers
         change.</p>
    </div>
  </section>

  <section class="blk">
    <h2 id="updates">Firmware updates</h2>
    <p>Your device updates itself. New firmware is picked up and installed on its
       own, and each model only ever downloads the build made for it — there is
       nothing for you to do, and nothing to plug into a computer.</p>
    <p>Your current firmware version is shown on the config page. If you are
       reporting a problem, include it.</p>
  </section>

  <section class="blk">
    <h2 id="privacy">Privacy</h2>
    <p><b>Blipscope does not track how you use it.</b> Nothing reports which screen
       you look at, how often you tap, how long it is watched, or which aircraft you
       open. That is a permanent decision, not a feature we haven't got to yet.</p>
    <p>What a cloud device does send is operational: that it checked in, its model and
       firmware version, and — after a firmware update — whether the update worked and
       how much free memory it had. No location, no configuration, no serial number,
       no list of aircraft you watched.</p>
    <p>Run it against your own receiver and the radar never talks to us at all. The
       <a href="/blipscope/leaderboard">spotting leaderboard</a> is separate, opt-in,
       and off until you turn it on. The full detail, including exactly what the
       leaderboard submits, is in the
       <a href="https://github.com/Valar-Systems/valar-scopes#privacy--telemetry">Privacy
       &amp; telemetry</a> section of the README.</p>
  </section>

  <section class="blk">
    <h2 id="help">Still stuck</h2>
    <p>Email <a href="mailto:support@valarsystems.com">support@valarsystems.com</a>
       — a real person reads it.</p>
    <p>The <a href="https://github.com/Valar-Systems/valar-scopes/wiki">Valar Scopes
       wiki</a> has a deeper walkthrough of every screen and setting, and anything
       that looks like a bug can go straight to
       <a href="https://github.com/Valar-Systems/valar-scopes/issues">GitHub
       issues</a>.</p>
    <p>Whichever route, include your <b>model</b> and <b>firmware version</b> — both
       are on the config page — and what the screen actually shows. That turns most
       reports into a one-round-trip fix.</p>
  </section>

  <section class="blk builder">
    <div class="lede">For tinkerers</div>
    <h2 id="building">Building it yourself</h2>
    <p>Nothing below is needed to use a Blipscope you bought — it updates itself,
       and opening it is not part of owning one. This is here because the firmware
       is open source and some people would rather build their own.</p>
    <p>Install <a href="https://code.visualstudio.com/">VS Code</a> with the
       PlatformIO extension, open
       <a href="https://github.com/Valar-Systems/valar-scopes">the repository</a>,
       connect a board over USB-C and hit upload. If a board doesn't reboot into new
       firmware by itself, hold <b>BOOT</b>, press <b>RESET</b> once, then release
       <b>BOOT</b> — those buttons are on the bare board, not on an assembled unit.</p>
    <p>The <a href="https://github.com/Valar-Systems/valar-scopes/wiki">wiki</a> covers
       assembly, the build variants and the per-edition guides.</p>
  </section>

  <footer>
    <a href="/">All editions</a>
    <span class="sep">·</span>
    <a href="/blipscope/leaderboard">Leaderboard</a>
    <span class="sep">·</span>
    <!-- LAUNCH: restore this link. The product page is DRAFT and 404s until
         the store opens, and a dead "Order a kit" on the support page is
         worse than no link at all. See RELEASING.md launch checklist. -->
    <a href="mailto:support@valarsystems.com">Contact us</a>
    <span class="sep">·</span>
    <a href="/credits">Photo credits</a>
  </footer>

</div>
</body>
</html>
`;

