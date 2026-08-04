# Web URL convention

`scopes.valarsystems.com` serves more than one product. This is how its URLs are
laid out, and why pages and APIs are migrated differently.

## The convention

| | Shape | Example |
|---|---|---|
| **Pages** | `/{edition}/{surface}` | `/blipscope/leaderboard` |
| **APIs** | `/api/v1/{edition}/...` | `/api/v1/blipscope/blips` |

`{edition}` is the product: `blipscope` today; `missileer` next, with
`/missileer/leaderboard`, `/missileer/log`, `/missileer/archive`.

**The prefix is what makes the domain multiplexable.** Each edition runs as its
own Worker — Missileer's backend is the separate `valar-eam-feed` — so the path
prefix is the thing a route rule can dispatch on later: `/blipscope/*` to this
Worker, `/missileer/*` to the EAM one. Without it, one domain cannot serve two
Workers without a shared router that has to know about every edition.

`/` is deliberately unrouted. It becomes a hub page listing the editions.

Infrastructure endpoints are **not** edition-scoped, because they describe the
deployment rather than a product: `/healthz`, `/credits`, `/fonts/*`.

## Pages redirect; APIs alias. This asymmetry is the important part.

**Pages get a permanent redirect (301).** Browsers follow redirects, bookmarks
and search results get rewritten by a 301, and the cost of being wrong is one
extra round trip.

**APIs get an internal alias and must NEVER get a redirect.** The clients are
deployed ESP32 firmware. `HTTPClient` is not guaranteed to follow a 301, and on
the leaderboard `POST` following one would require re-sending the request body —
so a redirect there would break the submit for every device in the field the
moment it deployed, and would present as a server outage rather than as a
routing change. An alias is a second path into the *same handler*: identical
response bytes, identical auth, identical status codes.

In [`proxy/src/index.ts`](../proxy/src/index.ts) both prefixes are reduced to a
bare endpoint suffix by `apiEndpoint()` before dispatch, so there is exactly one
call site per handler. The alias cannot drift from the new path, because there is
no second route table to drift.

> **The auth gate is load-bearing.** Everything past the prefix check in `route()`
> is authenticated and rate-limited. A prefix that fails to reach that check is
> not a 404 — it is an endpoint that skipped authentication. That check therefore
> tests the normalized result, not a literal prefix string.

## Current Blipscope paths

### Pages

| Current | Deprecated | Status |
|---|---|---|
| `/blipscope/leaderboard` | `/leaderboard` | 301 |
| `/blipscope/leaderboard.json` | `/leaderboard.json` | 301 |
| `/blipscope/leaderboard/<id>` | `/leaderboard/<id>` | 301 |

### APIs

| Current | Deprecated alias | Method |
|---|---|---|
| `/api/v1/blipscope/blips` | `/v1/blips` | GET |
| `/api/v1/blipscope/config` | `/v1/config` | GET |
| `/api/v1/blipscope/airports` | `/v1/airports` | GET |
| `/api/v1/blipscope/leaderboard` | `/v1/leaderboard` | **POST** |
| `/api/v1/blipscope/enrich/<hex>` | `/v1/enrich/<hex>` | GET |
| `/api/v1/blipscope/photo/<key>` | `/v1/photo/<key>` | GET |

**`/v1/*` is now Blipscope-legacy alias space and must never be assigned to
another edition.** Some device will keep calling it until every unit in the field
has taken an OTA, and a path that means one thing to old firmware and another to
a new product is a bug that cannot be diagnosed from either side.

## The photo path moved without a firmware change

`/v1/photo/<key>` was never a firmware constant. The proxy sends the path inside
the enrich response ([`enrich.ts`](../proxy/src/enrich.ts)) and the device treats
it as an opaque string to concatenate onto its cloud base. Changing the emitted
string moved the whole fleet — including units that will never take another OTA —
the instant the Worker deployed. The Worker deploy is atomic, so the new route
existed before any device could be handed the new string.

The `/v1/photo` alias is kept anyway: URLs already cached on a device across the
deploy moment, and so the deprecation story is the same for all six endpoints
rather than five-plus-a-special-case.

## When the aliases can be deleted

**Legacy hit-rate is the instrument.** [`proxy/src/metrics.ts`](../proxy/src/metrics.ts)
keeps namespaced and legacy paths as **separate** route templates rather than
merging them into one. Merging would lose the only number that answers the
question:

> When `/v1/*` counts have been **zero for a full fleet-update cycle**, every
> device has taken the OTA and the alias layer can be removed.

Until then the same count identifies which devices have not updated.

The dashboard's endpoint filters
([`dashboard/src/analytics.ts`](../dashboard/src/analytics.ts)) match **both**
families with an `IN` list, because Analytics Engine points are retained rather
than rewritten: any window spanning the cutover holds both shapes by design, and
a device on old firmware keeps producing the old one long afterwards. Filtering
on the new path alone reads `0` for cards and enriches — which looks exactly like
a fleet that stopped fetching photos, rather than a query that stopped matching.

## Adding an edition

1. Pages at `/{edition}/{surface}`, APIs at `/api/v1/{edition}/...`.
2. Never reuse `/v1/*` — it belongs to Blipscope's deprecation window.
3. Add both to `KNOWN_ROUTES` in `metrics.ts`, or every path buckets to `/other`
   and stops being distinguishable in analytics.
4. New editions get no alias layer. It exists only for firmware already in the
   field on the old paths.
