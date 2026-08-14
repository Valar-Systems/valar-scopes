#!/usr/bin/env bash
#
# Blipscope egress relay -- idempotent root setup for one box (Debian/Ubuntu).
# Safe to re-run; converges the box to the desired state each time.
#
# WHAT IT DOES
#   - installs nginx (apt) + unattended-upgrades
#   - writes the caching relay vhost: proxy_cache + cache_lock, use_stale on
#     error/timeout/429, TTL a clearly-marked tunable (CACHE_TTL, starts 6s)
#   - serves TWO upstreams, BOTH shipping: adsb.fi under /fi (the chain primary
#     for positions + hex) and adsb.lol at the root (licensed fallback, and the
#     only route source), each in its own cache zone under identical policy
#   - X-Relay-Key gate: 403 unless the caller presents the key, read from a
#     ROOT-ONLY file (never in this script or in git)
#   - installs the Cloudflare Origin certificate for TLS (443)
#   - ufw: allow 22 + 443-from-Cloudflare-ranges only, deny the rest
#   - unauthenticated /healthz
#   - adds X-Relay-Cache: HIT|MISS|STALE|UPDATING and logs upstream_status so the
#     soak can read adsb.lol's REAL per-IP 429 behaviour from the relay's own logs
#
# PREREQUISITES -- scp these to the box first (see relay/README.md):
#   /etc/ssl/cloudflare/origin.pem   Cloudflare Origin cert   (0644 root:root)
#   /etc/ssl/cloudflare/origin.key   its private key          (0600 root:root)
#   /etc/nginx/relay.key             the X-Relay-Key value     (0600 root:root)
#
# RUN:  sudo CACHE_TTL=6s bash setup-relay.sh
set -euo pipefail

# ---- tunables ---------------------------------------------------------------
# THE scaling knob. Upstream fetch rate ~= (distinct hot tiles) / CACHE_TTL,
# independent of device count (the fleet collapses to one fetch per tile per TTL).
# It is NOT a pile-up risk: refresh is foreground + proxy_cache_lock, so a busy tile
# whose fetch exceeds the TTL just refreshes at its fetch rate (one at a time) while
# use_stale serves the rest -- the TTL is a floor, the throttle is the real ceiling.
#
# !! THIS KNOB IS PINCHED BETWEEN TWO CONSTRAINTS THAT DO NOT BOTH FIT AT 50 BOARDS !!
#
#   RATE (pushes UP):   adsb.fi is 1 req/s per IP, 4xx/429 counted. 50 scattered
#                       boards are ~50 distinct 0.05-deg tiles. 50/TTL must stay under
#                       the two-IP budget of 2 req/s, so TTL >= 25s; 30s leaves ~17%
#                       headroom. (Two IPs only count if BOTH carry traffic -- see
#                       partitionOrder in proxy/src/upstreams/chain.ts. Under pure
#                       failover the budget is one IP and TTL would have to be 50s.)
#
#   STALENESS (pushes DOWN): the device flags amber past its stale threshold, and the
#                       age it measures INCLUDES the server-side lag
#                       (AircraftManager::IsDataStale, dataLagAtMergeMs, derived from
#                       the picture timestamp we serve). That threshold USED to be
#                       staleFactor(3) x the current poll interval and nothing else --
#                       15s on an active 5s poll, and only 6s on the 2s-polling
#                       s3-146/s3-21. So 8s was never a freshness preference: it was
#                       the largest TTL that kept an active device inside its own amber
#                       line, and it was ALREADY too slow for the 2s SKUs.
#
# RESOLVED 2026-08-08. IsDataStale now takes max(staleFactor x poll, minStaleMs), with
# minStaleMs served as fleet config and defaulting to 45s, so the threshold no longer
# tracks how often the device asks -- which was measuring the wrong thing, since polling
# faster cannot make the server's tile any newer. 45s clears the worst-case healthy age
# at a 30s TTL (30 + ~1 relay fetch + 3 Worker fresh window + one poll interval ~= 39s)
# and is exactly the old idle-tier threshold, so idle and night are untouched.
#
# ORDER OF OPERATIONS -- backwards shows the whole fleet amber:
#   1. devices run firmware with the floor, AND the Worker serves minStaleMs
#   2. raise BLIPS_FEED_MAX_AGE_MS to 85000 (N >= 2 x TTL + 25s) and deploy
#   3. only then raise this to 30s and re-run setup-relay.sh on BOTH boxes
# A device on older firmware ignores minStaleMs and keeps the 15s active threshold, so
# it reads amber at a 30s TTL. That is an upgrade gate, not a surprise.
#
# !! THE DEFAULT BELOW IS STEP 3'S TARGET, NOT WHAT THE BOXES RUN. !!
#
# Both relays are on 8s and step 3 is gated on the firmware floor actually reaching
# the boards, not merely existing in the tree. So a BARE re-run of this script -- for
# any reason, a cache-size change, a key rotation, anything -- silently performs step
# 3 early, out of the order written directly above, and every device still on older
# firmware goes amber.
#
# PASS IT EXPLICITLY when re-running for something else:
#     sudo CACHE_TTL=8s bash setup-relay.sh
#
# And read the live value first rather than trusting this file, since the whole point
# is that they differ:
#     ssh root@<relay> "nginx -T 2>/dev/null | grep -A2 'location \^~ /fi/'"
#
# (Verified 2026-08-13: both boxes serve `proxy_cache_valid 200 8s` on /fi.)
CACHE_TTL="${CACHE_TTL:-30s}"

# Tile TTL for the /fi50 experiment path only (see the location block below).
# The Worker's BLIPS_FEED_MAX_AGE_MS must satisfy N >= 2 x this + 25s, or every
# fetch on this path looks degraded and the chain walks all feeds every request.
FI50_TTL="${FI50_TTL:-50s}"
FI25_TTL="${FI25_TTL:-25s}"

UPSTREAM_HOST="api.adsb.lol"
CERT="/etc/ssl/cloudflare/origin.pem"
KEY="/etc/ssl/cloudflare/origin.key"
RELAY_KEY_FILE="/etc/nginx/relay.key"
KEY_MAP="/etc/nginx/conf.d/00-relay-key.conf"
SITE="/etc/nginx/sites-available/relay"
CACHE_DIR="/var/cache/nginx/adsblol"
# Second upstream, served under the /fi path prefix: adsb.fi -- the chain PRIMARY
# for positions and hex since 2026-07-31, permitted commercially in writing
# (2026-08-05) conditional on the 1 req/s per-IP limit and nothing else. This is a
# LIVE SERVING PATH (see proxy/src/upstreams/adsb_fi.ts).
# Its own cache zone so the two upstreams can be sized, inspected and dropped
# independently -- deleting adsb.fi must never cost us a warm adsb.lol tile.
CACHE_DIR_FI="/var/cache/nginx/adsbfi"

log() { printf '\033[1;36m[relay]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[relay] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run as root (sudo)"

# ---- preflight: required files must already be in place ---------------------
for f in "$CERT" "$KEY" "$RELAY_KEY_FILE"; do
  [ -s "$f" ] || die "missing $f -- scp it to the box first (see relay/README.md)"
done
chmod 600 "$KEY" "$RELAY_KEY_FILE"; chmod 644 "$CERT"

# ---- packages ---------------------------------------------------------------
log "installing nginx + unattended-upgrades"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq nginx ufw unattended-upgrades curl >/dev/null

# unattended security upgrades (idempotent: overwrite the toggle file)
cat > /etc/apt/apt.conf.d/20auto-upgrades <<'EOF'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF

# ---- X-Relay-Key map (generated from the root-only key file) ----------------
# nginx reads config as root at load, so the map (which contains the key) is
# root-only. Regenerated each run so rotating relay.key + re-running is enough.
log "generating X-Relay-Key gate from $RELAY_KEY_FILE"
RELAY_KEY_VALUE="$(tr -d ' \t\r\n' < "$RELAY_KEY_FILE")"
[ -n "$RELAY_KEY_VALUE" ] || die "$RELAY_KEY_FILE is empty"
umask 077
cat > "$KEY_MAP" <<EOF
# GENERATED by setup-relay.sh from $RELAY_KEY_FILE -- do not edit. Root-only.
# The 64-char relay key exceeds nginx's default map_hash_bucket_size (64 on many
# builds), which fails "could not build map_hash" at load -- bump it.
map_hash_bucket_size 128;
map \$http_x_relay_key \$relay_ok {
    default 0;
    "$RELAY_KEY_VALUE" 1;
}
EOF
umask 022
unset RELAY_KEY_VALUE

# ---- cache dirs -------------------------------------------------------------
mkdir -p "$CACHE_DIR" "$CACHE_DIR_FI"
chown www-data:www-data "$CACHE_DIR" "$CACHE_DIR_FI"

# ---- shared upstream proxy directives (included by each location) -----------
# Factored out so the two locations below differ ONLY in cache TTL. Quoted
# heredoc: nginx $variables must reach the file, not be expanded by bash.
mkdir -p /etc/nginx/snippets
cat > /etc/nginx/snippets/relay-upstream.conf <<'NGINX'
if ($relay_ok = 0) { return 403; }
set $upstream "api.adsb.lol";
proxy_pass https://$upstream;
proxy_ssl_server_name on;
proxy_ssl_name api.adsb.lol;
proxy_set_header Host api.adsb.lol;
proxy_set_header User-Agent "blipscope-relay";
proxy_set_header X-Relay-Key "";   # never forward our secret upstream
proxy_cache adsblol;
proxy_cache_key "$request_uri";     # the Worker already quantizes tiles
# OUR proxy_cache_valid (per location) governs the cache, not adsb.lol's headers.
proxy_ignore_headers Cache-Control Expires Set-Cookie;
# Collapse concurrent identical requests into ONE upstream fetch.
proxy_cache_lock on;
proxy_cache_lock_timeout 6s;
# Absorb adsb.lol slowness here: serve last-good instantly while a refresh runs.
# ONLY 200s are cached (per location) -- a 429 is never cached, it falls through
# to this use_stale, which serves the last good response.
#
# adsb.lol bandwidth-throttles the ANONYMOUS API to ~100 kbit/s (~12 KB/s), measured
# identically from both relays AND an unrelated home ISP -- a server-side egress cap,
# not a per-IP limit we can escape. A busy 160 km tile is ~187 KB => a full fetch
# takes ~16 s (rural tiles are ~1 KB => instant). The relay makes that invisible by
# COLLAPSING the fleet to one upstream fetch per tile per TTL and serving stale in the
# meantime -- but the refresh runs in the FOREGROUND, NOT via background_update.
#
# background_update ORPHANS the refresh under sparse polling: nginx serves the stale
# copy instantly, the triggering client disconnects, and the in-flight update subrequest
# is cut short before it caches. A tile polled every ~15 s then re-serves stale and
# re-orphans forever (observed: a tile stuck 21 min stale while the device polled it
# every 15 s; only a 5 s curl loop kept a refresh alive long enough to land). Foreground
# has no orphan: the request that finds the tile stale DOES the fetch and waits for it,
# so fresh data always lands. use_stale `updating` serves any CONCURRENT requests stale
# while that one fetch runs, so only the single refreshing request waits. And the wait is
# invisible: the Worker's revalidation runs in ctx.waitUntil (the device already has its
# instant answer from the Worker cache), so the relay can take 4-16 s to return fresh.
#
# The earlier "stuck in UPDATING" wedge was proxy_read_timeout (15 s) landing ~0.1 s
# SHORT of the 15.7 s throttled body, so a refresh timed out before caching and re-fired
# forever. read_timeout is now 30 s (below) so the throttled body completes. Keep the
# requested radius bounded (Worker R_BUCKETS_KM) so no single fetch approaches 30 s.
proxy_cache_use_stale updating error timeout http_429 http_500 http_502 http_503 http_504;
add_header X-Relay-Cache $upstream_cache_status always;
# Orange-clouded: tell the CF edge NOT to add its own cache (Worker ignores this).
add_header Cache-Control "no-store" always;
proxy_connect_timeout 5s;
proxy_read_timeout 30s;   # anon throttle ~12 KB/s; a 187 KB busy tile is ~16s. Must
                          # exceed the worst throttled body or bg refresh never caches.
NGINX

# ---- adsb.fi upstream snippet (served under /fi) ----------------------------
# Same shape as the adsb.lol snippet above: same key gate, same collapsing, same
# use_stale, same "a 429 makes the next attempt LATER, never sooner" principle.
# It differs in exactly three ways, all forced by the upstream:
#
#  1. PATH REWRITE. adsb.fi's API lives under /api, so /fi/v3/... -> /api/v3/....
#     $request_uri (the CACHE KEY) keeps the original /fi/... form, so the two
#     upstreams can never collide on a key even if they shared a zone.
#  2. ITS OWN CACHE ZONE (adsbfi). Independent sizing + a clean "drop adsb.fi"
#     story that cannot evict a warm adsb.lol tile.
#  3. SHORTER read timeout. adsb.fi is NOT bandwidth-capped -- measured ~90 KB/s
#     from both relays (27 KB tile in ~0.30 s) vs adsb.lol's ~12 KB/s anon cap --
#     so 30 s of patience buys nothing here; a slow response means trouble, and
#     failing fast keeps a stuck fetch from occupying the cache lock.
#
# RATE LIMIT -- the binding constraint, read before touching CACHE_TTL:
# adsb.fi's public limit is 1 req/s PER IP, and 400/401/403/404/429 responses
# COUNT TOWARD IT (their docs), so a re-firing 429 doesn't just fail, it digs the
# hole deeper and earns "a temporary IP address restriction". Upstream rate here
# is (distinct hot tiles) / CACHE_TTL, so at TTL=8s the ceiling is only ~8 hot
# tiles per relay. The hold-down below is therefore load-bearing, not courtesy.
cat > /etc/nginx/snippets/relay-upstream-fi.conf <<'NGINX'
if ($relay_ok = 0) { return 403; }
set $upstream_fi "opendata.adsb.fi";
# Strip our prefix and map onto adsb.fi's /api root. Matches BOTH the normal /fi
# path and the /fi50 experiment path (this snippet is shared by both locations);
# $request_uri -- the cache key -- keeps the original prefix, so the two never
# share a cache entry.
rewrite ^/fi(?:25|50)?/(.*)$ /api/$1 break;   # /fi50/v3/lat/... -> /api/v3/lat/...
proxy_pass https://$upstream_fi;
proxy_ssl_server_name on;
proxy_ssl_name opendata.adsb.fi;
proxy_set_header Host opendata.adsb.fi;
proxy_set_header User-Agent "blipscope-relay";
proxy_set_header X-Relay-Key "";   # never forward our secret upstream
proxy_cache adsbfi;
proxy_cache_key "$request_uri";     # the ORIGINAL /fi/... uri, not the rewritten one
proxy_ignore_headers Cache-Control Expires Set-Cookie;
proxy_cache_lock on;
proxy_cache_lock_timeout 6s;
proxy_cache_use_stale updating error timeout http_429 http_500 http_502 http_503 http_504;
add_header X-Relay-Cache $upstream_cache_status always;
add_header Cache-Control "no-store" always;
proxy_connect_timeout 5s;
proxy_read_timeout 10s;
NGINX

# ---- relay vhost ------------------------------------------------------------
log "writing relay vhost (tile TTL=$CACHE_TTL + 429 hold-down 15s, hex TTL=24h + 429 hold-down 60s)"
log "  upstreams: /fi -> opendata.adsb.fi (chain primary) | / -> api.adsb.lol (fallback + routes)"
cat > "$SITE" <<'NGINX'
proxy_cache_path /var/cache/nginx/adsblol levels=1:2 keys_zone=adsblol:10m
                 max_size=100m inactive=2h use_temp_path=off;
# adsb.fi, served under /fi -- the chain PRIMARY.
#
# SIZED THE SAME AS THE ROOT ZONE, and not because they carry equal traffic today.
# Either one can be carrying ALL of it: adsb.fi serves the fleet's working set
# normally, and adsb.lol takes the whole load the moment the chain fails over --
# which is the exact moment a too-small zone would start evicting warm tiles and
# multiplying upstream fetches, on the source we have just fallen back to.
#
# This was 5m/50m ("one comparison tile, not the fleet's working set"), sized when
# /fi was a bench path. That sizing outlived the role by two weeks: correct for
# what it was, quietly wrong for the primary, and invisible at bench scale because
# a handful of tiles never approaches 50 MB. It would have surfaced as unexplained
# upstream-fetch growth somewhere in the pilot ramp.
proxy_cache_path /var/cache/nginx/adsbfi levels=1:2 keys_zone=adsbfi:10m
                 max_size=100m inactive=2h use_temp_path=off;

# Log adsb.lol's OWN status (ustatus) + our cache status, so the soak measures the
# one honest unknown -- the real per-IP 429 rate -- from these logs:
#   grep -c 'ustatus=429' /var/log/nginx/relay.log
# src=$remote_addr separates the two kinds of traffic that share this log: the
# bench poller runs ON the box (127.0.0.1) while real fleet traffic arrives from
# Cloudflare. Without it a staging soak can't tell its own synthetic load from the
# Worker's. Kept AFTER rt= so measure.mjs's `.*uri=` regex is unaffected.
log_format relay '$time_iso8601 cache=$upstream_cache_status status=$status '
                 'ustatus=$upstream_status rt=$request_time src=$remote_addr '
                 'uri="$request_uri"';

server {
    listen 443 ssl;
    listen [::]:443 ssl;
    # (no HTTP/2: not needed for a machine-to-machine proxy, and the standalone
    # `http2 on;` directive only exists in nginx >= 1.25.1; Ubuntu 24.04 ships 1.24.)
    server_name _;

    ssl_certificate     /etc/ssl/cloudflare/origin.pem;
    ssl_certificate_key /etc/ssl/cloudflare/origin.key;
    ssl_protocols TLSv1.2 TLSv1.3;

    access_log /var/log/nginx/relay.log relay;

    # Unauthenticated liveness probe (no key, not logged).
    location = /healthz {
        access_log off;
        add_header Content-Type text/plain;
        return 200 "ok\n";
    }

    # Re-resolve api.adsb.lol periodically (its IP can move).
    resolver 1.1.1.1 8.8.8.8 valid=300s ipv6=off;

    # Aircraft METADATA (/v2/hex) is IMMUTABLE on human timescales -- type/reg/operator
    # change only on re-registration (months). The Worker's 30-day fleet KV is the real
    # authority and fetches a hex ONLY on a KV miss (never-seen airframe); this cache is
    # the second line. Cache 200s for 24h so a re-seen hex never re-hits adsb.lol.
    #
    # NEGATIVE-CACHE the 429 (60s hold-down): the storm was a 429'd hex caching nothing,
    # so every poll re-fired it -- ~94% sustained 429 from one device. Caching the 429
    # for 60s means a 429 makes the next upstream attempt LATER, never sooner. use_stale
    # (http_429, in the snippet) still shields any hex we HAVE seen -- it serves the
    # cached 200 and the 429 is not stored; the 60s hold-down only applies to a
    # never-seen hex that has no good copy to fall back to.
    location ^~ /v2/hex/ {
        include /etc/nginx/snippets/relay-upstream.conf;
        proxy_cache_valid 200 24h;
        proxy_cache_valid 429 60s;
    }

    # Live POSITIONS (/v2/lat), routeset, everything else: short TTL for a fresh
    # picture. A tile we HAVE seen keeps its last-good 200 -- use_stale (http_429,
    # in the snippet) serves that stale copy on a 429 and the 429 is NOT stored,
    # so the good tile is never clobbered (verified live: the /v2/hex block has run
    # this same valid-429 + use_stale-429 pair without breaking seen-hex HITs).
    #
    # The 429 hold-down (15s) matches the /v2/hex fix, one class of bug: without it,
    # a COLD tile (no last-good) that adsb.lol 429s caches nothing, so every ~15s
    # device poll re-fires upstream -- a 429 must make the next attempt LATER, never
    # sooner. It only bites the cold case (a seen tile is shielded by use_stale
    # above); 15s (not the hex 60s) because positions are live -- a cold tile must
    # start refreshing again quickly once the throttle clears.
    location / {
        include /etc/nginx/snippets/relay-upstream.conf;
        proxy_cache_valid 200 __CACHE_TTL__;
        proxy_cache_valid 429 15s;
    }

    # ---- adsb.fi under /fi -- THE CHAIN PRIMARY, live customer traffic -------
    # Deliberately identical TTL/hold-down policy to the adsb.lol blocks above,
    # which began as a way to make the 24 h comparison measure the UPSTREAM and
    # not our tuning, and is now simply the shipping policy for both. Longer
    # prefixes win in nginx, so /fi/v2/hex/ takes the metadata policy and
    # everything else under /fi takes the live-positions policy.
    #
    # THE HOLD-DOWN IS LOAD-BEARING HERE IN A WAY IT IS NOT AT THE ROOT. Our
    # permission from adsb.fi (in writing, 2026-08-05) is conditional on staying
    # inside their 1 req/s per-IP limit and on nothing else -- and 4xx/429s count
    # toward that limit. So re-firing a 429 does not merely waste a request, it
    # spends the budget the permission depends on. Do not shorten proxy_cache_valid
    # 429 here without re-reading that grant.
    location ^~ /fi/v2/hex/ {
        include /etc/nginx/snippets/relay-upstream-fi.conf;
        proxy_cache_valid 200 24h;
        proxy_cache_valid 429 60s;
    }

    location ^~ /fi/ {
        include /etc/nginx/snippets/relay-upstream-fi.conf;
        proxy_cache_valid 200 __CACHE_TTL__;
        proxy_cache_valid 429 15s;
    }

    # ---- /fi50: the SLOW-TTL EXPERIMENT path (staging only) ------------------
    # Identical to /fi except positions cache for FI50_TTL instead of CACHE_TTL.
    # It exists to answer one question by eye before we commit to it fleet-wide:
    # a long tile TTL is what buys headroom under adsb.fi's 1 req/s limit
    # (upstream rate = distinct hot tiles / TTL), but it is paid for in freshness,
    # and the honest test is pattern traffic at a field like KBDN where
    # dead-reckoning is weakest -- tight circuits, constant turns, low speed.
    #
    # Separate LOCATION, not a separate zone: it shares the adsbfi cache zone but
    # its own cache keys (/fi50/... vs /fi/...), so the experiment can never serve
    # a 50 s tile to something asking on the normal path, and vice versa.
    #
    # REVERSIBLE BY ENV VAR, not by a relay change: staging points
    # UPSTREAM_ADSB_FI_BASE at /fi50 and production does not. To end the
    # experiment, repoint staging back at /fi -- this block can stay.
    location ^~ /fi50/ {
        include /etc/nginx/snippets/relay-upstream-fi.conf;
        proxy_cache_valid 200 __FI50_TTL__;
        proxy_cache_valid 429 15s;
    }

    # The other half of the A/B. 25 s is what a raised rate limit (two IPs, or a
    # commercial tier) would buy back; 50 s is what one IP forces at pilot scale.
    # Same upstream, same everything else -- so switching the Worker's base URL
    # between /fi25 and /fi50 changes exactly ONE variable, which is the only way
    # a by-eye judgement of dead-reckoning quality means anything.
    location ^~ /fi25/ {
        include /etc/nginx/snippets/relay-upstream-fi.conf;
        proxy_cache_valid 200 __FI25_TTL__;
        proxy_cache_valid 429 15s;
    }
}
NGINX
sed -i "s/__CACHE_TTL__/$CACHE_TTL/; s/__FI50_TTL__/$FI50_TTL/; s/__FI25_TTL__/$FI25_TTL/" "$SITE"

ln -sf "$SITE" /etc/nginx/sites-enabled/relay
rm -f /etc/nginx/sites-enabled/default

# NB: an earlier "tile warmer" systemd service (blipscope-warm) was tried here and
# REMOVED 2026-07-22. It re-requested hot tiles every 3 s to keep them warm, but on a
# stale tile that adsb.lol was 429ing it retried every 3 s (429s aren't cached), which
# turned a transient 429 into a self-sustaining storm (~58% of upstream contacts 429).
# It was also redundant: foreground refresh already serves age~0 under sparse client
# polling, because the requesting client waits for the fetch. Do NOT reintroduce a
# warmer without an upstream rate limit / 429 backoff. If a box still has it, this run
# leaves it stopped; remove with: systemctl disable --now blipscope-warm.

# ---- firewall ---------------------------------------------------------------
# Allow SSH BEFORE enabling ufw so we can't lock ourselves out.
log "configuring ufw (22 + 443-from-Cloudflare)"
ufw allow 22/tcp >/dev/null
ufw --force enable >/dev/null
# Only Cloudflare may reach 443 (the relay is orange-clouded; direct hits are
# refused at L3, the X-Relay-Key gate is the L7 backstop).
for url in https://www.cloudflare.com/ips-v4 https://www.cloudflare.com/ips-v6; do
  while read -r range; do
    [ -n "$range" ] && ufw allow from "$range" to any port 443 proto tcp >/dev/null || true
  done < <(curl -fsS "$url" || true)
done

# ---- validate + reload ------------------------------------------------------
log "nginx -t"
nginx -t
systemctl reload nginx
# Belt-and-suspenders: ensure the removed warmer stays gone on re-run.
systemctl disable --now blipscope-warm >/dev/null 2>&1 || true
systemctl enable nginx >/dev/null 2>&1 || true

log "done. verify:"
log "  curl -s -o /dev/null -w '%{http_code}\\n' https://localhost/healthz --resolve localhost:443:127.0.0.1 -k   # 200"
log "  curl -s -o /dev/null -w '%{http_code}\\n' https://localhost/v2/lat/40/lon/-74/dist/50 --resolve localhost:443:127.0.0.1 -k   # 403 (no key)"
