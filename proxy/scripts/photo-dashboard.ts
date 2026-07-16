/**
 * photo-dashboard.ts -- local curation dashboard for the stock-photo library.
 *
 *   npm run dashboard          # then open http://127.0.0.1:8123
 *
 * Browse every manifest entry with the EXACT 150x100 crop the device will show,
 * search Wikimedia Commons for replacements (license-checked live against the
 * same per-layer gate the ingest enforces), preview a candidate's crop before
 * committing, swap it into photos/manifest.json + photos/src/, and publish to
 * staging with one click (which just runs the normal ingest -- gate and all).
 *
 * Deliberately LOCAL-ONLY (binds 127.0.0.1): photo writes must flow through the
 * manifest + license gate (the manifest generates the credits page), so there is
 * no KV-direct "swap" and no admin surface on the public Worker.
 */
import { execSync } from "node:child_process";
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import http from "node:http";
import { join } from "node:path";
import {
  classifyLicense,
  isBaselineJpeg,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";

const PORT = 8123;
const PHOTOS_DIR = "photos";
const MANIFEST_PATH = join(PHOTOS_DIR, "manifest.json");
const UA = "BlipscopePhotoDashboard/1.0 (local curation tool)";
const COMMONS_API = "https://commons.wikimedia.org/w/api.php";

// Device sprite dims -- keep in sync with PHOTO_W/PHOTO_H in src/AircraftManager.cpp.
const PHOTO_W = 150;
const PHOTO_H = 100;

// ---------------------------------------------------------------- helpers

function readManifest(): ManifestEntry[] {
  return JSON.parse(readFileSync(MANIFEST_PATH, "utf8")) as ManifestEntry[];
}

function writeManifest(entries: ManifestEntry[]): void {
  writeFileSync(MANIFEST_PATH, JSON.stringify(entries, null, 2) + "\n");
}

async function loadSharp() {
  try {
    return (await import("sharp")).default as unknown as typeof import("sharp");
  } catch {
    throw new Error("sharp is required: npm i -D sharp");
  }
}

// The exact bytes the ingest would upload for this source file -- crop preview
// and published blob can never disagree because they share the encode settings.
async function deviceCrop(srcPath: string): Promise<Buffer> {
  const sharp = await loadSharp();
  const jpeg = await sharp(readFileSync(srcPath))
    .resize(PHOTO_W, PHOTO_H, { fit: "cover" })
    .jpeg({ progressive: false, quality: 82 }) // NO mozjpeg (forces progressive; undecodable on-device)
    .toBuffer();
  if (!isBaselineJpeg(jpeg)) throw new Error("encode produced a non-baseline JPEG");
  return jpeg;
}

async function fetchJson(url: string): Promise<unknown> {
  const res = await fetch(url, { headers: { "User-Agent": UA } });
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return res.json();
}

async function fetchBytes(url: string): Promise<Buffer> {
  const res = await fetch(url, { headers: { "User-Agent": UA } });
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
}

function stripHtml(s: string): string {
  return (s ?? "").replace(/<[^>]+>/g, "").trim();
}

interface Candidate {
  title: string;
  thumb: string; // ~640px thumb for the picker grid
  full: string; // larger rendition the replace step downloads
  descUrl: string;
  license: string;
  licenseClass: string;
  artist: string;
  acceptedIn: { "mil-tier": boolean; auto: boolean };
  rejectReason?: string;
}

// One Commons file-namespace search -> candidates annotated with the gate verdict.
// Rejected candidates are RETURNED (greyed out in the UI with the reason) so the
// curator sees why an otherwise-great photo can't be used.
async function searchCommons(query: string): Promise<Candidate[]> {
  const params = new URLSearchParams({
    action: "query",
    format: "json",
    prop: "imageinfo",
    generator: "search",
    gsrsearch: query,
    gsrnamespace: "6",
    gsrlimit: "24",
    iiprop: "url|mime|extmetadata",
    iiurlwidth: "640",
  });
  const data = (await fetchJson(`${COMMONS_API}?${params}`)) as {
    query?: { pages?: Record<string, any> };
  };
  const out: Candidate[] = [];
  for (const p of Object.values(data.query?.pages ?? {})) {
    const ii = p.imageinfo?.[0];
    if (!ii || ii.mime !== "image/jpeg") continue;
    const em = ii.extmetadata ?? {};
    const license = em.LicenseShortName?.value ?? "";
    const cls = classifyLicense(license);
    // Dry-run the real gate per layer so the verdict can never drift from ingest.
    const probe = (layer: "mil-tier" | "auto") =>
      validateEntry({
        target: "XXXX",
        kind: "type",
        source: "probe",
        author: "probe",
        credit: "probe",
        license,
        layer,
        autoPicked: true,
        changesNoted: "resized for device display",
      }).ok;
    const acceptedIn = { "mil-tier": probe("mil-tier"), auto: probe("auto") };
    let rejectReason: string | undefined;
    if (!acceptedIn.auto) {
      rejectReason =
        cls === "reject-nc" ? "NonCommercial (NC)" :
        cls === "reject-nd" ? "NoDerivatives (ND)" :
        `unrecognized license "${license}"`;
    } else if (!acceptedIn["mil-tier"]) {
      rejectReason = "CC-BY-SA: auto layer only";
    }
    out.push({
      title: p.title,
      thumb: ii.thumburl ?? ii.url,
      full: ii.url, // original; the replace step re-encodes anyway
      descUrl: ii.descriptionurl,
      license,
      licenseClass: cls,
      artist: stripHtml(em.Artist?.value ?? "") || "Unknown",
      acceptedIn,
      rejectReason,
    });
  }
  return out;
}

// Exact-title imageinfo lookup used by /api/replace (the search result the user
// clicked), so the manifest row is filled from Commons' own metadata.
async function commonsFileInfo(title: string): Promise<Candidate | null> {
  const params = new URLSearchParams({
    action: "query",
    format: "json",
    prop: "imageinfo",
    titles: title,
    iiprop: "url|mime|extmetadata",
    iiurlwidth: "1280",
  });
  const data = (await fetchJson(`${COMMONS_API}?${params}`)) as {
    query?: { pages?: Record<string, any> };
  };
  for (const p of Object.values(data.query?.pages ?? {})) {
    const ii = p.imageinfo?.[0];
    if (!ii) continue;
    const em = ii.extmetadata ?? {};
    const license = em.LicenseShortName?.value ?? "";
    return {
      title: p.title,
      thumb: ii.thumburl ?? ii.url,
      full: ii.thumburl ?? ii.url, // 1280px rendition: plenty for a 150x100 crop
      descUrl: ii.descriptionurl,
      license,
      licenseClass: classifyLicense(license),
      artist: stripHtml(em.Artist?.value ?? "") || "Unknown",
      acceptedIn: { "mil-tier": true, auto: true }, // real verdict comes from validateEntry below
    };
  }
  return null;
}

// ---------------------------------------------------------------- routes

async function handleApi(req: http.IncomingMessage, res: http.ServerResponse, url: URL): Promise<void> {
  const json = (status: number, body: unknown) => {
    res.writeHead(status, { "Content-Type": "application/json" });
    res.end(JSON.stringify(body));
  };
  const jpeg = (buf: Buffer) => {
    res.writeHead(200, { "Content-Type": "image/jpeg", "Cache-Control": "no-store" });
    res.end(buf);
  };
  const readBody = () =>
    new Promise<any>((resolve, reject) => {
      let s = "";
      req.on("data", (c) => (s += c));
      req.on("end", () => {
        try { resolve(s ? JSON.parse(s) : {}); } catch (e) { reject(e); }
      });
    });

  // GET /api/manifest -- entries + per-row gate verdict + pending-publish flag
  if (url.pathname === "/api/manifest") {
    const entries = readManifest().map((e) => ({
      ...e,
      valid: validateEntry(e),
    }));
    return json(200, { entries, photoW: PHOTO_W, photoH: PHOTO_H });
  }

  // GET /api/current/<target> -- the device crop of the entry's local source
  const cur = url.pathname.match(/^\/api\/current\/([A-Za-z0-9~]+)$/);
  if (cur) {
    const entry = readManifest().find((e) => e.target === cur[1]);
    if (!entry?.file) return json(404, { error: "no local source for entry" });
    return jpeg(await deviceCrop(join(PHOTOS_DIR, entry.file)));
  }

  // GET /api/search?q=... -- Commons candidates with live gate verdicts
  if (url.pathname === "/api/search") {
    const q = url.searchParams.get("q")?.trim();
    if (!q) return json(400, { error: "missing q" });
    return json(200, { candidates: await searchCommons(q) });
  }

  // POST /api/preview {url} -- device crop of a candidate before committing
  if (url.pathname === "/api/preview" && req.method === "POST") {
    const { url: imgUrl } = await readBody();
    if (typeof imgUrl !== "string" || !/^https:\/\/upload\.wikimedia\.org\//.test(imgUrl))
      return json(400, { error: "only upload.wikimedia.org sources" });
    const sharp = await loadSharp();
    const buf = await sharp(await fetchBytes(imgUrl))
      .resize(PHOTO_W, PHOTO_H, { fit: "cover" })
      .jpeg({ progressive: false, quality: 82 })
      .toBuffer();
    return jpeg(buf);
  }

  // POST /api/replace {target, kind?, layer?, title} -- swap (or add) an entry's
  // photo: fetch Commons metadata for the exact file, run the REAL gate, download
  // the source into photos/src/, and rewrite the manifest row. Publish is separate.
  if (url.pathname === "/api/replace" && req.method === "POST") {
    const { target, kind = "type", layer = "auto", title } = await readBody();
    if (!target || !title) return json(400, { error: "missing target/title" });

    const info = await commonsFileInfo(title);
    if (!info) return json(404, { error: `Commons file not found: ${title}` });

    const entries = readManifest();
    const existing = entries.find((e) => e.target === target);
    const fileName = `src/${String(target).toLowerCase()}.jpg`;
    const entry: ManifestEntry = {
      target,
      kind: (existing?.kind ?? kind) as "type" | "hex",
      source: info.descUrl,
      author: info.artist,
      credit: stripHtml(title).replace(/^File:/, "").replace(/\.[a-z]+$/i, ""),
      license: info.license,
      layer: (existing?.layer ?? layer) as "mil-tier" | "auto",
      autoPicked: false, // a human just picked it
      file: fileName,
    };
    if (classifyLicense(info.license) === "CC-BY-SA")
      entry.changesNoted = "resized for device display";

    // The same gate the ingest runs -- reject BEFORE touching disk or manifest.
    const verdict = validateEntry(entry);
    if (!verdict.ok) return json(422, { error: "license gate rejected", details: verdict.errors });

    mkdirSync(join(PHOTOS_DIR, "src"), { recursive: true });
    writeFileSync(join(PHOTOS_DIR, entry.file!), await fetchBytes(info.full));
    if (existing) Object.assign(existing, entry);
    else entries.push(entry);
    writeManifest(entries);
    return json(200, { ok: true, entry });
  }

  // POST /api/publish -- run the normal ingest against staging (gate included)
  if (url.pathname === "/api/publish" && req.method === "POST") {
    try {
      const out = execSync("npx tsx scripts/ingest-photos.ts --env staging", {
        encoding: "utf8",
        stdio: ["ignore", "pipe", "pipe"],
        timeout: 300_000,
      });
      return json(200, { ok: true, output: out });
    } catch (err: any) {
      return json(500, { ok: false, output: `${err.stdout ?? ""}\n${err.stderr ?? ""}\n${err.message}` });
    }
  }

  json(404, { error: "not_found" });
}

// ---------------------------------------------------------------- UI

const PAGE = `<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blipscope photo curation</title>
<style>
  :root{--bg:#0d1117;--panel:#161b22;--line:#30363d;--fg:#e6edf3;--dim:#8b949e;--acc:#3fb950;--warn:#d29922;--err:#f85149}
  *{box-sizing:border-box}
  body{margin:0;font:14px/1.45 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
  header{display:flex;align-items:center;gap:12px;padding:12px 20px;border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);z-index:5}
  h1{font-size:15px;margin:0} .dim{color:var(--dim)}
  button{background:#21262d;color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:6px 12px;cursor:pointer}
  button:hover{border-color:var(--dim)} button.primary{background:var(--acc);border-color:var(--acc);color:#04170a;font-weight:600}
  button:disabled{opacity:.5;cursor:default}
  main{max-width:1100px;margin:0 auto;padding:20px}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:14px}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;display:flex;gap:12px}
  .card img{width:150px;height:100px;border-radius:4px;border:1px solid var(--line);image-rendering:pixelated;flex:none}
  .meta{min-width:0} .meta b{font-size:16px}
  .badge{display:inline-block;font-size:11px;padding:1px 7px;border-radius:10px;border:1px solid var(--line);color:var(--dim);margin-right:4px}
  .badge.ok{color:var(--acc);border-color:var(--acc)} .badge.warn{color:var(--warn);border-color:var(--warn)} .badge.err{color:var(--err);border-color:var(--err)}
  .credit{font-size:12px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:220px}
  #picker{position:fixed;inset:0;background:rgba(0,0,0,.65);display:none;align-items:flex-start;justify-content:center;overflow:auto;z-index:10}
  #picker.open{display:flex}
  .sheet{background:var(--panel);border:1px solid var(--line);border-radius:10px;margin:5vh 16px;padding:18px;width:min(980px,94vw)}
  .sheet .row{display:flex;gap:8px;margin-bottom:12px}
  .sheet input{flex:1;background:#0d1117;border:1px solid var(--line);border-radius:6px;color:var(--fg);padding:7px 10px}
  .cands{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:10px}
  .cand{border:1px solid var(--line);border-radius:8px;padding:8px;cursor:pointer}
  .cand:hover{border-color:var(--acc)} .cand.rejected{opacity:.45;cursor:not-allowed}
  .cand img{width:100%;height:110px;object-fit:cover;border-radius:4px}
  .cand .t{font-size:11px;color:var(--dim);margin-top:4px;height:2.6em;overflow:hidden}
  #confirm{display:none;margin-top:14px;border-top:1px solid var(--line);padding-top:14px}
  #confirm.open{display:flex;gap:16px;align-items:center}
  #confirm img{width:150px;height:100px;border-radius:4px;border:1px solid var(--acc);image-rendering:pixelated}
  #log{white-space:pre-wrap;font:12px/1.4 ui-monospace,monospace;background:#0d1117;border:1px solid var(--line);border-radius:6px;padding:10px;margin-top:14px;display:none;max-height:260px;overflow:auto}
  .addrow{display:flex;gap:8px;margin:18px 0 6px}
  .addrow input,.addrow select{background:#0d1117;border:1px solid var(--line);border-radius:6px;color:var(--fg);padding:7px 10px}
</style></head><body>
<header>
  <h1>Blipscope photo curation</h1>
  <span class="dim" id="count"></span>
  <span style="flex:1"></span>
  <button class="primary" id="publish">Publish to staging</button>
</header>
<main>
  <div class="grid" id="entries"></div>
  <div class="addrow">
    <input id="newTarget" placeholder="New type code (e.g. B738) or hex" style="width:220px">
    <select id="newLayer"><option value="auto">auto</option><option value="mil-tier">mil-tier</option></select>
    <button id="addBtn">Add + pick photo…</button>
  </div>
  <div id="log"></div>
</main>

<div id="picker"><div class="sheet">
  <div class="row">
    <b id="pickTitle" style="align-self:center"></b>
    <span style="flex:1"></span>
    <button onclick="closePicker()">Close</button>
  </div>
  <div class="row">
    <input id="q" placeholder="Search Wikimedia Commons…">
    <button id="searchBtn">Search</button>
  </div>
  <div class="dim" id="pickHint" style="margin-bottom:10px">Greyed-out results fail the license gate (reason on hover). Click a result to preview its exact device crop.</div>
  <div class="cands" id="cands"></div>
  <div id="confirm">
    <img id="cropPrev" alt="device crop preview">
    <div style="min-width:0">
      <div id="confTitle" style="font-size:13px"></div>
      <div class="dim" id="confLicense"></div>
      <div style="margin-top:8px"><button class="primary" id="useBtn">Use this photo</button></div>
    </div>
  </div>
</div></div>

<script>
let pickTarget = null, pickLayer = "auto", picked = null;

async function load() {
  const r = await fetch("/api/manifest"); const d = await r.json();
  document.getElementById("count").textContent = d.entries.length + " entries";
  const grid = document.getElementById("entries"); grid.innerHTML = "";
  for (const e of d.entries) {
    const div = document.createElement("div"); div.className = "card";
    const licBadge = e.valid.ok ? '<span class="badge ok">' + e.license + '</span>'
                                : '<span class="badge err" title="' + e.valid.errors.join("; ") + '">gate: FAIL</span>';
    div.innerHTML =
      '<img src="/api/current/' + e.target + '?t=' + Date.now() + '" onerror="this.style.opacity=.2">' +
      '<div class="meta"><b>' + e.target + '</b> <span class="badge">' + e.kind + '</span>' +
      '<span class="badge">' + e.layer + '</span> ' + licBadge +
      '<div class="credit" title="' + e.credit + '">' + e.credit + '</div>' +
      '<div class="credit">' + e.author + '</div>' +
      '<div style="margin-top:8px"><button onclick="openPicker(\\'' + e.target + '\\',\\'' + e.layer + '\\')">Replace…</button> ' +
      '<a class="dim" style="font-size:12px" href="' + e.source + '" target="_blank">source ↗</a></div></div>';
    grid.appendChild(div);
  }
}

function openPicker(target, layer) {
  pickTarget = target; pickLayer = layer; picked = null;
  document.getElementById("pickTitle").textContent = "Photo for " + target + " (" + layer + ")";
  document.getElementById("q").value = target + " aircraft";
  document.getElementById("cands").innerHTML = "";
  document.getElementById("confirm").classList.remove("open");
  document.getElementById("picker").classList.add("open");
}
function closePicker(){ document.getElementById("picker").classList.remove("open"); }

async function doSearch() {
  const q = document.getElementById("q").value.trim(); if (!q) return;
  const box = document.getElementById("cands");
  box.innerHTML = '<span class="dim">Searching…</span>';
  const r = await fetch("/api/search?q=" + encodeURIComponent(q)); const d = await r.json();
  box.innerHTML = "";
  for (const c of d.candidates) {
    const ok = pickLayer === "mil-tier" ? c.acceptedIn["mil-tier"] : c.acceptedIn.auto;
    const div = document.createElement("div");
    div.className = "cand" + (ok ? "" : " rejected");
    div.title = ok ? c.license + " — " + c.artist : "REJECTED: " + c.rejectReason;
    div.innerHTML = '<img loading="lazy" src="' + c.thumb + '"><div class="t">' + c.title.replace("File:","") +
      '<br><span class="' + (ok ? "" : "dim") + '">' + c.license + '</span></div>';
    if (ok) div.onclick = () => preview(c);
    box.appendChild(div);
  }
  if (!d.candidates.length) box.innerHTML = '<span class="dim">No JPEG results.</span>';
}

async function preview(c) {
  picked = c;
  document.getElementById("confirm").classList.add("open");
  document.getElementById("confTitle").textContent = c.title;
  document.getElementById("confLicense").textContent = c.license + " — " + c.artist;
  const img = document.getElementById("cropPrev"); img.src = ""; img.alt = "cropping…";
  const r = await fetch("/api/preview", { method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({ url: c.full }) });
  if (r.ok) img.src = URL.createObjectURL(await r.blob());
}

document.getElementById("searchBtn").onclick = doSearch;
document.getElementById("q").addEventListener("keydown", e => { if (e.key === "Enter") doSearch(); });

document.getElementById("useBtn").onclick = async () => {
  if (!picked) return;
  const r = await fetch("/api/replace", { method:"POST", headers:{"Content-Type":"application/json"},
    body: JSON.stringify({ target: pickTarget, layer: pickLayer, title: picked.title }) });
  const d = await r.json();
  if (!r.ok) { alert("Rejected: " + (d.details ? d.details.join("; ") : d.error)); return; }
  closePicker(); load();
};

document.getElementById("addBtn").onclick = () => {
  const t = document.getElementById("newTarget").value.trim().toUpperCase();
  if (!t) return;
  openPicker(t, document.getElementById("newLayer").value);
};

document.getElementById("publish").onclick = async () => {
  const btn = document.getElementById("publish"); btn.disabled = true; btn.textContent = "Publishing…";
  const log = document.getElementById("log"); log.style.display = "block"; log.textContent = "Running ingest --env staging…";
  const r = await fetch("/api/publish", { method: "POST" });
  const d = await r.json();
  log.textContent = d.output || JSON.stringify(d);
  btn.disabled = false; btn.textContent = "Publish to staging";
};

load();
</script>
</body></html>`;

// ---------------------------------------------------------------- server

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", `http://127.0.0.1:${PORT}`);
  try {
    if (url.pathname === "/") {
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      res.end(PAGE);
      return;
    }
    if (url.pathname.startsWith("/api/")) return await handleApi(req, res, url);
    res.writeHead(404); res.end("not found");
  } catch (err) {
    res.writeHead(500, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: String(err instanceof Error ? err.message : err) }));
  }
});

server.listen(PORT, "127.0.0.1", () => {
  console.log(`Photo dashboard: http://127.0.0.1:${PORT}`);
});
