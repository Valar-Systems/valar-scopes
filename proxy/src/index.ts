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
import { FONTS } from "./fonts.generated";
import { indexHtml, supportHtml } from "./pages.generated";
import { record, recordOtaMem, setDeviceAttribution, type RequestMetric } from "./metrics";
import { handleMissileer, isMissileerPath } from "./missileer";
import { handleCredits, handlePhoto } from "./photos";
import { verifyDeviceKey } from "./deviceauth";
import { limitByIp, limitByKey } from "./ratelimit";
import { feedHealth } from "./upstreams/chain";
import { isRevoked } from "./revocation";
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

  const deviceId = (request.headers.get("X-Blip-Device") ?? "").trim().toLowerCase();

  // Revocation is checked BEFORE any key path, so a revoked device cannot slip
  // through by presenting a still-valid SHARED key -- revocation is by identity,
  // not by which credential happened to be offered. isRevoked fails OPEN on a KV
  // error (see revocation.ts): a storage blip must never take the fleet down to
  // enforce a list that is almost always empty.
  if (deviceId && (await isRevoked(env, deviceId))) return null;

  // Per-device path: HMAC-derived key keyed to the X-Blip-Device id.
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

// Substituted at bundle time by scripts/deploy.sh (--define). Defaults to "dev"
// via the `define` block in wrangler.toml, which is what dev and the test runner
// see. Declared rather than imported because esbuild replaces the identifier
// textually -- there is no module to import from.
declare const BUILD_COMMIT: string;

function handleHealth(env: Env): Response {
  // The commit is here because "what is production actually running?" was, for a
  // long time, only answerable by correlating deploy timestamps against git log
  // and hoping the working tree had been clean. It is a public build identifier,
  // not a secret: the repo it names is public, and being able to ask a running
  // Worker what it is beats inferring it after something has already gone wrong.
  return jsonResponse({ ok: true, commit: BUILD_COMMIT, upstreams: feedHealth(env) });
}

// ---- edition-namespaced URLs (see docs/web-url-convention.md) ---------------
//
// Pages live at /{edition}/{surface} and APIs at /api/v1/{edition}/..., so the
// domain can multiplex one Worker per edition by route later: /blipscope/* here,
// /missileer/* at the eam Worker. Blipscope's old unprefixed paths stay working,
// but the two surfaces get OPPOSITE treatment, and the difference is the whole
// point:
//
//   PAGES  -> 301. Browsers follow redirects and updating a bookmark is free.
//   APIs   -> INTERNAL ALIAS, never a redirect. Deployed ESP32 firmware is not
//             guaranteed to follow a 301, and certainly not on a POST, where
//             following one at all would require re-sending the body. A redirect
//             here would brick the leaderboard submit for the entire fleet the
//             moment it deployed, and the failure would look like a server
//             outage rather than a routing change.
//
// /v1/* is now Blipscope-legacy alias space and must never be assigned to
// another edition, because a device somewhere will keep calling it until every
// unit has taken an OTA.
const EDITION = "blipscope";
const API_PREFIX = `/api/v1/${EDITION}/`;
const API_LEGACY_PREFIX = "/v1/";
const PAGE_PREFIX = `/${EDITION}`;

// Reduce either API prefix to the bare endpoint ("blips", "enrich/<hex>"), so
// dispatch below happens ONCE against the suffix. Structural rather than a
// duplicated route table: an alias cannot drift from its new path, because there
// is only one of each handler call and both prefixes reach it the same way.
// Returns null for anything that is not an API request at all.
function apiEndpoint(pathname: string): { suffix: string; legacy: boolean } | null {
  if (pathname.startsWith(API_PREFIX)) return { suffix: pathname.slice(API_PREFIX.length), legacy: false };
  if (pathname.startsWith(API_LEGACY_PREFIX)) return { suffix: pathname.slice(API_LEGACY_PREFIX.length), legacy: true };
  return null;
}

// A page with no data in it: the markup IS the response. Encoded once per call
// rather than held as bytes because these are served rarely (a human arriving,
// not a fleet polling), and max-age=300 matches the leaderboard page -- markup
// changes on deploys, so five minutes is the cost of being wrong.
function staticPage(html: string): Response {
  const bytes = new TextEncoder().encode(html);
  return new Response(bytes, {
    status: 200,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Length": String(bytes.byteLength),
      "Cache-Control": "public, max-age=300",
    },
  });
}

// 301, not 302: the move is permanent, and a permanent redirect is what gets
// bookmarks and search results rewritten instead of re-followed forever.
function movedTo(url: URL, newPath: string): Response {
  const target = new URL(url.toString());
  target.pathname = newPath;
  return new Response(null, { status: 301, headers: { Location: target.toString() } });
}

async function route(
  request: Request,
  env: Env,
  ctx: ExecutionContext,
  url: URL,
  meta: RequestMetric,
): Promise<Response> {
  // MISSILEER FIRST, and deliberately before both the method gate and the auth
  // gate. It is a different edition with a different auth model, reverse-proxied
  // to the valar-eam-feed origin -- see missileer.ts for why this Worker proxies
  // rather than a route rule dispatching, and why it must never inherit
  // Blipscope's device-key check. Method policy belongs to the origin too: this
  // edition will POST votes, and the 405 below is a Blipscope rule.
  if (isMissileerPath(url.pathname)) return handleMissileer(request, env, url);

  const api = apiEndpoint(url.pathname);
  // POST is accepted ONLY for the leaderboard submit (authed below); every
  // other route is GET. Keyed off the normalized suffix so the legacy path
  // POSTs exactly as the new one does.
  const isLeaderboardSubmit = api?.suffix === "leaderboard" && request.method === "POST";
  if (request.method !== "GET" && !isLeaderboardSubmit) return errorResponse(405, "method_not_allowed");

  // Trailing-slash normalisation, PAGES ONLY. A URL that is printed, typed, or
  // pasted into someone else's redirect field picks up a trailing slash easily,
  // and "/blipscope/support/" answering a JSON error object is a bad way to find
  // that out -- particularly on a path that is going onto a QR code nobody can
  // reprint. One 301 costs a round trip; the 404 costs the customer.
  //
  // APIs are exempt STRUCTURALLY rather than by remembering to exclude them: a
  // redirect on an API path would break deployed firmware (see the pages/APIs
  // asymmetry above), and `api` is already computed, so the guard cannot drift
  // out of step with what counts as an API.
  if (api === null && url.pathname.length > 1 && url.pathname.endsWith("/")) {
    return movedTo(url, url.pathname.replace(/\/+$/, "") || "/");
  }

  // An edition root with no page of its own. Someone who trims a surface off the
  // end of "/blipscope/support" lands here, and the hub is what they were
  // reaching for -- the same reasoning that put a page at "/" at all.
  if (url.pathname === PAGE_PREFIX) return movedTo(url, "/");

  if (url.pathname === "/healthz") return handleHealth(env);
  // Public photo-attribution page (a browser follows the config page's link; no
  // device key). Rendered from the manifest the ingest script publishes to KV.
  if (url.pathname === "/credits") return handleCredits(env);

  // The root is the EDITION HUB, not a Blipscope page -- this domain serves more
  // than one product, and a customer who trims the path off a link must not land
  // on the wrong edition's support. It was an unrouted 404 until now; see
  // docs/web-url-convention.md, which reserved it for exactly this.
  if (url.pathname === "/") return staticPage(indexHtml);

  // Blipscope support. Edition-namespaced like every other page, so Missileer's
  // equivalent can sit at /missileer/support without colliding -- though that one
  // ships in valar-eam-feed, because the isMissileerPath branch above hands the
  // whole /missileer/* prefix to the origin before this Worker sees it.
  if (url.pathname === `${PAGE_PREFIX}/support`) return staticPage(supportHtml);

  // Public leaderboard: HTML board, its JSON, and per-device profiles. No key,
  // same as /credits (a browser follows the config page's link).
  if (url.pathname === `${PAGE_PREFIX}/leaderboard`) return handleLeaderboardPage();
  if (url.pathname === `${PAGE_PREFIX}/leaderboard.json`) return handleLeaderboardJson(request, env);
  const profileMatch = url.pathname.match(/^\/blipscope\/leaderboard\/([0-9a-f]{8,32})$/);
  if (profileMatch) return handleProfile(env, profileMatch[1] as string);

  // DEPRECATED page paths -- permanent redirects to the namespaced ones above.
  // Kept indefinitely: these are the URLs already in customers' browser history
  // and on the config page of every device that has not taken an OTA.
  if (url.pathname === "/leaderboard") return movedTo(url, `${PAGE_PREFIX}/leaderboard`);
  if (url.pathname === "/leaderboard.json") return movedTo(url, `${PAGE_PREFIX}/leaderboard.json`);
  const legacyProfile = url.pathname.match(/^\/leaderboard\/([0-9a-f]{8,32})$/);
  if (legacyProfile) return movedTo(url, `${PAGE_PREFIX}/leaderboard/${legacyProfile[1]}`);
  // Self-hosted webfonts for the board page. Previously pulled from Google,
  // which sent every visitor's IP to a third party from a page whose whole
  // posture is not needing to explain itself. Exact-name lookup against the
  // embedded map -- no path joining, so nothing here can be traversed.
  if (url.pathname.startsWith("/fonts/")) {
    const font = FONTS[url.pathname.slice("/fonts/".length)];
    if (!font) return errorResponse(404, "not_found");
    return new Response(font, {
      headers: {
        "Content-Type": "font/woff2",
        "Content-Length": String(font.byteLength),
        // Immutable for a year: these bytes never change under a given name.
        // Changing a font means changing its filename (see fonts.generated.ts).
        "Cache-Control": "public, max-age=31536000, immutable",
        // The page is same-origin, but a font served without this is unusable
        // from any other origin and the failure is silent in most browsers.
        "Access-Control-Allow-Origin": "*",
      },
    });
  }
  // THE AUTH GATE, and it is load-bearing for all six aliases: everything past
  // this point is authed + rate-limited, so a prefix that fails to reach here is
  // not merely a 404, it is an endpoint that skipped authentication. Both
  // prefixes must arrive, which is why this tests the normalized `api` rather
  // than a literal prefix string.
  if (api === null) return errorResponse(404, "not_found");

  // Per-IP limit first (throttles key-guessing too), then auth, then per-key.
  const ipLimited = await limitByIp(env, request);
  if (ipLimited) return ipLimited;
  const auth = await authenticate(env, request);
  if (auth === null) return errorResponse(401, "unauthorized");
  // Attribute the metric to the device only now that its key has been verified;
  // see setDeviceAttribution() for why unauthenticated headers are never stored.
  setDeviceAttribution(meta, request, auth.deviceAuthed);
  const keyLimited = await limitByKey(env, auth.bucket);
  if (keyLimited) return keyLimited;

  // A device's one-shot OTA memory report, if this check-in carries one. Recorded
  // only past auth + rate limiting, so an anonymous caller can never spend our
  // Analytics Engine budget, and it cannot affect the response the device came
  // for: whatever this does, the request below is served identically.
  recordOtaMem(env, request.headers.get("X-Blip-OTA-Mem"), meta.model, meta.dev);

  // Dispatch on the normalized suffix: one call site per handler, reached
  // identically from /api/v1/blipscope/<x> and the deprecated /v1/<x>. Same
  // handler, same auth, same status codes, byte-identical bodies -- which is the
  // alias contract, and is asserted path-family-by-path-family in the tests.
  if (api.suffix === "blips") return handleBlips(request, env, ctx, meta);
  if (api.suffix === "config") return handleConfig(request, env);
  if (api.suffix === "airports") return handleAirports(request, env);
  if (isLeaderboardSubmit) return handleLeaderboardSubmit(request, env);
  const enrichMatch = api.suffix.match(/^enrich\/([^/]+)$/);
  if (enrichMatch) return handleEnrich(request, env, ctx, enrichMatch[1] as string, meta);
  const photoMatch = api.suffix.match(/^photo\/([^/]+)$/);
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
