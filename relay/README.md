# Blipscope egress relay

A dedicated-IP reverse-proxy cache in front of adsb.lol. It exists for exactly
one reason: **Cloudflare Workers egress from a shared per-colo IP pool, and
adsb.lol's anonymous limiter counts other tenants' traffic against us.** A direct
`api.adsb.lol` curl from any normal dedicated IP returns live data at the same
moment the Worker path gets throttled — the only variable is the IP. The relay
gives adsb.lol one stable, non-shared IP to rate-limit, and the Worker consumes
tiles from the relay instead of hitting adsb.lol directly.

Nothing in the Worker's architecture changes: edge cache, stale-while-revalidate,
the circuit breaker, and the multi-feed failover chain all stay put. Only the
upstream base URL moves (`UPSTREAM_ADSB_LOL_BASE`). The relay adds one thing the
Worker can't do across colos: **request collapsing** (`proxy_cache_lock`), so the
whole fleet's polls for a tile become one upstream fetch per cache window.

## HA topology (the shipping shape)

Two relays on **diverse US IPs** (Hetzner Hillsboro + a second in Ashburn or on
Fly), modelled as **two feeds in the Worker's failover chain**:

```
Worker failover chain:  adsb_lol_relay_a  (primary)
                        adsb_lol_relay_b  (terminal — always tried; see PR #119)
```

If relay-A dies, the breaker opens on `adsb_lol_relay_a` and the chain fails over
to `adsb_lol_relay_b`. Because B is the terminal feed, the breaker never skips it
(PR #119). This is the "failover-chain-as-relay-HA" integration — soak it as the
HA pair, not as a single relay, because single-then-add soaks the wrong config.

**Fleet failure story:** relay-A down → chain uses relay-B (invisible). Both down
→ Worker serves SWR cache (up to `SWR_MAX_AGE_S = 600 s`), then the device's
stale ladder escalates honestly. A cold tile during a total double-relay outage
still 503s (no data to serve) — the residual gap the HA pair makes rare.

## Run it (Pi bench this week / VPS for launch)

1. Set the shared secret. Do **not** commit it. Render `nginx.conf` from a
   template or drop a root-only include that sets the `map`:
   ```
   # /etc/nginx/relay-key.conf  (chmod 600, git-ignored)
   map $http_x_relay_key $relay_ok { default 0; "<your-secret>" 1; }
   ```
   and replace the placeholder `map` block in `nginx.conf` with `include`.
2. `sudo mkdir -p /var/cache/nginx/adsblol && sudo cp nginx.conf /etc/nginx/sites-enabled/relay`
3. `sudo nginx -t && sudo systemctl reload nginx`
4. Smoke test (needs the secret; a bare request must 403):
   ```
   curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8080/v2/lat/40/lon/-74/dist/50            # 403
   curl -s -H 'X-Relay-Key: <secret>' http://localhost:8080/v2/lat/40/lon/-74/dist/50 | head -c 80     # aircraft JSON
   ```

### Pointing the Worker at the Pi (bench proof)

The Pi is on your LAN, unreachable from a deployed Worker, so prove it with a
**local** Worker:
```
cd proxy
UPSTREAM_ADSB_LOL_BASE=http://<pi-ip>:8080 npx wrangler dev
# then hit the local Worker's /v1/blips with a dev key and watch X-Cache
```
(The `UPSTREAM_ADSB_LOL_BASE` plumbing lands in the Worker-side PR; until then,
temporarily point `BASE` in `src/upstreams/adsb_lol.ts` at the Pi for the bench.)

## Soak — measure the one honest unknown

adsb.lol's actual per-IP tolerance is undocumented ("dynamic based on load"). The
relay's log is instrumented to measure it directly:

- **adsb.lol throttle rate from our dedicated IP:** `grep -c 'ustatus=429' relay.log`
  over a window. This is the number that decides whether one IP carries the fleet
  or we need to raise `proxy_cache_valid` / shard tiles across more IPs.
- **Cache effectiveness:** `awk '{for(i=1;i<=NF;i++)if($i~/^cache=/)print $i}' relay.log | sort | uniq -c`
  — MISS is upstream load, HIT/EXPIRED/UPDATING is collapsed load.
- **Fleet-side:** the Worker's `X-Cache` flip rate (STALE→HIT) on real devices.
- **HA:** kill relay-A mid-soak and confirm the chain fails to relay-B with no
  fleet-visible gap.

Expected upstream rate ≈ distinct hot tiles ÷ `proxy_cache_valid` (device-count
independent once tiles are shared): ~7–8 req/s at 50 devices / 8 s TTL. Tune the
TTL up or shard across IPs if the 429 measurement says so.

## Operator courtesy

When a relay goes live its IP is **announced to adsb.lol** as a courtesy
identification (we already feed them). The production upstream table in the main
README records which IPs are announced; keep it current when relays change.
