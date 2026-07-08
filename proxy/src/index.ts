import type { Env } from "./types";
import { handleBlips } from "./blips";
import { handleConfig } from "./config";
import { handleEnrich } from "./enrich";
import { record, type RequestMetric } from "./metrics";
import { limitByIp, limitByKey } from "./ratelimit";
import { feedHealth } from "./upstreams/chain";
import { errorResponse, jsonResponse } from "./util";

// Returns the index of the presented key in BLIP_KEYS (comma-separated so a
// rotation can accept old+new simultaneously), or null when rejected. The
// index -- never the key -- feeds the per-key rate-limit bucket and logs.
function authenticate(env: Env, request: Request): number | null {
  const provided = request.headers.get("X-Blip-Key") ?? "";
  if (!provided) return null;
  const keys = (env.BLIP_KEYS ?? "")
    .split(",")
    .map((k) => k.trim())
    .filter((k) => k.length > 0);
  const idx = keys.indexOf(provided);
  return idx >= 0 ? idx : null;
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
  if (request.method !== "GET") return errorResponse(405, "method_not_allowed");
  if (url.pathname === "/healthz") return handleHealth(env);
  if (!url.pathname.startsWith("/v1/")) return errorResponse(404, "not_found");

  // Per-IP limit first (throttles key-guessing too), then auth, then per-key.
  const ipLimited = await limitByIp(env, request);
  if (ipLimited) return ipLimited;
  const keyIndex = authenticate(env, request);
  if (keyIndex === null) return errorResponse(401, "unauthorized");
  const keyLimited = await limitByKey(env, keyIndex);
  if (keyLimited) return keyLimited;

  if (url.pathname === "/v1/blips") return handleBlips(request, env, ctx, meta);
  if (url.pathname === "/v1/config") return handleConfig(request, env);
  const enrichMatch = url.pathname.match(/^\/v1\/enrich\/([^/]+)$/);
  if (enrichMatch) return handleEnrich(request, env, ctx, enrichMatch[1] as string, meta);
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
