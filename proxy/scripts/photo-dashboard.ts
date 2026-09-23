/**
 * photo-dashboard.ts -- local curation dashboard for the stock-photo library,
 * and the ONE CLICK that puts a photo on devices.
 *
 *   npm run dashboard          # then open http://127.0.0.1:8123
 *
 * Browse every manifest entry as the 240 px square the Kit S3 draws, search
 * Wikimedia Commons for replacements (licence-checked live against the same
 * per-layer gate the ingest enforces), preview a candidate's square before
 * committing, and PUBLISH TO DEVICES: the dashboard commits the changed rows to
 * main (photos only, fast-forward only), and the `photos` workflow writes
 * production behind its verifier. The status panel reads that run's verdict
 * back from the commit. See "Photo publish pipeline: design".
 *
 * THE DASHBOARD NEVER WRITES KV. It writes the repo; CI turns the repo into
 * production. It needs a GitHub token (contents + actions + checks, this repo
 * only) in a file only Daniel can read -- see TOKEN_FILE. The value is never
 * logged, only its presence.
 *
 * Deliberately LOCAL-ONLY (binds 127.0.0.1).
 */
import { execSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync, mkdirSync } from "node:fs";
import http from "node:http";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";
import {
  classifyLicense,
  renderCreditsHtml,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";
import {
  NotFastForward,
  commitToMain,
  commitsForPath,
  dispatchPhotos,
  fileAt,
  mainHead,
  publishCheck,
  recentPhotoRuns,
  type FileChange,
  type Gh,
} from "./github-api";
import { renderSquares } from "./photo-render";
import { PHOTOS_PREFIX, assertPhotoPaths, planPublish, validateRows } from "./publish-plan";
import { RateLimited, wikimediaFetch } from "./wikimedia-fetch";

const PORT = 8123;
const PHOTOS_DIR = "photos";
const MANIFEST_PATH = join(PHOTOS_DIR, "manifest.json");
const UA = "BlipscopePhotoDashboard/1.0 (local curation tool)";
const COMMONS_API = "https://commons.wikimedia.org/w/api.php";
const REPO = process.env.BLIPSCOPE_REPO ?? "Valar-Systems/valar-scopes";

// The panel size previews render at: the Kit S3, the default SKU.
const PREVIEW_SIZE = 240;

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

// The square a Kit S3 draws, from the SAME renderer the ingest uploads with --
// so the preview is those bytes, not an approximation of them.
async function devicePreview(src: Buffer, row: { kind: string; target: string; focus?: [number, number]; zoom?: number }): Promise<Buffer> {
  const sharp = await loadSharp();
  const [sq] = await renderSquares(sharp, src, row, [PREVIEW_SIZE]);
  return sq!.buf;
}

// ---------------------------------------------------------------- the token

// Daniel's, alone: a fine-grained token for this repo only (Contents read/write,
// Actions read/write, Checks read), in a file under his profile. Refused when
// missing, and refused when it sits inside a git working tree, where one
// `git add -A` would commit it.
const TOKEN_FILE = process.env.BLIPSCOPE_GH_TOKEN_FILE ?? join(homedir(), ".config", "blipscope", "github-token");

function insideGitTree(p: string): boolean {
  let d = dirname(resolve(p));
  for (;;) {
    if (existsSync(join(d, ".git"))) return true;
    const up = dirname(d);
    if (up === d) return false;
    d = up;
  }
}

function loadToken(): { gh?: Gh; reason?: string } {
  if (!existsSync(TOKEN_FILE)) return { reason: `no GitHub token file at ${TOKEN_FILE}` };
  if (insideGitTree(TOKEN_FILE)) return { reason: `the token file ${TOKEN_FILE} is inside a git working tree; move it out` };
  const token = readFileSync(TOKEN_FILE, "utf8").trim();
  if (!token) return { reason: `the token file ${TOKEN_FILE} is empty` };
  return { gh: { repo: REPO, token } };
}

// ---------------------------------------------------------------- the base

// main's manifest as this dashboard last saw it. A local row counts as an edit
// only if it differs from here, so a stale checkout can never publish someone
// else's newer row back to an older one (publish-plan.ts). Kept beside the
// photos, gitignored, and first seeded from the checkout's own HEAD.
const BASE_FILE = ".photo-dashboard-base.json";

function loadBase(): ManifestEntry[] {
  if (existsSync(BASE_FILE)) return JSON.parse(readFileSync(BASE_FILE, "utf8")) as ManifestEntry[];
  try {
    const head = execSync("git show HEAD:proxy/photos/manifest.json", { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
    return JSON.parse(head) as ManifestEntry[];
  } catch {
    return readManifest();
  }
}

function saveBase(m: ManifestEntry[]): void {
  writeFileSync(BASE_FILE, JSON.stringify(m) + "\n");
}

// Every Wikimedia call retries once on 429 -- see wikimedia-fetch.ts for why.
const wm = (url: string) => wikimediaFetch(url, UA, { log: console.log });

async function fetchJson(url: string): Promise<unknown> {
  return (await wm(url)).json();
}

async function fetchBytes(url: string): Promise<Buffer> {
  return Buffer.from(await (await wm(url)).arrayBuffer());
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
  wikiLead?: boolean; // the Wikipedia article's lead (infobox) image for the query
}

// Gate verdict for a license string, per layer -- a dry-run of the REAL
// validateEntry so the picker's verdict can never drift from the ingest.
function gateVerdict(license: string): Pick<Candidate, "acceptedIn" | "rejectReason"> {
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
  const cls = classifyLicense(license);
  let rejectReason: string | undefined;
  if (!acceptedIn.auto) {
    rejectReason =
      cls === "reject-nc" ? "NonCommercial (NC)" :
      cls === "reject-nd" ? "NoDerivatives (ND)" :
      `unrecognized license "${license}"`;
  } else if (!acceptedIn["mil-tier"]) {
    rejectReason = "CC-BY-SA: auto layer only";
  }
  return { acceptedIn, rejectReason };
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
    out.push({
      title: p.title,
      thumb: ii.thumburl ?? ii.url,
      full: ii.url, // original; the replace step re-encodes anyway
      descUrl: ii.descriptionurl,
      license,
      licenseClass: classifyLicense(license),
      artist: stripHtml(em.Artist?.value ?? "") || "Unknown",
      ...gateVerdict(license),
    });
  }
  return out;
}

// The Wikipedia article's lead (infobox) image for a query -- the
// community-curated canonical shot (the playbook's P18 pick), always the first
// candidate to consider. Still license-checked like everything else: e.g. the
// Cessna 172 article's lead is GFDL-1.2-only, which the gate rightly rejects
// (GFDL requires shipping the full license text with the image).
async function wikipediaLead(query: string): Promise<Candidate | null> {
  const params = new URLSearchParams({
    action: "query",
    format: "json",
    generator: "search",
    gsrsearch: query,
    gsrlimit: "1",
    prop: "pageimages",
    piprop: "name",
    pilicense: "free",
  });
  const data = (await fetchJson(`https://en.wikipedia.org/w/api.php?${params}`)) as {
    query?: { pages?: Record<string, any> };
  };
  const page = Object.values(data.query?.pages ?? {})[0];
  if (!page?.pageimage) return null;

  const info = await commonsFileInfo(`File:${page.pageimage}`);
  if (!info) return null;
  return { ...info, ...gateVerdict(info.license), wikiLead: true };
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

// ---------------------------------------------------------------- publish

interface PublishResult {
  status: number;
  body: Record<string, unknown>;
}

const sameRow = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b);

// Commit this dashboard's edits to main, and nothing else. Every refusal happens
// BEFORE the commit: malformed rows (A17), a dropped row, a row someone else
// changed on main since (A6), a path outside proxy/photos (A15). A race with
// another publish (A5) re-reads main and tries again; the ref update is
// fast-forward only, so a lost race can never discard the other commit.
async function publishToDevices(note = ""): Promise<PublishResult> {
  const { gh, reason } = loadToken();
  if (!gh) return { status: 401, body: { error: `Publish is off: ${reason}.` } };

  const local = readManifest();
  const problems = validateRows(local);
  if (problems.length) {
    return {
      status: 422,
      body: { error: `refused before committing: ${problems.map((p) => `${p.target} (${p.errors.join("; ")})`).join(", ")}`, rows: problems },
    };
  }
  const base = loadBase();
  for (let attempt = 1; attempt <= 3; attempt++) {
    const head = await mainHead(gh);
    const currentBytes = await fileAt(gh, `${PHOTOS_PREFIX}manifest.json`, head);
    if (!currentBytes) return { status: 500, body: { error: "main has no proxy/photos/manifest.json" } };
    const current = JSON.parse(currentBytes.toString("utf8")) as ManifestEntry[];
    const plan = planPublish(base, current, local);
    if (plan.removed.length) {
      return { status: 422, body: { error: `refused: this publish would remove ${plan.removed.join(", ")}. A photo publish never removes a row.` } };
    }
    if (plan.conflicts.length) {
      return {
        status: 409,
        body: { error: `refused: ${plan.conflicts.join(", ")} changed on main since this dashboard loaded ${plan.conflicts.length > 1 ? "them" : "it"}. Reload and re-pick.`, conflicts: plan.conflicts },
      };
    }
    if (!plan.changed.length) return { status: 200, body: { nothing: true, message: "Nothing to publish: main already has every row shown here." } };

    // The commit: the merged manifest, credits rendered from it (the same
    // function CI uses, so the file cannot disagree with production), and the
    // source image of every changed row.
    const files: FileChange[] = [
      { path: `${PHOTOS_PREFIX}manifest.json`, bytes: Buffer.from(JSON.stringify(plan.merged, null, 2) + "\n") },
      { path: `${PHOTOS_PREFIX}credits.html`, bytes: Buffer.from(renderCreditsHtml(plan.merged.map(({ file, ...rest }) => rest))) },
    ];
    for (const t of plan.changed) {
      const row = plan.merged.find((e) => e.target === t)!;
      files.push({ path: `${PHOTOS_PREFIX}${row.file}`, bytes: readFileSync(join(PHOTOS_DIR, row.file!)) });
    }
    // TEST SEAM for A15, off unless set in the dashboard's own environment: add
    // a path the guard must refuse, to show the refusal happens before commit.
    if (process.env.PHOTO_DASHBOARD_TEST_EXTRA_PATH) {
      files.push({ path: process.env.PHOTO_DASHBOARD_TEST_EXTRA_PATH, bytes: Buffer.from("test\n") });
    }
    try {
      assertPhotoPaths(files.map((f) => f.path));
    } catch (err) {
      return { status: 422, body: { error: String(err instanceof Error ? err.message : err) } };
    }

    const message =
      `photos: ${plan.changed.join(", ")}${note ? ` -- ${note}` : ""}\n\n` +
      `Published from the photo dashboard. Rows changed: ${plan.changed.length}.\n` +
      `The photos workflow publishes this commit to production and reports the\n` +
      `verdict on it as the photos-publish check run.\n`;
    try {
      const sha = await commitToMain(gh, head, files, message);
      saveBase(plan.merged);
      writeManifest(plan.merged);
      writeFileSync(join(PHOTOS_DIR, "credits.html"), files[1]!.bytes!);
      console.log(`[publish] ${sha.slice(0, 7)}: ${plan.changed.join(", ")}`);
      return { status: 200, body: { sha, changed: plan.changed, attempt } };
    } catch (err) {
      if (err instanceof NotFastForward) {
        console.log(`[publish] main moved during commit (attempt ${attempt}); re-reading main`);
        continue;
      }
      throw err;
    }
  }
  return { status: 409, body: { error: "main kept moving while publishing; nothing was committed. Try again." } };
}

// Put a row back to the version it had before its latest change, from the
// repo's own history, and publish that. The previous blob is still in KV (the
// ingest never deletes), so the same bytes produce the same key: nothing new.
async function revertRow(target: string): Promise<PublishResult> {
  const { gh, reason } = loadToken();
  if (!gh) return { status: 401, body: { error: `Revert is off: ${reason}.` } };
  const path = `${PHOTOS_PREFIX}manifest.json`;
  const head = await mainHead(gh);
  const current = JSON.parse((await fileAt(gh, path, head))!.toString("utf8")) as ManifestEntry[];
  const now = current.find((e) => e.target === target);
  if (!now) return { status: 404, body: { error: `${target} is not in main's manifest` } };
  for (const sha of await commitsForPath(gh, path)) {
    const m = JSON.parse((await fileAt(gh, path, sha))!.toString("utf8")) as ManifestEntry[];
    const then = m.find((e) => e.target === target);
    if (!then || sameRow(then, now)) continue;
    const src = await fileAt(gh, `${PHOTOS_PREFIX}${then.file}`, sha);
    if (!src) return { status: 500, body: { error: `${then.file} is missing at ${sha.slice(0, 7)}` } };
    // Stage it exactly as a Replace would, against main as it is now.
    saveBase(current);
    const local = current.map((e) => (e.target === target ? then : e));
    writeFileSync(join(PHOTOS_DIR, then.file!), src);
    writeManifest(local);
    return publishToDevices(`revert ${target} to its version at ${sha.slice(0, 7)}`);
  }
  return { status: 404, body: { error: `no earlier version of ${target} in the last 30 photo commits` } };
}

// What the fleet has, read back from the runs and the verdicts on their
// commits. Newest first; the dashboard shows the first prominently.
async function publishStatus(): Promise<PublishResult> {
  const { gh, reason } = loadToken();
  if (!gh) return { status: 200, body: { tokenPresent: false, reason, runs: [] } };
  const runs = await recentPhotoRuns(gh);
  const out = [];
  for (const r of runs) {
    const check = r.status === "completed" ? await publishCheck(gh, r.sha, r.createdAt, r.updatedAt) : null;
    let detail: Record<string, unknown> = {};
    try { detail = check?.text ? JSON.parse(check.text) : {}; } catch { /* a check without our JSON */ }
    const state =
      r.status !== "completed" ? (r.status === "queued" || r.status === "waiting" || r.status === "pending" ? "QUEUED" : "RUNNING")
      : r.conclusion === "cancelled" ? "SUPERSEDED"
      : (detail.verdict as string | undefined) ?? (r.conclusion === "success" ? "LIVE" : "FAILED");
    out.push({
      runId: r.id, sha: r.sha, event: r.event, state, url: r.url, createdAt: r.createdAt, updatedAt: r.updatedAt,
      title: check?.title ?? "", reason: detail.reason ?? "", liveAt: detail.liveAt ?? "",
      live: detail.live ?? [], failed: detail.failed ?? [], notReached: detail.notReached ?? [],
      verifyMs: detail.verifyMs ?? null, pointersChecked: detail.pointersChecked ?? null,
    });
  }
  return { status: 200, body: { tokenPresent: true, runs: out } };
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

  // GET /api/manifest -- entries + per-row gate verdict + whether it differs from main as last seen
  if (url.pathname === "/api/manifest") {
    const base = new Map(loadBase().map((e) => [`${e.kind}:${e.target}`, e]));
    const entries = readManifest().map((e) => ({
      ...e,
      valid: validateEntry(e),
      unpublished: !sameRow(base.get(`${e.kind}:${e.target}`), e),
    }));
    const t = loadToken();
    return json(200, { entries, previewSize: PREVIEW_SIZE, tokenPresent: !!t.gh, tokenReason: t.reason ?? "" });
  }

  // GET /api/current/<target> -- the square a Kit S3 draws for this row
  const cur = url.pathname.match(/^\/api\/current\/([A-Za-z0-9~]+)$/);
  if (cur) {
    const entry = readManifest().find((e) => e.target === cur[1]);
    if (!entry?.file) return json(404, { error: "no local source for entry" });
    return jpeg(await devicePreview(readFileSync(join(PHOTOS_DIR, entry.file)), entry));
  }

  // GET /api/search?q=... -- the Wikipedia lead image first (curated pick),
  // then Commons search candidates; all with live gate verdicts.
  if (url.pathname === "/api/search") {
    const q = url.searchParams.get("q")?.trim();
    if (!q) return json(400, { error: "missing q" });
    const [lead, found] = await Promise.all([
      wikipediaLead(q).catch(() => null),
      searchCommons(q),
    ]);
    const candidates = lead
      ? [lead, ...found.filter((c) => c.title !== lead.title)]
      : found;
    return json(200, { candidates });
  }

  // POST /api/preview {url, target} -- the square a candidate would become
  if (url.pathname === "/api/preview" && req.method === "POST") {
    const { url: imgUrl, target } = await readBody();
    if (typeof imgUrl !== "string" || !/^https:\/\/upload\.wikimedia\.org\//.test(imgUrl))
      return json(400, { error: "only upload.wikimedia.org sources" });
    return jpeg(await devicePreview(await fetchBytes(imgUrl), { kind: "type", target: String(target ?? "PREVIEW") }));
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

  // POST /api/publish -- commit this dashboard's edits to main (see publishToDevices)
  if (url.pathname === "/api/publish" && req.method === "POST") {
    const r = await publishToDevices();
    return json(r.status, r.body);
  }

  // POST /api/revert {target}
  if (url.pathname === "/api/revert" && req.method === "POST") {
    const { target } = await readBody();
    if (typeof target !== "string" || !target) return json(400, { error: "missing target" });
    const r = await revertRow(target);
    return json(r.status, r.body);
  }

  // GET /api/publishes -- the recent runs and their verdicts
  if (url.pathname === "/api/publishes") {
    const r = await publishStatus();
    return json(r.status, r.body);
  }

  // POST /api/retry -- a fresh run of main (see dispatchPhotos for why not a re-run)
  if (url.pathname === "/api/retry" && req.method === "POST") {
    const { gh, reason } = loadToken();
    if (!gh) return json(401, { error: `Retry is off: ${reason}.` });
    await dispatchPhotos(gh);
    return json(200, { ok: true });
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
  header{display:flex;align-items:center;gap:12px;padding:12px 20px;border-bottom:1px solid var(--line);position:sticky;top:0;background:var(--bg);z-index:5;flex-wrap:wrap}
  h1{font-size:15px;margin:0} .dim{color:var(--dim)}
  button{background:#21262d;color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:6px 12px;cursor:pointer}
  button:hover{border-color:var(--dim)} button.primary{background:var(--acc);border-color:var(--acc);color:#04170a;font-weight:600}
  button:disabled{opacity:.5;cursor:default}
  main{max-width:1100px;margin:0 auto;padding:20px}
  #status{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px 14px;margin-bottom:16px}
  #status .head{font-size:15px;font-weight:600} #status .rows{font-size:12px;margin-top:6px}
  #status.live{border-color:var(--acc)} #status.live .head{color:var(--acc)}
  #status.bad{border-color:var(--err)} #status.bad .head{color:var(--err)}
  #status.busy{border-color:var(--warn)} #status.busy .head{color:var(--warn)}
  #history{font-size:12px;margin-top:8px} #history div{margin-top:2px}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:14px}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;display:flex;gap:12px}
  .card.unpub{border-color:var(--warn)}
  .card img{width:120px;height:120px;border-radius:50%;border:1px solid var(--line);flex:none}
  .meta{min-width:0} .meta b{font-size:16px}
  .badge{display:inline-block;font-size:11px;padding:1px 7px;border-radius:10px;border:1px solid var(--line);color:var(--dim);margin-right:4px}
  .badge.ok{color:var(--acc);border-color:var(--acc)} .badge.warn{color:var(--warn);border-color:var(--warn)} .badge.err{color:var(--err);border-color:var(--err)}
  .credit{font-size:12px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:180px}
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
  #confirm img{width:160px;height:160px;border-radius:50%;border:1px solid var(--acc)}
  .addrow{display:flex;gap:8px;margin:18px 0 6px}
  .addrow input,.addrow select{background:#0d1117;border:1px solid var(--line);border-radius:6px;color:var(--fg);padding:7px 10px}
</style></head><body>
<header>
  <h1>Blipscope photo curation</h1>
  <span class="dim" id="count"></span>
  <span style="flex:1"></span>
  <span class="dim" id="tokenNote"></span>
  <button class="primary" id="publish" disabled>Publish to devices</button>
</header>
<main>
  <div id="status"><div class="head">Loading publish status…</div><div class="rows dim"></div><div id="history" class="dim"></div></div>
  <div class="grid" id="entries"></div>
  <div class="addrow">
    <input id="newTarget" placeholder="New type code (e.g. B738) or hex" style="width:220px">
    <select id="newLayer"><option value="auto">auto</option><option value="mil-tier">mil-tier</option></select>
    <button id="addBtn">Add + pick photo…</button>
  </div>
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
  <div class="dim" id="pickHint" style="margin-bottom:10px">Greyed-out results fail the license gate (reason on hover). Click a result to see the exact square a device draws.</div>
  <div class="cands" id="cands"></div>
  <div id="confirm">
    <img id="cropPrev" alt="device preview">
    <div style="min-width:0">
      <div id="confTitle" style="font-size:13px"></div>
      <div class="dim" id="confLicense"></div>
      <div style="margin-top:8px"><button class="primary" id="useBtn">Use this photo</button></div>
    </div>
  </div>
</div></div>

<script>
let pickTarget = null, pickLayer = "auto", picked = null, clickedAt = 0, unpublished = 0;
const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));

async function load() {
  const r = await fetch("/api/manifest"); const d = await r.json();
  unpublished = d.entries.filter(e => e.unpublished).length;
  document.getElementById("count").textContent = d.entries.length + " entries" + (unpublished ? " · " + unpublished + " not yet published" : "");
  const btn = document.getElementById("publish");
  btn.disabled = !d.tokenPresent || !unpublished;
  btn.title = d.tokenPresent ? (unpublished ? "" : "Nothing to publish") : d.tokenReason;
  document.getElementById("tokenNote").textContent = d.tokenPresent ? "" : "Publish off: " + d.tokenReason;
  const grid = document.getElementById("entries"); grid.innerHTML = "";
  for (const e of d.entries) {
    const div = document.createElement("div"); div.className = "card" + (e.unpublished ? " unpub" : "");
    const licBadge = e.valid.ok ? '<span class="badge ok">' + esc(e.license) + '</span>'
                                : '<span class="badge err" title="' + esc(e.valid.errors.join("; ")) + '">gate: FAIL</span>';
    div.innerHTML =
      '<img src="/api/current/' + esc(e.target) + '?t=' + Date.now() + '" onerror="this.style.opacity=.2">' +
      '<div class="meta"><b>' + esc(e.target) + '</b> <span class="badge">' + esc(e.kind) + '</span>' +
      '<span class="badge">' + esc(e.layer) + '</span> ' + licBadge +
      (e.unpublished ? '<span class="badge warn">not yet published</span>' : '') +
      '<div class="credit" title="' + esc(e.credit) + '">' + esc(e.credit) + '</div>' +
      '<div class="credit">' + esc(e.author) + '</div>' +
      '<div style="margin-top:8px"><button data-t="' + esc(e.target) + '" data-l="' + esc(e.layer) + '" class="rep">Replace…</button> ' +
      '<button data-t="' + esc(e.target) + '" class="rev" title="Put back the previous photo, and publish">Revert</button> ' +
      '<a class="dim" style="font-size:12px" href="' + esc(e.source) + '" target="_blank">source ↗</a></div></div>';
    grid.appendChild(div);
  }
  grid.querySelectorAll(".rep").forEach(b => b.onclick = () => openPicker(b.dataset.t, b.dataset.l));
  grid.querySelectorAll(".rev").forEach(b => b.onclick = () => revert(b.dataset.t));
}

// The status panel: what the fleet has, from the runs and their verdicts.
async function status() {
  const r = await fetch("/api/publishes"); const d = await r.json();
  const box = document.getElementById("status"), head = box.querySelector(".head"), rows = box.querySelector(".rows");
  if (!d.tokenPresent) { box.className = "bad"; head.textContent = "Publishing is off"; rows.textContent = d.reason || ""; return false; }
  const runs = d.runs || [];
  const top = runs[0];
  let busy = false;
  if (!top) { box.className = ""; head.textContent = "No publish has run yet."; rows.textContent = ""; }
  else {
    const when = new Date(top.state === "LIVE" && top.liveAt ? top.liveAt : top.updatedAt).toLocaleString();
    const took = clickedAt && top.state === "LIVE" && top.liveAt ? " · " + Math.round((Date.parse(top.liveAt) - clickedAt) / 1000) + " s from click" : "";
    if (top.state === "LIVE") { box.className = "live"; head.textContent = "Live on devices at " + when + took; rows.textContent = "Published " + (top.live.length ? top.live.join(", ") : "(no rows changed)") + " · " + top.pointersChecked + " pointers verified in " + top.verifyMs + " ms · devices pick it up within about a minute"; }
    else if (top.state === "QUEUED" || top.state === "RUNNING") { busy = true; box.className = "busy"; head.textContent = (top.state === "QUEUED" ? "Queued" : "Publishing") + "… (" + top.sha.slice(0,7) + ")"; rows.textContent = "Started " + new Date(top.createdAt).toLocaleTimeString(); }
    else if (top.state === "SUPERSEDED") { busy = runs.some(x => x.state === "QUEUED" || x.state === "RUNNING"); box.className = "busy"; head.textContent = "Superseded by a newer publish"; rows.textContent = "Its rows are included in the next run."; }
    else {
      box.className = "bad";
      const label = top.state === "UNTRUSTWORTHY" ? "Not published: the verifier could not see production (instrument blind)" : top.state === "STALE" ? "Refused: a newer photo commit is on main" : "Publish FAILED";
      head.innerHTML = esc(label) + ' <button id="retry">Retry</button> <a class="dim" href="' + esc(top.url) + '" target="_blank">run ↗</a>';
      rows.innerHTML = esc(top.reason) + "<br>Live now: " + esc(top.live.join(", ") || "none") + (top.failed.length ? " · Failed: " + esc(top.failed.join(", ")) : "") + (top.notReached.length ? " · Not reached: " + esc(top.notReached.join(", ")) : "");
      document.getElementById("retry").onclick = retry;
    }
  }
  document.getElementById("history").innerHTML = runs.slice(1, 6).map(x => '<div>' + esc(new Date(x.createdAt).toLocaleString()) + ' · ' + esc(x.sha.slice(0,7)) + ' · ' + esc(x.state) + (x.live.length ? ' · ' + esc(x.live.join(", ")) : '') + '</div>').join("");
  return busy;
}

async function poll() { const busy = await status().catch(() => false); setTimeout(poll, busy ? 4000 : 20000); }

async function publish() {
  const btn = document.getElementById("publish"); btn.disabled = true; btn.textContent = "Publishing…";
  clickedAt = Date.now();
  const r = await fetch("/api/publish", { method: "POST" }); const d = await r.json();
  btn.textContent = "Publish to devices";
  if (!r.ok) alert(d.error || ("HTTP " + r.status));
  else if (d.nothing) alert(d.message);
  await load(); await status();
}
async function revert(target) {
  if (!confirm("Put " + target + " back to its previous photo and publish that?")) return;
  clickedAt = Date.now();
  const r = await fetch("/api/revert", { method: "POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({ target }) });
  const d = await r.json(); if (!r.ok) alert(d.error || ("HTTP " + r.status));
  await load(); await status();
}
async function retry() {
  clickedAt = Date.now();
  const r = await fetch("/api/retry", { method: "POST" }); const d = await r.json();
  if (!r.ok) alert(d.error || ("HTTP " + r.status));
  setTimeout(status, 3000);
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
  if (!r.ok) { box.innerHTML = ""; const s = document.createElement("span"); s.className = "dim"; s.textContent = "Search failed: " + (d.error || ("HTTP " + r.status)); box.appendChild(s); return; }
  box.innerHTML = "";
  for (const c of d.candidates) {
    const ok = pickLayer === "mil-tier" ? c.acceptedIn["mil-tier"] : c.acceptedIn.auto;
    const div = document.createElement("div");
    div.className = "cand" + (ok ? "" : " rejected");
    div.title = ok ? c.license + " — " + c.artist : "REJECTED: " + c.rejectReason;
    const lead = c.wikiLead ? '<span class="badge ok">★ Wikipedia lead</span> ' : "";
    div.innerHTML = '<img loading="lazy" src="' + esc(c.thumb) + '"><div class="t">' + lead + esc(c.title.replace("File:","")) +
      '<br><span class="' + (ok ? "" : "dim") + '">' + esc(c.license) + (ok ? "" : " — " + esc(c.rejectReason)) + '</span></div>';
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
  const img = document.getElementById("cropPrev"); img.src = ""; img.alt = "rendering…";
  const r = await fetch("/api/preview", { method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify({ url: c.full, target: pickTarget }) });
  if (r.ok) img.src = URL.createObjectURL(await r.blob());
  else img.alt = "preview failed: " + ((await r.json().catch(() => ({}))).error || ("HTTP " + r.status));
}

document.getElementById("searchBtn").onclick = doSearch;
document.getElementById("q").addEventListener("keydown", e => { if (e.key === "Enter") doSearch(); });
document.getElementById("publish").onclick = publish;

document.getElementById("useBtn").onclick = async () => {
  if (!picked) return;
  const r = await fetch("/api/replace", { method:"POST", headers:{"Content-Type":"application/json"},
    body: JSON.stringify({ target: pickTarget, layer: pickLayer, title: picked.title }) });
  const d = await r.json();
  // Only a 422 is a verdict on the photo. Everything else is a failure to get it.
  if (r.status === 422) { alert("Rejected by the license gate: " + (d.details ? d.details.join("; ") : d.error)); return; }
  if (r.status === 429) { alert("Not saved. " + d.error); return; }
  if (!r.ok) { alert("Failed: " + (d.error || ("HTTP " + r.status))); return; }
  closePicker(); load();
};

document.getElementById("addBtn").onclick = () => {
  const t = document.getElementById("newTarget").value.trim().toUpperCase();
  if (!t) return;
  openPicker(t, document.getElementById("newLayer").value);
};

load(); poll();
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
    // 429 stays 429 so the page can tell "try again" from "this is broken".
    res.writeHead(err instanceof RateLimited ? 429 : 500, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: String(err instanceof Error ? err.message : err) }));
  }
});

server.listen(PORT, "127.0.0.1", () => {
  // Presence only. The value is never printed, anywhere.
  const t = loadToken();
  console.log(`Photo dashboard: http://127.0.0.1:${PORT}`);
  console.log(`github token: ${t.gh ? "present" : `absent (${t.reason})`}`);
});
