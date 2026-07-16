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

// Resolve the pointer for an aircraft: per-hex first (an override IS that
// airframe -> uncaptioned), then the generic type shot (captioned
// "representative photo" on the card). Null when the library has neither.
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
