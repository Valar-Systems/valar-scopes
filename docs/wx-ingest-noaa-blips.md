# The weather ingest rides out NOAA blips (predictions first)

**Asked by the reviewer (reviewer chat, pasted by Daniel, 2026-10-23):** "With 4 of 15 runs failing, the ingest needs
to ride out NOAA blips before the weather-on soak. Propose a PR, predictions first, with three parts:
- a short timeout on each listing attempt, and a few attempts per run;
- backfilling any frame in the 20-minute listing window that the bucket lacks, so a blip doesn't leave a hole in the
  loop;
- logging: a run that succeeds after a retry logs wx_ingest with its attempt count, and wx_ingest_error means every
  attempt failed.
Once it's deployed, I5″'s 70-minute window restarts on the new code; I5″'s predictions stay unchanged."

## What was measured (2026-10-23)

- **The failures:** `NOAA list 520`, `522` and `525`, `NOAA get 522` and `525`, and "Network connection lost", each
  after **24-288 s**. These are Cloudflare's codes for a subrequest that cannot connect to, or finish TLS with, the
  origin (AWS S3).
- **Their rate:** 25 `wx_ingest_error` against 19 `wx_ingest` from 15:15 to 15:40Z, and 4 of 15 runs from 16:09 to
  16:24Z.
- **The holes they leave:** NOAA published 30 frames from 14:22:34 to 15:20:33, and the bucket held 29. The ingest
  stores only the newest frame on each check, so a frame is lost whenever no check succeeds while it is the newest.

## The change (`proxy/src/wx/ingest.ts`, `ingest-worker.ts`)

**1. Attempts and timeouts.** Every NOAA request (the listing, and the frame download, which failed the same way)
goes through one wrapper. An attempt is aborted at its timeout, and a failed attempt is retried after a short wait. A
4xx answer is final and is not retried.

| | timeout per attempt | attempts | waits between | worst case |
|---|---|---|---|---|
| listing | 8 s | 3 | 1 s, then 2 s | 27 s |
| frame download (about 13 MB) | 20 s | 2 | 1 s | 41 s |

**The download is an addition the ruling did not name.** The tails saw `NOAA get 522/525` from the same failure. It
is listed here so it can be struck.

**2. Backfill, one build per check.** A check lists every frame NOAA has in the 20-minute window, newest first, and
builds **the newest one the bucket lacks**: the newest frame if it is new, otherwise the newest hole. **At most one
frame is built per check**, so an invocation (two checks) builds at most two, the same worst case as today. That
matters because `cpu_ms = 5000` covers the whole invocation, and a build costs about 1-2 s.
- **How fast holes fill:** two checks a minute against one new NOAA frame every two minutes.
- **What it costs:** up to about 10 `HEAD`s per check (R2 Class B, about 0.9M a month, against 10M included). No new
  Class A operation: a backfilled frame is a put that would have happened had it not been missed.

**3. Logging.**
- `wx_ingest` gains **`attempts: {list, get}`**, and a status **`backfilled`** for a hole filled.
- **`wx_ingest_error` is logged only when every attempt failed**, with the attempt count.

## Predictions

**Host, `proxy/test/wx-ingest.test.ts`, against the R2 emulator, using fake NOAA answers. Short timeouts are
injected, so the tests take milliseconds.**

| | check | predicted |
|---|---|---|
| **N1** retry | the listing answers 522, then succeeds | `stored`, `attempts.list` **2**. *Sabotage:* attempts set to 1, and the check throws |
| **N2** timeout | the first listing attempt never answers | aborted at its timeout; the second attempt succeeds; `stored`. The check ends within timeout + wait + 1 s. *Sabotage:* the abort signal not passed, and the check does not end (the test's time bound fails) |
| **N3** every attempt fails | the listing answers 525 three times | the check throws, saying 3 attempts; the worker logs one `wx_ingest_error` with `attempts` 3 |
| **N4** 4xx is final | the listing answers 403 | the check throws after **1** attempt |
| **N5** backfill | NOAA lists F1, F2 and F3; the bucket has F1 and F3 | `backfilled`, F2 stored, `latest` unchanged at F3. The next check reports `already-stored`. *Sabotage:* newest-only selection, and F2 is never stored |
| **N6** one build per check | the bucket is empty; NOAA lists 5 frames | check 1 builds only the newest (1 download); check 2 builds the next newest (1 download) |
| **N7** download retry | the frame download answers 522, then succeeds | `stored`, `attempts.get` 2 |
| **N8** the existing tests | WS5 (a)-(d) and the rest of the proxy suite | all pass. **Two are modified, named here:** WS5 (a) and (b) list an older frame next to the newest, so their second run now **backfills** it where it reported `already-stored`. (a)'s claim, that the same frame is written once, is unchanged. Nothing else changes, and `tsc` is clean |

**Live, after it is reviewed, merged and deployed** (the ingest Worker only, from clean main, under the standing rule):

| | check | predicted |
|---|---|---|
| **NL1** complete | **ground truth:** a bucket listing against NOAA's own listing, for the frames published from deploy + 5 min to deploy + 65 min (read after deploy + 70 min) | **every NOAA frame in that span is in the bucket** |
| **NL2** logging | the tail and Workers Logs, from the deploy | `wx_ingest` lines carry `attempts`. Any `wx_ingest_error` carries `attempts` equal to the maximum |
| **NL3** CPU | `workersInvocationsAdaptive`, the first 2 h | **0** errors, and `cpuTimeP99` below 5,000 ms |
| **I5″** | as frozen | its 70-minute window restarts on the new code, and its predictions are unchanged |

**Outside the set:** stop and report. In production, roll the ingest back to its current version, `47ad5010`, first.
