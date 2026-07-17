import type { Env } from "./types";
import { handleAirports } from "./airports";
import { handleBlips } from "./blips";
import { handleConfig } from "./config";
import { handleEnrich } from "./enrich";
import {
  handleLeaderboardJson,
  handleLeaderboardPage,
  handleLeaderboardSubmit,
  handleProfile,
} from "./leaderboard";
import { record, recordOtaMem, type RequestMetric } from "./metrics";
import { handleCredits, handlePhoto } from "./photos";
import { verifyDeviceKey } from "./deviceauth";
import { limitByIp, limitByKey } from "./ratelimit";
import { feedHealth } from "./upstreams/chain";
import { errorResponse, jsonResponse } from "./util";

// Authenticate a request. Tries the per-device key path first (only active when
// DEVICE_KEY_SECRET is set and the request carries X-Blip-Device), then falls
// back to the shared BLIP_KEYS list -- so the live fleet, which sends only a
// shared key, is entirely unaffected. Returns a rate-limit bucket id and whether
// the caller is device-authed (trustworthy enough for the leaderboard's verified
// tier), or null when rejected. The bucket is never the key itself.
async function authenticate(
  env: Env,
  request: Request,
): Promise<{ bucket: string; deviceAuthed: boolean } | null> {
  const provided = request.headers.get("X-Blip-Key") ?? "";
  if (!provided) return null;

  // Per-device path: HMAC-derived key keyed to the X-Blip-Device id.
  const deviceId = (request.headers.get("X-Blip-Device") ?? "").trim().toLowerCase();
  if (deviceId && (await verifyDeviceKey(env, deviceId, provided))) {
    return { bucket: `dev:${deviceId}`, deviceAuthed: true };
  }

  // Shared-key path (unchanged): the index -- never the key -- is the bucket.
  const keys = (env.BLIP_KEYS ?? "")
    .split(",")
    .map((k) => k.trim())
    .filter((k) => k.length > 0);
  const idx = keys.indexOf(provided);
  return idx >= 0 ? { bucket: `key:${idx}`, deviceAuthed: false } : null;
}

function handleHealth(env: Env): Response {
  return jsonResponse({ ok: true, upstreams: feedHealth(env) });
}

async function route(
  request: Request,
  env: Env,
  ctx: ExecutionContext,
  url: URL,
  meta: RequestMetric,
): Promise<Response> {
  // POST is accepted ONLY for the leaderboard submit (authed below); every
  // other route is GET.
  const isLeaderboardSubmit = url.pathname === "/v1/leaderboard" && request.method === "POST";
  if (request.method !== "GET" && !isLeaderboardSubmit) return errorResponse(405, "method_not_allowed");
  if (url.pathname === "/healthz") return handleHealth(env);
  // Public photo-attribution page (a browser follows the config page's link; no
  // device key). Rendered from the manifest the ingest script publishes to KV.
  if (url.pathname === "/credits") return handleCredits(env);
  // Public leaderboard: HTML board, its JSON, and per-device profiles. No key,
  // same as /credits (a browser follows the config page's link).
  if (url.pathname === "/leaderboard") return handleLeaderboardPage(env);
  if (url.pathname === "/leaderboard.json") return handleLeaderboardJson(request, env);
  const profileMatch = url.pathname.match(/^\/leaderboard\/([0-9a-f]{8,32})$/);
  if (profileMatch) return handleProfile(env, profileMatch[1] as string);
  if (!url.pathname.startsWith("/v1/")) return errorResponse(404, "not_found");

  // Per-IP limit first (throttles key-guessing too), then auth, then per-key.
  const ipLimited = await limitByIp(env, request);
  if (ipLimited) return ipLimited;
  const auth = await authenticate(env, request);
  if (auth === null) return errorResponse(401, "unauthorized");
  const keyLimited = await limitByKey(env, auth.bucket);
  if (keyLimited) return keyLimited;

  // A device's one-shot OTA memory report, if this check-in carries one. Recorded
  // only past auth + rate limiting, so an anonymous caller can never spend our
  // Analytics Engine budget, and it cannot affect the response the device came
  // for: whatever this does, the request below is served identically.
  recordOtaMem(env, request.headers.get("X-Blip-OTA-Mem"), meta.model);

  if (url.pathname === "/v1/blips") return handleBlips(request, env, ctx, meta);
  if (url.pathname === "/v1/config") return handleConfig(request, env);
  if (url.pathname === "/v1/airports") return handleAirports(request, env);
  if (isLeaderboardSubmit) return handleLeaderboardSubmit(request, env);
  const enrichMatch = url.pathname.match(/^\/v1\/enrich\/([^/]+)$/);
  if (enrichMatch) return handleEnrich(request, env, ctx, enrichMatch[1] as string, meta);
  const photoMatch = url.pathname.match(/^\/v1\/photo\/([^/]+)$/);
  if (photoMatch) return handlePhoto(env, photoMatch[1] as string);
  return errorResponse(404, "not_found");
}

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    const started = Date.now();
    const url = new URL(request.url);
    const meta: RequestMetric = {
      ep: url.pathname,
      status: 0,
      ms: 0,
      model: request.headers.get("X-Blip-Model") ?? "",
      colo: (request.cf?.colo as string | undefined) ?? "",
    };
    let response: Response;
    try {
      response = await route(request, env, ctx, url, meta);
    } catch (err) {
      console.log(JSON.stringify({ evt: "error", ep: url.pathname, err: String(err) }));
      response = errorResponse(500, "internal");
    }
    meta.status = response.status;
    meta.ms = Date.now() - started;
    record(env, meta);
    return response;
  },
} satisfies ExportedHandler<Env>;
