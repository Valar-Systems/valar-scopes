import type { Env } from "./types";
import { MANIFEST_KEY, pointerKey, renderCreditsHtml, type ManifestEntry } from "./photolicense";

// Stock photo library -- the KV-touching half (serving + pointer resolution).
// Pure rules (license gate, key derivation, credits render) live in
// photolicense.ts. See proxy/README.md "Stock photo library for cloud mode".

// Blob keys are content-addressed: photo:<TYPE|hex>-<hash8>. Serving validates
// against this shape before touching KV so a crafted `key` can't read arbitrary
// namespaces (the pointer/manifest keys use different prefixes).
const BLOB_KEY_RE = /^photo:[~0-9A-Za-z]{2,8}-[0-9a-f]{8}$/;

export function isValidPhotoKey(key: string): boolean {
  return BLOB_KEY_RE.test(key);
}

export interface PhotoRef {
  key: string; // the content-addressed blob key
  kind: "hex" | "type"; // per-airframe override vs generic type stock (drives the "representative photo" label)
}

// Variant type code -> the base type whose photo represents it well enough.
//
// The pointer lookup is EXACT-match on the designator, so a variant with its own
// code gets no photo even when we already hold a perfectly good shot of the base
// aircraft. Measured 2026-07-30 against live US traffic: 10 of 14 common variants
// were in exactly that state, and the same airframe could get a photo under one
// designator and not another -- an SR22T shows a photo as SR2T but not as S22T.
// That is an indexing artifact, not a content gap.
//
// The bar for an alias is that the two are visually the same aeroplane to someone
// glancing at a 1.3-inch round display: turbo/retract/engine variants qualify,
// different airframes do not. A variant with its OWN picksheet entry always wins,
// because this map is only consulted after the exact lookup misses.
const TYPE_PHOTO_ALIAS: Record<string, string> = {
  S22T: "SR22", SR2T: "SR22",                       // Cirrus SR22 turbo
  C82R: "C182", C82S: "C182", C82T: "C182",         // Skylane RG / turbo variants
  C72R: "C172",                                     // Cutlass RG
  T206: "C206",                                     // Turbo Stationair
  E45X: "E145",                                     // ERJ-145XR
  BE9L: "BE20", BE30: "B350",                       // King Air 90 / Super King Air 300
  TBM7: "TBM8",                                     // TBM 700/850 share a hull
  P28R: "P28A", P32R: "PA32",                       // Arrow / Saratoga-Lance
  C185: "C180",                                     // Skywagon -- see below
};

// WHY C185 QUALIFIES AND B505/PA23 DO NOT -- the rule is hulls, not neighbours.
// The rule itself lives in blipscope-military-photo-sourcing.md ("Aliasing a
// type: justify it by the HULL, never by the photograph"); this is its instance.
//
// The 185 is a structurally strengthened 180 with a bigger engine and a taller
// fin, on the same fuselage: the TBM7/TBM8 relationship above, not the "another
// Piper light twin" category error that got PA23 refused. Aliasing on
// resemblance rather than shared airframe is how the table stops meaning
// anything, so the distinction is load-bearing, not pedantry.
//
// EYEBALLED AT 240px IN THE DISC BEFORE IT LANDED, against the published square
// (photo:C180-2e567c3e), not the source file -- read the artifact. It is a
// three-quarter-rear shot of a red-and-white high-wing taildragger on pavement,
// wings unforeshortened, in colour and on wheels.
//
// Recorded honestly: this is a "you cannot tell" pass, not a "it looks right"
// one. The tail is the feature that most distinguishes a 185, and it is angled
// away and small in frame -- so the differentiator is HIDDEN rather than
// matching. That still passes, because the claim the card makes is "a
// representative Skywagon" and the hull genuinely is shared; but if this photo
// is ever re-picked to a tail-on or side view, RE-CHECK THIS ALIAS. A pick that
// makes the C180 more identifiable makes it a worse stand-in for the 185.

// ---------------------------------------------------------------------------
// Which photo variant a device may be sent.
//
// THE HAZARD THIS EXISTS TO PREVENT. Firmware up to FW 6 draws the photo into a
// fixed 150x100 slot, and its drawJpg call site passes no scale -- maxWidth and
// maxHeight CLIP rather than shrink. Send one of those devices a 240x240 blob
// and it renders the top-left corner of the image, on every card, silently, with
// a 200 on the wire and nothing in any log. The gate is the only thing standing
// between the square library and every device already in the field.
//
// So the default is legacy, and a device gets a square only by positively
// proving it can draw one: a parseable FW at or past the version that introduced
// the full-bleed card, AND a model whose panel size we actually publish. Anything
// unrecognised -- a blank header, a new SKU, a bad parse -- falls back to the
// rectangle, which every firmware that has ever shipped can draw.
export const FULLBLEED_MIN_FW = 7;

// Panel size per model slug (variant::SLUG -> variant::SCREEN_SIZE). Only sizes
// the ingest actually emits belong here; a model absent from this map gets the
// legacy rectangle rather than a guess.
const MODEL_PANEL: Record<string, number> = {
  "s3-128": 240,
  "s3-146": 412,
  "s3-21": 480,
};

/**
 * The square panel size this request may be served, or null for the legacy
 * rectangle. `fw` and `model` come straight off X-Blip-FW / X-Blip-Model.
 */
export function squareSizeFor(fw: string | null, model: string | null): number | null {
  const v = Number.parseInt((fw ?? "").trim(), 10);
  if (!Number.isFinite(v) || v < FULLBLEED_MIN_FW) return null;
  return MODEL_PANEL[(model ?? "").trim()] ?? null;
}

// Resolve the pointer for an aircraft: per-hex first (an override IS that
// airframe -> uncaptioned), then the generic type shot (captioned
// "representative photo" on the card), then the base type a variant aliases to.
// Null when the library has none of them.
export async function resolvePhoto(
  env: Env,
  hex: string,
  type: string,
  square: number | null = null,
): Promise<PhotoRef | null> {
  // A full-bleed device gets a square or it gets NOTHING -- never the rectangle.
  //
  // Falling back to the legacy 150x100 looks like the safe, generous choice and
  // is not: the full-bleed card has no slot to put it in, so a 150x100 image in a
  // 240 disc is wrong wherever it lands (top-left leaves an L of black, centred
  // leaves a border the ring cuts through). Returning null instead puts the card
  // into "No photo available", which is a DESIGNED state with a silhouette --
  // honest, and visibly a gap rather than visibly a bug. Observed on the bench
  // 2026-08-10: a legacy blob reaching a full-bleed card renders as a fragment
  // with hasPhoto=1 and no error anywhere.
  //
  // Old firmware is unaffected -- square is null for it, and it reads only the
  // legacy pointer, exactly as before.
  const read = async (kind: "type" | "hex", target: string): Promise<string | null> => {
    const k = square !== null ? pointerKey(kind, target, square) : pointerKey(kind, target);
    const v = await env.ENRICH_KV.get(k);
    return v && isValidPhotoKey(v) ? v : null;
  };

  const hexPtr = await read("hex", hex);
  if (hexPtr) return { key: hexPtr, kind: "hex" };

  const t = type.trim().toUpperCase();
  if (t) {
    const typePtr = await read("type", t);
    if (typePtr) return { key: typePtr, kind: "type" };

    // Exact miss: fall back to the base type. Still `kind: "type"` -- the card
    // captions it as a representative shot either way, which is what it is.
    const base = TYPE_PHOTO_ALIAS[t];
    if (base) {
      const basePtr = await read("type", base);
      if (basePtr) return { key: basePtr, kind: "type" };
    }
  }
  return null;
}

// GET /api/v1/blipscope/photo/<key>: serve the immutable, content-addressed JPEG blob. Keys
// are content-addressed, so the long immutable cache is safe (a re-upload lands
// on a new key + pointer flip, never a mutated blob). Auth + rate limiting are
// applied by the router before this runs.
export async function handlePhoto(env: Env, key: string): Promise<Response> {
  if (!isValidPhotoKey(key)) {
    return new Response(JSON.stringify({ v: 1, error: "bad_photo_key" }), {
      status: 400,
      headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
    });
  }

  const body = await env.ENRICH_KV.get(key, "arrayBuffer");
  if (body === null) {
    return new Response(JSON.stringify({ v: 1, error: "not_found" }), {
      status: 404,
      headers: { "Content-Type": "application/json", "Cache-Control": "no-store" },
    });
  }

  return new Response(body, {
    status: 200,
    headers: {
      // Baseline JPEG, pre-sized to the device sprite at ingest (EXIF stripped).
      "Content-Type": "image/jpeg",
      "Content-Length": String(body.byteLength),
      "Cache-Control": "public, max-age=31536000, immutable",
    },
  });
}

// GET /credits: the public attribution page, rendered from the manifest KV
// entry the ingest script publishes. No auth (a browser follows the config
// page's link); returns an empty-but-valid page when nothing's been ingested.
export async function handleCredits(env: Env): Promise<Response> {
  const entries = (await env.ENRICH_KV.get<ManifestEntry[]>(MANIFEST_KEY, "json")) ?? [];
  const html = renderCreditsHtml(entries);
  const bytes = new TextEncoder().encode(html);
  return new Response(bytes, {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Length": String(bytes.byteLength),
      "Cache-Control": "public, max-age=3600",
    },
  });
}
