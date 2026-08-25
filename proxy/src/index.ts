import type { Env } from "./types";
import { handleAirports } from "./airports";
import { handleBlips } from "./blips";
import { handleConfig } from "./config";
import { handleEnrich } from "./enrich";
import { handleEnroll } from "./enroll";
import { enrollHtml } from "./enrollpage";
import {
  handleLeaderboardJson,
  handleLeaderboardPage,
  handleLeaderboardSubmit,
  handleProfile,
} from "./leaderboard";
import { FONTS } from "./fonts.generated";
import { indexHtml, supportHtml, notfoundHtml } from "./pages.generated";
import { record, recordOtaMem, setDeviceAttribution, type RequestMetric } from "./metrics";
import { handleMissileer, isMissileerPath } from "./missileer";
import { handleCredits, handlePhoto } from "./photos";
import { verifyDeviceKey } from "./deviceauth";
import { limitByIp, limitByKey } from "./ratelimit";
import { feedHealth } from "./upstreams/chain";
import { isRevoked } from "./revocation";
import { errorResponse, jsonResponse } from "./util";

// Authenticate a request. Per-device keys are now the ONLY way in: an
// HMAC-derived key that is only valid when presented with the X-Blip-Device id
// it was derived for. Returns a rate-limit bucket id, or null when rejected.
// The bucket is never the key itself.
//
// The shared BLIP_KEYS list was removed 2026-08-13, after every board on the
// fleet had enrolled and the analytics showed zero successful device requests
// still arriving on it. Its problem was structural rather than cryptographic:
// one secret held by every device means a single leak revokes the whole fleet,
// and a request proved only that SOMEONE held the key -- never which device --
// so rate limiting bucketed by key index and attribution was impossible. Both
// of those are now per-identity.
//
// There is deliberately no fallback. A fallback is what makes a credential
// migration untestable: while both paths work, every check passes whichever one
// the caller happens to be exercising, which is why the cutover was verified by
// showing a SHARED response before an enrolled one rather than the reverse.
async function authenticate(env: Env, request: Request): Promise<{ bucket: string } | null> {
  const provided = request.headers.get("X-Blip-Key") ?? "";
  if (!provided) return null;

  const deviceId = (request.headers.get("X-Blip-Device") ?? "").trim().toLowerCase();
  if (!deviceId) return null;

  // Revocation is checked BEFORE the key path, so a revoked device is refused
  // even while its derived key remains cryptographically valid -- revocation is
  // by identity, and with the shared path gone, identity is all there is.
  // isRevoked fails OPEN on a KV error (see revocation.ts): a storage blip must
  // never take the fleet down to enforce a list that is almost always empty.
  if (await isRevoked(env, deviceId)) return null;

  if (await verifyDeviceKey(env, deviceId, provided)) return { bucket: `dev:${deviceId}` };
  return null;
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
function staticPage(html: string, status = 200): Response {
  const bytes = new TextEncoder().encode(html);
  // A 404 gets a SHORT cache. The 300 s that suits a live page would keep
  // serving "not found" for five minutes after the page it names starts
  // existing -- and the paths most likely to 404 here are the ones about to go
  // on a printed card, i.e. exactly the ones we might fix in a hurry.
  return new Response(bytes, {
    status,
    headers: {
      "Content-Type": "text/html; charset=utf-8",
      "Content-Length": String(bytes.byteLength),
      "Cache-Control": status === 200 ? "public, max-age=300" : "public, max-age=30",
    },
  });
}

// 301, not 302: the move is permanent, and a permanent redirect is what gets
// bookmarks and search results rewritten instead of re-followed forever.
function movedTo(url: URL, newPath: string, status: 301 | 302 = 301): Response {
  const target = new URL(url.toString());
  target.pathname = newPath;
  return new Response(null, { status, headers: { Location: target.toString() } });
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
  // Enrollment POSTs too, and it is NOT an `api` route: it sits above the auth
  // gate on purpose, because a board with no key yet is the caller it exists
  // for. Listed here rather than moved above this line so that every method
  // exception in the Worker stays visible in one place.
  const isEnrollSubmit = url.pathname === `${PAGE_PREFIX}/enroll` && request.method === "POST";
  if (request.method !== "GET" && !isLeaderboardSubmit && !isEnrollSubmit) {
    return errorResponse(405, "method_not_allowed");
  }

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

  // The edition root goes to SUPPORT, not the hub. This is the short URL going
  // on the printed quick-start card, and the card is the one artifact we cannot
  // revise after the fact -- so it must land on the page that answers the
  // question someone scans a support QR to ask, rather than on a product index
  // that makes them choose again. Trimming further, to "/", still reaches the
  // hub for anyone who wants the other editions.
  //
  // 302, NOT 301, and this one is load-bearing. A 301 is cached by browsers
  // INDEFINITELY, so every customer who has already scanned the card keeps the
  // old destination forever -- which negates the entire reason for routing the
  // printed QR through this Worker instead of printing the support URL
  // directly. That reason is re-pointability AFTER the cards exist, and the
  // cards cannot be reprinted. A 301 also asserts a permanence we have
  // explicitly said we do not want.
  //
  // The trailing-slash normalisation above stays 301: those targets ARE
  // permanent, and caching them is the point.
  if (url.pathname === PAGE_PREFIX) return movedTo(url, `${PAGE_PREFIX}/support`, 302);

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

  // Device enrollment. The PAGE is public (it has to be — a customer on a phone
  // reaches it directly); the ENDPOINT mints nothing without a verified solve.
  // Both sit above the auth gate below, because a board with no key yet is
  // exactly the caller they exist for.
  // ONE path, dispatched on METHOD. Written as two sequential `if`s on the same
  // pathname first, which made the POST branch unreachable — the GET matched
  // every time and enrollment would have been a page that never minted.
  if (url.pathname === `${PAGE_PREFIX}/enroll`) {
    return request.method === "POST"
      ? handleEnroll(request, env)
      : staticPage(enrollHtml(env.TURNSTILE_SITEKEY ?? ""));
  }
  // THE SHORT URL, and it is not a nicety. The device's fallback text tells a
  // customer with no internet on that machine to TYPE this on their phone, next
  // to an 8-hex id — every character is one they can get wrong, so the typed
  // form stays "/enroll" and lands here.
  //
  // This route was missing when enrollment first landed, while the firmware
  // popup pointed at it: the whole feature was one 404 with sixteen passing
  // tests behind it, because every test requested the prefixed path. The
  // firmware now opens the canonical path directly (no redirect in the machine
  // path), and this serves the human one.
  //
  // GET only, by falling out of the method gate above rather than by a check
  // here: `isEnrollSubmit` matches the prefixed path alone, so a POST to this
  // one is already a 405 and can never be answered with a redirect the browser
  // would have to re-send a body to follow.
  if (url.pathname === "/enroll") return movedTo(url, `${PAGE_PREFIX}/enroll`);

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
  // NOT an API path, so this is a person -- a typo, a stale link, a QR that
  // aged badly. They get a page. Answering `{"v":1,"error":"not_found"}` to a
  // customer reads as a broken device rather than a wrong address, and the
  // address most likely to be wrong is the one printed on a card nobody can
  // reprint.
  //
  // The auth gate below is UNCHANGED and still load-bearing: a mistyped API
  // path resolves `api` non-null and falls through to the JSON 404 at the end
  // of this function, so machines still get machine errors.
  if (api === null) return staticPage(notfoundHtml, 404);

  // Per-IP limit first (throttles key-guessing too), then auth, then per-key.
  const ipLimited = await limitByIp(env, request);
  if (ipLimited) return ipLimited;
  const auth = await authenticate(env, request);
  if (auth === null) return errorResponse(401, "unauthorized");
  // Attribute the metric to the device only now that its key has been verified;
  // see setDeviceAttribution() for why unauthenticated headers are never stored.
  setDeviceAttribution(meta, request);
  // WHICH CREDENTIAL WAS ACCEPTED — echoed to the device, not merely recorded.
  //
  // Now always "device": reaching here at all means the per-device path passed,
  // since it is the only path. KEPT ANYWAY, and not because removing it is hard.
  // Its job was to make a credential migration observable at the bench, and it
  // did that; the next migration will want the same affordance, and a header
  // that has always been present is cheaper to keep than to reintroduce. It also
  // keeps the device-visible contract stable across this deploy: firmware and
  // smoke-prod.sh both assert on it TODAY, and smoke-prod asserting "expect
  // device, got device" is the check that proves the shared path is gone.
  meta.authPath = "device";
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
    // Surface the accepted credential to the caller. Only ever set on requests
    // that actually authenticated, so its ABSENCE is meaningful too: no header
    // means no auth happened (a page, a 401, a health check).
    if (meta.authPath) {
      response = new Response(response.body, response);
      response.headers.set("X-Blip-Auth", meta.authPath);
    }
    return response;
  },
} satisfies ExportedHandler<Env>;
