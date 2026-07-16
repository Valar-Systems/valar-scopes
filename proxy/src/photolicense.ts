// Photo library: the pure, side-effect-free half of the stock-photo pipeline --
// license gate, manifest validation, KV key derivation, and credits-page render.
// Kept apart from photos.ts (which touches KV) so the ingest script (Node) and
// the vitest-pool-workers tests can exercise the rules without a KV binding.
//
// See proxy/README.md "Stock photo library for cloud mode" and
// blipscope-military-photo-sourcing.md for the sourcing playbook these encode.

// A manifest row. The ingest script refuses any upload missing a required field
// (everything but the ingest-filled `blobKey`/`changesNoted`).
export interface ManifestEntry {
  target: string; // ICAO type code (e.g. "A320") or 6-hex ICAO address for a per-airframe override
  kind: "type" | "hex";
  source: string; // source URL (Commons file page, DVIDS, .mil, ...)
  author: string; // photographer / issuing body
  credit: string; // the human credit line rendered on the credits page
  license: string; // raw license token from the source (classified below)
  layer: "mil-tier" | "auto"; // hand-curated military tier vs auto-harvested civil long tail
  autoPicked: boolean; // manifest-only curation state (drives re-harvest / spot-check policy)
  file?: string; // local source image the ingest script resizes (input only)
  changesNoted?: string; // required for CC-BY-SA: how the image was modified ("resized for device display")
  blobKey?: string; // ingest-filled: the content-addressed KV key the pointer points at
}

// License classification, normalized from the raw token. NC/ND are rejected in
// both layers; SA is rejected in mil-tier and accepted in auto (with a
// credits-page obligation); everything unrecognized is rejected.
export type LicenseClass =
  | "PD-USGov"
  | "CC-BY"
  | "CC-BY-SA"
  | "OGL"
  | "own"
  | "reject-nc"
  | "reject-nd"
  | "unknown";

// Map a raw license string to its class. Tolerant of version suffixes and case
// ("CC-BY-4.0" -> CC-BY, "OGL-3.0" -> OGL, "PD-USGov-Military" -> PD-USGov), but
// NC/ND anywhere in a CC token is a hard reject regardless of the rest.
export function classifyLicense(raw: string): LicenseClass {
  const s = raw.trim().toUpperCase().replace(/\s+/g, "-");
  if (!s) return "unknown";

  // Creative Commons: reject the non-free clauses first so "CC-BY-NC-SA" can't
  // slip through the BY/SA arms below.
  if (s.startsWith("CC")) {
    if (s.includes("NC")) return "reject-nc";
    if (s.includes("ND")) return "reject-nd";
    if (s.includes("SA")) return "CC-BY-SA"; // CC-BY-SA (+ version)
    if (s.includes("BY")) return "CC-BY"; // CC-BY (+ version)
    if (s.startsWith("CC0")) return "PD-USGov"; // CC0 is a public-domain dedication; treat as PD-clean
    return "unknown";
  }

  // Public domain, incl. the US-government work tags Commons uses.
  if (s.startsWith("PD") || s === "PUBLIC-DOMAIN" || s.startsWith("PD-USGOV")) return "PD-USGov";
  // UK Open Government Licence.
  if (s.startsWith("OGL")) return "OGL";
  // Owner's own photograph, released for this use.
  if (s === "OWN" || s === "SELF") return "own";

  return "unknown";
}

// Which classes each layer accepts. mil-tier stays maximally clean (no SA);
// auto additionally accepts SA (with the credits obligation enforced below).
const MIL_ALLOWED: ReadonlySet<LicenseClass> = new Set(["PD-USGov", "CC-BY", "OGL", "own"]);
const AUTO_ALLOWED: ReadonlySet<LicenseClass> = new Set([
  "PD-USGov",
  "CC-BY",
  "CC-BY-SA",
  "OGL",
  "own",
]);

const TYPE_RE = /^[A-Z0-9]{2,4}$/;
const HEX_RE = /^~?[0-9a-f]{6}$/;

export interface ValidationResult {
  ok: boolean;
  errors: string[];
  license?: LicenseClass;
}

// Validate one manifest row against the per-layer license gate and the shape
// rules. Pure: no KV, no filesystem. The ingest script calls this before any
// upload and aborts the whole run if any row fails.
export function validateEntry(entry: ManifestEntry): ValidationResult {
  const errors: string[] = [];

  const required: (keyof ManifestEntry)[] = [
    "target",
    "kind",
    "source",
    "author",
    "credit",
    "license",
    "layer",
  ];
  for (const f of required) {
    const v = entry[f];
    if (v === undefined || v === null || (typeof v === "string" && v.trim() === "")) {
      errors.push(`missing required field: ${f}`);
    }
  }
  if (typeof entry.autoPicked !== "boolean") errors.push("autoPicked must be a boolean");

  if (entry.kind !== "type" && entry.kind !== "hex") {
    errors.push(`kind must be "type" or "hex" (got ${JSON.stringify(entry.kind)})`);
  }
  if (entry.layer !== "mil-tier" && entry.layer !== "auto") {
    errors.push(`layer must be "mil-tier" or "auto" (got ${JSON.stringify(entry.layer)})`);
  }

  // Target must match its kind (type codes upper-alnum; hex is 6 lowercase hex).
  if (entry.kind === "type" && !TYPE_RE.test(entry.target ?? "")) {
    errors.push(`type target must be 2-4 upper-alnum chars (got ${JSON.stringify(entry.target)})`);
  }
  if (entry.kind === "hex" && !HEX_RE.test(entry.target ?? "")) {
    errors.push(`hex target must be 6 lowercase hex digits (got ${JSON.stringify(entry.target)})`);
  }

  // The license gate. Only run it once the layer is known and valid.
  const cls = classifyLicense(entry.license ?? "");
  if (entry.layer === "mil-tier" || entry.layer === "auto") {
    const allowed = entry.layer === "mil-tier" ? MIL_ALLOWED : AUTO_ALLOWED;
    if (cls === "reject-nc") errors.push("license rejected: NonCommercial (NC) is never accepted");
    else if (cls === "reject-nd") errors.push("license rejected: NoDerivatives (ND) is never accepted");
    else if (cls === "unknown") errors.push(`license not recognized: ${JSON.stringify(entry.license)}`);
    else if (cls === "CC-BY-SA" && entry.layer === "mil-tier")
      errors.push("license rejected: CC-BY-SA is not accepted in the mil-tier layer");
    else if (!allowed.has(cls)) errors.push(`license ${cls} not accepted in layer ${entry.layer}`);
    // SA carries a credits-page obligation: the changes-noted line is mandatory.
    if (cls === "CC-BY-SA" && entry.layer === "auto") {
      const cn = (entry.changesNoted ?? "").trim();
      if (!cn) errors.push("CC-BY-SA entries require a non-empty changesNoted line");
    }
  }

  return { ok: errors.length === 0, errors, license: cls };
}

// True when the JPEG's start-of-frame marker is baseline (SOF0) or extended
// sequential (SOF1); false for progressive (SOF2), which the on-device decoder
// (TJpgDec via LovyanGFX drawJpg) cannot decode. Shared by the ingest script's
// pre-upload assertion and the photo dashboard's candidate preview.
export function isBaselineJpeg(buf: Uint8Array): boolean {
  for (let i = 0; i < buf.length - 1; i++) {
    if (buf[i] !== 0xff) continue;
    const m = buf[i + 1];
    if (m === 0xc0 || m === 0xc1) return true;
    if (m === 0xc2) return false;
    if (m === 0xda) break; // reached scan data without a SOF verdict
  }
  return false;
}

// Content-addressed KV key for a blob: photo:<TYPE|hex>-<hash8>. The hash makes
// re-uploads land on a new key (pointer flip) instead of mutating a blob in place.
export async function deriveBlobKey(target: string, bytes: ArrayBuffer | Uint8Array): Promise<string> {
  const buf = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const digest = await crypto.subtle.digest("SHA-256", buf);
  const hash8 = [...new Uint8Array(digest).slice(0, 4)]
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
  return `photo:${target}-${hash8}`;
}

// Pointer keys: one small entry per hex/type records the current blob key. The
// enrich join resolves per-hex first (an override IS that airframe), then type.
export function pointerKey(kind: "type" | "hex", target: string): string {
  return kind === "hex" ? `pptr:h:${target}` : `pptr:t:${target}`;
}

// The public manifest KV key the credits page renders from (public credit rows
// only -- no local file paths).
export const MANIFEST_KEY = "photo:manifest";

function esc(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// License display + link for the credits page. PD/own need only a courtesy
// credit; CC-BY/SA and OGL need the license link (and SA a changes-noted line,
// carried per-entry).
const LICENSE_META: Record<string, { label: string; url?: string }> = {
  "PD-USGov": { label: "Public domain" }, // covers PD-USGov, CC0, and self-released PD (civil long tail)
  own: { label: "Used with permission" },
  "CC-BY": { label: "CC BY 4.0", url: "https://creativecommons.org/licenses/by/4.0/" },
  "CC-BY-SA": { label: "CC BY-SA 4.0", url: "https://creativecommons.org/licenses/by-sa/4.0/" },
  OGL: {
    label: "Open Government Licence v3.0",
    url: "https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/",
  },
};

// Render the attribution page from the manifest. Satisfies CC-BY / CC-BY-SA /
// OGL attribution in one place and courtesy-credits the PD shots. Pure string
// build so it's identical whether the Worker or the ingest script produces it.
export function renderCreditsHtml(entries: ManifestEntry[]): string {
  const rows = entries
    .slice()
    .sort((a, b) => a.target.localeCompare(b.target))
    .map((e) => {
      const cls = classifyLicense(e.license);
      const meta = LICENSE_META[cls] ?? { label: e.license };
      const lic = meta.url
        ? `<a href="${esc(meta.url)}" rel="noopener">${esc(meta.label)}</a>`
        : esc(meta.label);
      const changes =
        cls === "CC-BY-SA" && e.changesNoted ? ` &mdash; ${esc(e.changesNoted)}` : "";
      const src = e.source
        ? ` &middot; <a href="${esc(e.source)}" rel="noopener">source</a>`
        : "";
      return (
        `<li><strong>${esc(e.target)}</strong> &mdash; ${esc(e.credit)} ` +
        `(${esc(e.author)}) &middot; ${lic}${changes}${src}</li>`
      );
    })
    .join("\n");

  return `<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blipscope photo credits</title>
<style>
  body{font:15px/1.5 system-ui,sans-serif;max-width:52rem;margin:2rem auto;padding:0 1rem;color:#111;background:#fff}
  h1{font-size:1.4rem} ul{padding-left:1.1rem} li{margin:.35rem 0}
  a{color:#0a58ca} .note{color:#555;font-size:.9rem}
  @media(prefers-color-scheme:dark){body{background:#111;color:#eee}a{color:#6ea8fe}.note{color:#aaa}}
</style></head><body>
<h1>Blipscope photo credits</h1>
<p class="note">Aircraft images shown on Blipscope devices in cloud mode. Public-domain
images are credited as a courtesy; CC BY, CC BY-SA, and OGL images are attributed as
their licenses require. Images are resized for the device display.</p>
<ul>
${rows}
</ul>
</body></html>`;
}
