# What weather costs per board

**Read this first.** With weather on every cloud board, today's fleet costs about **$0.79 a month, under Daniel's $2**. The
$2 runs out somewhere between **50 and 84 boards**, depending on the weather and how busy the Worker is. At 600 boards it
is about **$29-46 a month**. Daniel's decision (2026-11-07 ~02:36Z): v19 ships with weather on by default, and `/wx` is
made cheaper before the fleet passes about 50 boards with weather on.

This is the first record of the cost. No earlier figure included `/wx`, because the layer was off by default. The
figures below were read on 2026-11-06 and 2026-11-07; the raw tools are read-only and printed no id, key or location.

## What one board costs a month

An enabled board polls `/wx` every 60 s (WD3 e): **43,200 requests in a 30-day month**. Radar frames arrive every 2
minutes, so half the polls find a new frame.

| | 200 (a new frame) | 304 (nothing new) | a board, a month |
|---|---|---|---|
| **Requests** | 21,600 | 21,600 | **43,200** |
| **CPU, each** | 12-51 ms (see below) | 2.27 ms mean | **0.31-1.16 M ms** |
| **R2 Class B, each** | 1 + up to 4 tile reads | 1 | **86,400-129,600** |
| **Billed once the free tier is gone** | | | $0.013 requests; $0.006-0.023 CPU; $0.031-0.047 R2 |

Nothing is edge-cached (`Cache-Control: no-cache`). **Every poll reads R2, even a 304**, because the server has to read
`latest` to know the current frame. Prices used: Workers requests $0.30/M over 10M; CPU $0.02/M ms over 30M; R2 Class B
$0.36/M over 10M. Class A does not move: `/wx` only reads.

**Why CPU is a range, and what each end is:**

| measured | CPU per 200 | what it is |
|---|---|---|
| production, one board, once a minute, 30 min, n = 15 | median **51 ms**, mean 51.4 | a real board's crop in an isolate that rarely runs |
| staging, 60 requests 2 s apart, KMCI, r = 120 | first 5 median 18 ms, last 30 median **12 ms** | a small, dry crop in a warm isolate |
| staging, 30 requests 2 s apart, the largest of 10 airports (KPDX, 14 KB), r = 300 | median **21 ms**, max 41 ms | the biggest crop found that hour, warm; the max is the first, cold |

So a cold isolate alone is not the 51 ms (cold on the small crop read 23 ms), and neither is crop size alone (21 ms). The
two together reach about 41-51 ms. **Crop size follows the weather:** the same hour read 36 B to 14,085 B across 10
airports, a factor of 391. A busy fleet keeps isolates warm, so at scale 12-21 ms is the likely figure, and 51 ms is the
figure for a small fleet in rain. Both are carried below. Sample sizes are small, and tile reads per 200 could not be
separated from the ingest's reads, so 2 and 4 are both carried.

## The account

The base is #587's: 52.2 M ms CPU, 4.80 M Class B, 0.93 M Class A, which is **$0.44 a month**, all of it CPU. Requests are
1.82 M a month at the last 7 days' rate. Active boards in those 7 days: **17**, 2 of them on `/wx`.

| a month, whole account | requests | CPU | R2 Class B | total |
|---|---|---|---|---|
| **Today, weather on all 17** (+15 boards) | 2.5 M: $0 | 69.6 M ms: $0.79 | 6.1-6.7 M: $0 | **$0.79** |
| **50 boards** | 4.0 M: $0 | $0.95-$1.60 | 9.1-11.3 M: $0-$0.46 | **$0.95-$2.06** |
| **600 boards, 200 at 51 ms** | 27.7 M: $5.32 | 748 M ms: $14.36 | 57-83 M: $16.79-$26.12 | **$36.47-$45.80** |
| **600 boards, 200 at 21 ms** | 27.7 M: $5.32 | 354 M ms: $6.48 | 57-83 M: $16.79-$26.12 | **$28.59-$37.92** |

**With the map and Europe:**
- **The map's wave (#587):** $0.08 at 600. Its "the allowance runs out at about 11,200 devices" counted the map alone, and
  `/wx` now sets that limit.
- **Europe's line as built** (`docs/wx-opera-europe.md` section 4): today's fleet goes to **$4.51-$4.74**. Europe's Class B adds
  4.3 M, so with weather on the account passes the free 10 M at about 7-10 boards instead of 40-60.
- **Held flat in every line above, and it should not be:** the proxy's other traffic, about 107,000 requests a board a
  month. At 600 boards that alone is 64 M requests, about $16 a month, with or without weather.
- **Not counted:** Analytics Engine writes one point per request. Check before quoting a figure at 600.

## The fixes, cheapest first

"$2 at" is the board count where the whole account reaches $2, for a 200 at 51 ms and at 21 ms.

| fix | changes | firmware? | $2 at (51 ms) | $2 at (21 ms) | at 600 |
|---|---|---|---|---|---|
| none | | | 50-64 | 61-84 | $29-46 |
| **F1: cache `latest`** | a poll no longer reads R2 | no | 64-68 | 84-134 | saves **$9.33**: $19-36 |
| **F2: poll every 120 s** | half the requests, no 304s | yes | 57-71 | 72-106 | saves about $9: $19-37 |
| **F3: serve every second frame** | half the 200s, so half the CPU and tile reads | no | 79-96 | 93-117 | $22-30 |
| **F1 + F3** | both | no | 122-124 | 160-212 | $12-21 |

**The cheapest is F1:** about ten lines in `proxy/src/wx/handler.ts`, no firmware, nothing an owner can see. It removes
43,200 Class B reads a board a month, a third to a half of the board's R2 line. **Its limit:** the saving above is a best
case. `latest` has to be held longer than one poll to help a board alone in a colo, so the Cache API (shared per colo) is
the likelier home than isolate memory, and the hit rate needs measuring. And while a 200 costs 51 ms, CPU is the binding
line, so F1 alone moves the $2 point only a little.

**F3 is the biggest single lever and it is visible:** weather refreshes every 4 minutes instead of 2. That is Daniel's
call, not an engineering default. F2 needs an OTA and changes a frozen property (WD3 e), so it is the last resort.

None of this reaches $2 at 600, and nothing here tries to. The $2 is an allowance for today's fleet. At 600 this is
$0.05-0.08 a board a month, and whether that needs a new allowance is a separate question for Daniel.

## What happens next (Daniel, ~02:36Z)

- **Read the cloud-board count at the cut** (`scripts/reconcile-fleet.py`) **and weekly after.**
- **If the count at the cut is over 40, say so before the cut.**
- **The fix is proposed from this record, predictions first, so it lands before the count passes 50.**
- **Anything over $2 a month before the fix lands is Daniel's call,** as already ruled.

## Instruments

- `/wx` has no line in Analytics Engine: it buckets into the mixed `/other` until the `KNOWN_ROUTES` PR is merged and
  deployed. Until then, read it from Workers Logs.
- The account's own 30-day reads came in lower than the base used here (CPU 20.2 M against 52.2 M, Class B 3.11 M against
  4.80 M), because the ingest has changed within the window. The higher base is used so the verdict errs high.
