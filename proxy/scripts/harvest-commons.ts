/**
 * harvest-commons.ts -- Wikimedia Commons harvest for the stock-photo library.
 *
 * The playbook (blipscope-military-photo-sourcing.md) wants Commons FIRST
 * because its API carries machine-readable license/author metadata: this script
 * turns a hand-picked "pick sheet" of Commons file titles into validated
 * photos/manifest.json rows plus downloaded source images, ready for
 * `npm run ingest`. It never bypasses the license gate -- rows are built from
 * extmetadata and passed through the SAME validateEntry the ingest uses.
 *
 *   npm run harvest -- --dry-run     # fetch + validate + report; no files touched
 *   npm run harvest                  # download images, merge manifest.json
 *
 * Pick sheet (photos/picksheet.json): [{ target, kind, layer, title, autoPicked? }]
 * where `title` is the exact Commons page title ("File:....jpg").
 *
 * Extra guard for the mil-tier layer (the playbook's credit-line test): a PD
 * row whose artist/credit mentions "courtesy" is rejected -- contractor-donated
 * imagery is copyrighted even on a .mil page.
 */
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { validateEntry, classifyLicense, type ManifestEntry } from "../src/photolicense";

const API = "https://commons.wikimedia.org/w/api.php";
const UA = "BlipscopePhotoHarvest/1.0 (+daniel@valarsystems.com)";
const THUMB_W = 1200; // plenty above the 150x100 device sprite; keeps downloads small

interface Pick {
  target: string;
  kind: "type" | "hex";
  layer: "mil-tier" | "auto";
  title: string; // exact Commons "File:..." title
  autoPicked?: boolean;
}

interface ImageInfo {
  url: string;
  thumburl?: string;
  descriptionurl: string;
  extmetadata?: Record<string, { value: string }>;
}

function stripHtml(s: string): string {
  return s
    .replace(/<[^>]*>/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

async function fetchInfo(title: string): Promise<ImageInfo | null> {
  const u = new URL(API);
  u.searchParams.set("action", "query");
  u.searchParams.set("titles", title);
  u.searchParams.set("prop", "imageinfo");
  u.searchParams.set("iiprop", "extmetadata|url");
  u.searchParams.set("iiurlwidth", String(THUMB_W));
  u.searchParams.set("format", "json");
  const res = await fetch(u, { headers: { "User-Agent": UA } });
  if (!res.ok) return null;
  const body = (await res.json()) as {
    query?: { pages?: Record<string, { imageinfo?: ImageInfo[] }> };
  };
  const pages = body.query?.pages ?? {};
  for (const p of Object.values(pages)) {
    if (p.imageinfo?.[0]) return p.imageinfo[0];
  }
  return null;
}

async function main(): Promise<void> {
  const dryRun = process.argv.includes("--dry-run");
  const photosDir = "photos";
  const picks = JSON.parse(readFileSync(join(photosDir, "picksheet.json"), "utf8")) as Pick[];
  const manifestPath = join(photosDir, "manifest.json");
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8")) as ManifestEntry[];

  let failed = 0;
  for (const pick of picks) {
    const info = await fetchInfo(pick.title);
    if (!info) {
      failed++;
      console.error(`FAIL ${pick.target}: Commons has no imageinfo for ${JSON.stringify(pick.title)}`);
      continue;
    }
    const md = info.extmetadata ?? {};
    const licenseRaw = stripHtml(md.LicenseShortName?.value ?? "");
    const artist = stripHtml(md.Artist?.value ?? "");
    const objectName = stripHtml(md.ObjectName?.value ?? pick.title.replace(/^File:/, ""));

    const entry: ManifestEntry = {
      target: pick.target,
      kind: pick.kind,
      source: info.descriptionurl,
      author: artist,
      credit: objectName,
      license: licenseRaw,
      layer: pick.layer,
      autoPicked: pick.autoPicked ?? false,
      file: `src/${pick.target.toLowerCase()}.jpg`,
    };
    const cls = classifyLicense(licenseRaw);
    if (cls === "CC-BY-SA" && pick.layer === "auto") {
      entry.changesNoted = "resized and crop-fitted for device display";
    }

    const gate = validateEntry(entry);
    // Credit-line test (mil-tier PD): "courtesy of <contractor>" imagery is
    // contractor copyright even when it rides a PD page -- refuse it.
    const creditLine = `${artist} ${stripHtml(md.Credit?.value ?? "")}`.toLowerCase();
    if (pick.layer === "mil-tier" && creditLine.includes("courtesy")) {
      gate.ok = false;
      gate.errors.push(`credit-line test failed: ${JSON.stringify(artist)}`);
    }

    if (!gate.ok) {
      failed++;
      console.error(`REJECT ${pick.target} [${licenseRaw}] (${artist})`);
      for (const e of gate.errors) console.error(`   - ${e}`);
      continue;
    }

    console.log(`OK ${pick.target} [${licenseRaw}] by ${artist} -- ${pick.title}`);
    if (dryRun) continue;

    const dl = info.thumburl ?? info.url;
    const imgRes = await fetch(dl, { headers: { "User-Agent": UA } });
    if (!imgRes.ok) {
      failed++;
      console.error(`FAIL ${pick.target}: image download ${imgRes.status} from ${dl}`);
      continue;
    }
    writeFileSync(join(photosDir, entry.file!), Buffer.from(await imgRes.arrayBuffer()));

    // Merge: replace an existing row for the same kind+target, else append.
    const idx = manifest.findIndex((m) => m.kind === entry.kind && m.target === entry.target);
    if (idx >= 0) manifest[idx] = entry;
    else manifest.push(entry);
  }

  if (!dryRun) {
    writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
    console.log(`merged ${picks.length - failed} rows into ${manifestPath}`);
  }
  if (failed > 0) {
    console.error(`\n${failed}/${picks.length} picks failed; fix the sheet and re-run.`);
    process.exit(1);
  }
}

main().catch((err) => {
  console.error(String(err instanceof Error ? err.stack : err));
  process.exit(1);
});
