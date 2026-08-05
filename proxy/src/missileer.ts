/**
 * Missileer reverse proxy.
 *
 * scopes.valarsystems.com serves more than one edition, but this Worker claims
 * the hostname as a wrangler CUSTOM DOMAIN -- which binds the WHOLE hostname to
 * this Worker and leaves no origin behind it. So the route-rule dispatch that
 * docs/web-url-convention.md anticipates ("/blipscope/* to this Worker,
 * /missileer/* to the EAM one") is not available today: there is nothing to
 * route TO without converting the custom domain to route patterns and standing
 * up an origin DNS record, on the hostname the live fleet depends on.
 *
 * Proxying here instead costs no new infrastructure, keeps one public hostname,
 * and preserves the convention exactly. The Worker already reverse-proxies to
 * external origins (the egress relays), so this is an established pattern.
 *
 * THE COST, STATED: this makes the Blipscope Worker a hard dependency of
 * Missileer's web surfaces. Blipscope Worker down = Missileer pages down. That
 * is acceptable at this scale and is the thing to revisit if Missileer ever
 * needs to fail independently.
 *
 * ---------------------------------------------------------------------------
 * THREE RULES, ALL OF THEM LOAD-BEARING
 *
 * 1. THIS MUST BE REACHED BEFORE THE AUTH GATE, AND MUST NOT USE IT. Everything
 *    past `if (api === null)` in index.ts is authed with a BLIPSCOPE device key.
 *    Missileer is a different product with a different auth model; running its
 *    traffic through Blipscope's gate would either reject every Missileer
 *    device or, worse, accept one because it happened to hold a Blipscope key.
 *    Two products sharing a domain is not one product with a hole in it.
 *
 * 2. apiEndpoint() MUST NOT BE WIDENED TO /api/v1/<any>/. It strips exactly two
 *    prefixes, and there is a test asserting /api/v1/missileer/leaderboard does
 *    NOT normalize into Blipscope's handlers -- because if it did, a Missileer
 *    path would write into Blipscope's leaderboard KV. This module is a
 *    separate, explicit branch for exactly that reason.
 *
 * 3. BLIPSCOPE CREDENTIALS ARE STRIPPED BEFORE FORWARDING. A device key is a
 *    credential for THIS Worker; forwarding it to a third-party origin hands
 *    that origin a working Blipscope key it has no need for and no business
 *    holding. Cheap to strip, permanent to leak.
 * ---------------------------------------------------------------------------
 */
import type { Env } from "./types";
import { errorResponse, jsonResponse } from "./util";

/** Pages: /missileer and /missileer/<surface>. APIs: /api/v1/missileer/<...>. */
const PAGE_PREFIX = "/missileer";
const API_PREFIX = "/api/v1/missileer/";

/**
 * NO ALIAS LAYER, EVER. Blipscope carries /v1/* aliases only because devices
 * were already in the field on those paths when the convention arrived. Nothing
 * is in the field on a Missileer path, so this starts clean and stays clean --
 * the Blipscope migration is the cautionary tale, not the template.
 */
export function isMissileerPath(pathname: string): boolean {
  return (
    pathname === PAGE_PREFIX ||
    pathname.startsWith(`${PAGE_PREFIX}/`) ||
    pathname.startsWith(API_PREFIX)
  );
}

/**
 * Request headers that are ours and must not travel. `X-Blip-Key` is the device
 * credential; the OTA-mem report is telemetry addressed to this Worker and is
 * meaningless (and unasked-for) at another origin.
 */
const STRIP_REQUEST_HEADERS = ["x-blip-key", "x-blip-ota-mem", "cf-connecting-ip"];

/** Bound the origin call so a slow Render instance cannot pin a Worker. */
const ORIGIN_TIMEOUT_MS = 10_000;

/**
 * Warn ONCE PER ISOLATE that the origin is unset.
 *
 * A Worker has no boot hook to hang this on -- module scope cannot read `env`,
 * which arrives per request -- so the first request that finds it missing is
 * the closest thing to boot there is. Latching means a scanner hammering
 * /missileer/* cannot turn a config warning into a log flood; request logs are
 * real money at fleet scale (see the README cost model).
 *
 * It is a WARNING and not silence because the alternative is a 503 with no
 * explanation anywhere: the route is deployed and live, so "it does not work"
 * looks identical to "the product is broken" unless something says which.
 */
let warnedUnconfigured = false;

export async function handleMissileer(request: Request, env: Env, url: URL): Promise<Response> {
  const origin = (env.MISSILEER_ORIGIN ?? "").trim();
  // Unconfigured is 503, not 404: 404 would say "this product does not exist",
  // which is a different and wrong answer during a staged rollout where the
  // route is live but the origin is not yet pointed at anything.
  if (!origin) {
    if (!warnedUnconfigured) {
      warnedUnconfigured = true;
      console.warn(
        JSON.stringify({
          evt: "missileer_unconfigured",
          msg:
            "MISSILEER_ORIGIN is not set: /missileer/* and /api/v1/missileer/* answer 503. " +
            "Set it per environment in proxy/wrangler.toml ([env.<name>.vars]) to the " +
            "valar-eam-feed base URL, e.g. https://valar-eam-feed.onrender.com. " +
            "It is a var, not a secret -- the value is a public hostname.",
          path: url.pathname,
        }),
      );
    }
    // The BODY says why too. A 503 whose reason lives only in a log line is a
    // reason nobody reading the response will ever see, and during bring-up the
    // person hitting this endpoint is usually not the person with log access.
    return jsonResponse(
      {
        v: 1,
        error: "missileer_unconfigured",
        message: "MISSILEER_ORIGIN is not set on this Worker environment; the Missileer origin is unrouted.",
      },
      503,
    );
  }

  // Path is preserved verbatim, including the /api/v1/missileer prefix, so the
  // origin's routes, its logs and ours all read the same URL. Rewriting here
  // would create a second mapping to keep in sync for no benefit.
  const target = new URL(url.pathname + url.search, origin);

  const headers = new Headers(request.headers);
  for (const h of STRIP_REQUEST_HEADERS) headers.delete(h);
  // Host must follow the target, not the public hostname, or a virtual-hosted
  // origin routes to the wrong app.
  headers.delete("host");
  // Announce the hop so the origin can distinguish proxied traffic from a
  // direct call while both paths exist.
  headers.set("X-Forwarded-Host", url.host);
  headers.set("X-Proxied-By", "blipscope-proxy");

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), ORIGIN_TIMEOUT_MS);
  try {
    const upstream = await fetch(target.toString(), {
      method: request.method,
      headers,
      body: request.method === "GET" || request.method === "HEAD" ? undefined : request.body,
      signal: controller.signal,
      redirect: "manual",
    });
    // Rebuild rather than return upstream directly: the body is streamed, but
    // the headers become mutable so the hop is visible in a response the way it
    // is in the request.
    const out = new Headers(upstream.headers);
    out.set("X-Served-By", "blipscope-proxy/missileer");
    return new Response(upstream.body, {
      status: upstream.status,
      statusText: upstream.statusText,
      headers: out,
    });
  } catch (err) {
    // 502, not 500: the failure is upstream of us, and saying so is the
    // difference between "the proxy is broken" and "the origin is asleep" --
    // which on a Render instance that sleeps when idle is a real distinction.
    const reason = (err as Error)?.name === "AbortError" ? "missileer_timeout" : "missileer_unreachable";
    return errorResponse(502, reason);
  } finally {
    clearTimeout(timer);
  }
}
