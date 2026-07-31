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
};

// Resolve the pointer for an aircraft: per-hex first (an override IS that
// airframe -> uncaptioned), then the generic type shot (captioned
// "representative photo" on the card), then the base type a variant aliases to.
// Null when the library has none of them.
export async function resolvePhoto(
  env: Env,
  hex: string,
  type: string,
): Promise<PhotoRef | null> {
  const hexPtr = await env.ENRICH_KV.get(pointerKey("hex", hex));
  if (hexPtr && isValidPhotoKey(hexPtr)) return { key: hexPtr, kind: "hex" };

  const t = type.trim().toUpperCase();
  if (t) {
    const typePtr = await env.ENRICH_KV.get(pointerKey("type", t));
    if (typePtr && isValidPhotoKey(typePtr)) return { key: typePtr, kind: "type" };

    // Exact miss: fall back to the base type. Still `kind: "type"` -- the card
    // captions it as a representative shot either way, which is what it is.
    const base = TYPE_PHOTO_ALIAS[t];
    if (base) {
      const basePtr = await env.ENRICH_KV.get(pointerKey("type", base));
      if (basePtr && isValidPhotoKey(basePtr)) return { key: basePtr, kind: "type" };
    }
  }
  return null;
}

// GET /v1/photo/<key>: serve the immutable, content-addressed JPEG blob. Keys
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
