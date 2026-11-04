# A test identity for the production checks: the plan, its questions, and its predictions

Ruled by the reviewer on 2026-10-30, items 1-8 (recorded verbatim in `docs/basemap-deploy.md`). Plan only: nothing here is code yet, and the predictions below are frozen before any of it.

## Why

`smoke-prod.sh` and `watch-upstream.sh` sign in as the bench board E2:08. Every run:
- writes firmware changes into E2:08's `fw:` row;
- counts as E2:08's traffic in the metrics and its `dev:<id>` bucket;
- leaves E2:08's key on this PC.

A leak of that key can only be contained by revoking E2:08, because re-enrolment returns the same key (`docs/bench-key-rotation.md`, the 2026-08-31 correction). The old synthetic identity cannot come back: production refuses it as a fake id (since 2026-09-23, e4c768a). That is likely how the checks drifted onto a real board's key.

## The plan, item by item

**1. The id.**
- **Where it comes from:** the provision route derives the id, `SHA-256(MAC ‖ salt)[:8]` as 16 hex. That is the device-id shape, and it works on both environments.
- **The MAC is fresh and random,** made by the mint script with `crypto.randomBytes`, with the **locally-administered bit set and the multicast bit clear**. No real board has such a MAC, because every board's MAC is universally administered (an Espressif OUI). So the id is no board's.
- **The script refuses an id on the fake-id list** before minting (it computes the id locally first, with the same salt).
- **It lives only in the token file,** as `test-device`. The script prints booleans, labels and lengths only.
- **Server-side, by the route's existing behaviour:** `/blipscope/provision` writes a `prov:day` counter and an Analytics Engine `provision` point carrying the id, as it does for every bench mint. The id is in production's analytics, not in the repo.

**2. The mint: `POST /blipscope/provision`,** the bench route (`provision.ts`), through a new `proxy/scripts/mint-test-identity.ts`.
- **One MAC, both environments:** production first, then staging (`scopes-staging.valarsystems.com`).
- **The same MAC and salt give the same id on both.** Each returns its own key, because the device-key secrets differ.
- **The provision tokens** are read from `%USERPROFILE%\.config\valar-flasher\provision-token` (present on this PC) and `provision-token-staging` (absent; see Q-T1). They are sent as `fetch` headers, never on a command line.
- **What it checks:**
  - both answers' `deviceId` equals the id computed locally;
  - each key is 64 hex;
  - the two keys differ.
- **What it writes,** to the token file, in one write: `test-device`, `test-key` (production) and `test-key-staging`. It refuses if any of the three already exists.
- **The key never reaches a chat or a tool's output.** Nothing is printed but booleans, lengths and labels.
- **Its own check:** `GET /v1/config` as the test identity on each environment, with `fetch` headers, must answer 200 with `X-Blip-Auth: device`.
- **Daniel's part, at most two plain steps:**
  1. **Only if he holds staging's token** (Q-T1): put staging's `PROVISION_TOKEN`, from his password manager, into `%USERPROFILE%\.config\valar-flasher\provision-token-staging`.
  2. Nothing else: the script does the rest, and production's token is already on the PC.

**3. The checks run only as the test identity.**
- **Where the pair comes from:** `smoke-prod.sh` and `watch-upstream.sh` resolve it from the token file's `test-device` with `test-key`, or with `test-key-staging` when `BASE` is staging. They go through `token-file.ts` (new purposes) and a wrapper like #500's (`with-test-identity.mts`).
- **What they refuse, with exit 2 before their first request:**
  - **any `BLIP_KEY` or `BLIP_DEVICE` in the environment,** whatever its value;
  - **a test device equal to the token file's `records-device`** (E2:08's id, item 5), so E2:08's pair is refused even if it is written into the test fields.
- **They never read `blip-key` or `blip-device`.**

**4. It is not a unit.** Every counter that would count it leaves it out.
- **The dashboard:** `fleetRows`, `fleetTotals` ("Devices seen"), `firmwareSpread`, `seenDevices`, `enrolledButSilent` and `readLedger`, `computeTriage`, the funnel, and the usage rows.
- **`scripts/reconcile-fleet.py`,** where a `fw:` row with no CSV line is "KV ONLY: unaccounted-for unit" and exits 1.
- **How the id reaches them without entering the repo:**
  - the dashboard Worker reads a secret, **`TEST_DEVICE_IDS`** (Q-T2), set from the token file through stdin (`wrangler secret put`), never displayed;
  - `reconcile-fleet.py` reads `test-device` from the token file.
- **`usage-stats.ts` needs no exclusion:** it counts only usage points, and the checks send no `X-Blip-Usage`.

**5. E2:08's key leaves the PC.**
- **Today's readers of `blip-key` and `blip-device`,** every one needing both:
  - `blip-creds.sh` and `with-blip-creds.mts` (#500's wrapper);
  - `smoke-prod.sh` and `watch-upstream.sh`;
  - `ingest-routes.ts`, which puts the key on a curl command line at `:534`;
  - `ingest-airports.ts` (fetch headers).
- **Also a reader, from outside this repo's scripts:** blipscope-5c's production-record reader (its RP rows), which needs **only E2:08's id**, per the 2026-10-30 ruling.
- **After this PR:**
  - the checks move to the test identity (item 3);
  - so do both ingest scripts (Q-T3), with `ingest-routes.ts`'s key moved off its command line;
  - **no reader needs E2:08's key.**
- **E2:08's id stays,** under **`records-device`**: it identifies whose production records to read, and it is not a credential. The mint script can add that line from `blip-device` in place (`--add-records-device`), without displaying it.
- **The two lines Daniel removes himself:** `blip-key=` and `blip-device=`. **When:** after all three of these:
  - this PR is merged, and its server part is deployed and verified;
  - one `smoke-prod.sh` and one `watch-upstream.sh` run have passed **as the test identity** on production;
  - blipscope-5c's reader reads `records-device`. That is its change, asked through Daniel.

**6. The server-side part:** the dashboard Worker's exclusion code and its `TEST_DEVICE_IDS` secret.
- **It deploys staged, in a reserved slot,** and is verified afterwards: the dashboard's "Devices seen" reads the same before and after one smoke run as the test identity.
- **The proxy needs no change:** the provision route already exists.

**7. No credential on a command line.**
- **The change:** `smoke-prod.sh`'s and `watch-upstream.sh`'s `AUTH` array (`-H "X-Blip-Key: …"`) is replaced by `curl -K -`. The two header lines go in on stdin, built with the `printf` builtin, so no process ever holds them as an argument.
- **Also moved:** `ingest-routes.ts`'s shell curl moves to `fetch` headers.
- **The check:** during a run, a sampler reads every process's command line (`Win32_Process.CommandLine`) every 50 ms and compares it in memory with the key, printing only a boolean.

**8. Staging.** It mints through **the same route**, `/blipscope/provision` on staging, with staging's own `PROVISION_TOKEN`.
- **Its only human part** is getting that token onto this PC (item 2's step 1).
- **Staging's enrolment page** would need a human Turnstile solve, and shows the key on the page. It is not used.

## Questions, each with a recommendation

- **Q-T1, staging's `PROVISION_TOKEN`.**
  - **Recommended:** Daniel copies it from his password manager into `provision-token-staging` (one step).
  - **If he does not hold it:** a script generates a new one, writes it to that file and pipes it to `wrangler secret put PROVISION_TOKEN --env staging`, never displayed. That rotates a staging secret, so it needs his yes.
- **Q-T2, where the dashboard learns the id.**
  - **The constraint:** item 1 says the id "lives only in Daniel's token file". Item 4 needs the dashboard to know it, and the dashboard runs on Cloudflare.
  - **Recommended:** a dashboard Worker secret, `TEST_DEVICE_IDS`. It is not in the repo, not printed and not logged.
  - **The alternative, not recommended:** a keyed hash of the id. It still needs a secret, and adds a second derivation to get wrong.
- **Q-T3, the two ingest scripts.**
  - **Recommended:** they move to the test identity too, so nothing needs E2:08's key afterwards.
  - **The alternative:** leave them on `blip-key`. Then E2:08's key cannot leave the PC.
- **Q-T4, the field names:** `test-device`, `test-key`, `test-key-staging` and `records-device`.
  - **Recommended:** as named, with the mint script adding `records-device` from `blip-device` in place, so Daniel never copies an id by hand.

## The rows, frozen before the code

| row | what | predicted |
|---|---|---|
| **TI1** | `smoke-prod.sh` and `watch-upstream.sh` with E2:08's pair in `BLIP_KEY`/`BLIP_DEVICE`; a `curl` shim first on `PATH` counts invocations | **refused, exit 2; the shim counts 0** for each script |
| **TI1b** | the same scripts, with a scratch token file whose `test-device` equals its `records-device` | **refused, exit 2; 0 requests** |
| **S-TIGUARD** | the refusal removed | TI1 fails: the shim counts **> 0** |
| **TI2** | the scripts with only the test fields present | they resolve `test-device` with `test-key` (or `test-key-staging` for staging), as booleans against the file, and never read `blip-key` or `blip-device` |
| **TX1** | the dashboard's counters over a fixture with one id listed in `TEST_DEVICE_IDS` | each counter excludes it; "Devices seen" equals the fixture's real devices |
| **S-TXNOEXCL** | the exclusion removed | TX1 fails: **"Devices seen" rises by exactly one** |
| **TX2** | `reconcile-fleet.py`'s selftest over a fixture with the test id holding a `fw:` row and no CSV line | not "KV ONLY"; exit 0 |
| **S-TXRECON** | its exclusion removed | the fixture reports **one** "KV ONLY: unaccounted-for unit", exit 1 |
| **TI7** | a smoke run against a local stub (every route 200), with the argv sampler running | **false**: no process's command line holds the key |
| **TI7's control** | the old `AUTH`-array version, the same way | **true** |
| **TM1** | `mint-test-identity.ts` against a local stub of the provision route | writes exactly three fields; its whole output, scanned for the stub's key, finds it **0 times**; a second run **refuses** (the fields exist) |
| **TM2, the live mint** | production, then staging | both answers 200 with the locally computed id; keys 64 hex and different; `/v1/config` as the test identity **200 with `X-Blip-Auth: device`** on both |
| **TD1, the dashboard deploy** | staged, in a reserved slot | "Devices seen" is **the same** before and after one production smoke run as the test identity |

**Anything outside these rows:** stop and report, with no fix until a ruling. **The A2.16 re-check runs for every new row** before any full pass: each row's inputs are read, and every sabotage that touches them is checked.

## Rulings, verbatim (source: reviewer chat, pasted by Daniel, 2026-11-04 ~04:45Z)

> #513: the plan is accepted.
> - Q-T1: asked of Daniel. If he holds staging's PROVISION_TOKEN, he puts it into
>   %USERPROFILE%\.config\valar-flasher\provision-token-staging. If he answers no, the rotation is approved: staging
>   only, the new value written to that file and piped to `wrangler secret put` through stdin, never displayed.
> - Q-T2: yes, the dashboard Worker secret TEST_DEVICE_IDS, set from the token file through stdin.
> - Q-T3: yes, both ingest scripts move to the test identity, with ingest-routes.ts's key off its command line.
> - Q-T4: yes, the names as listed, with the mint script adding records-device in place.
> - Build and mint production first. Staging follows once Q-T1 is answered. Mark #513 ready when the code and its rows are
>   in.

(The same message approved #512, recorded in `docs/basemap-deploy.md`.)

**One refinement the ruled order forces, stated before any code.**
- **Why:** minting production first and staging later means staging must send **the same MAC**, to get the same id. So the MAC has to persist.
- **Where it persists:** the token file, as **`test-mac`**. It is not a credential, but with the public salt it derives the id, so it is kept exactly as the id is: only in the token file, never committed, printed or logged.
- **The mint script's two runs:**
  - **`--env production`** writes **three fields:** `test-mac`, `test-device` and `test-key`.
  - **`--env staging`** reads `test-mac`, checks that staging's answer carries `test-device`, and writes **one field**, `test-key-staging`.
- **`--add-records-device`** adds `records-device` from `blip-device` in place (Q-T4).
- **TM1 and TM2, restated:**

| row | what | predicted |
|---|---|---|
| **TM1** | `mint-test-identity.ts --env production` against a local stub of the provision route | writes exactly `test-mac`, `test-device` and `test-key`; its whole output, scanned for the stub's key, the MAC and the id, finds each **0 times**; a second run **refuses**, because the fields exist |
| **TM1s** | `--env staging` against the stub | writes exactly `test-key-staging`; **refuses** if the stub answers a different id from `test-device`; output scan **0 times** |
| **TM2** | the live mint, **production now**, staging once Q-T1 is answered | 200 with the locally computed id; the key is 64 hex; `/v1/config` as the test identity answers **200 with `X-Blip-Auth: device`**. On staging, also: the key differs from production's |

**One sequencing point, stated before the code:** TM2's `/v1/config` check runs **after TD1**.
- **Why:** an authenticated request as the new id writes it into the dashboard's request points. The mint itself is unauthenticated (the provision token, no device key), so it writes none.
- **So the order is:** mint, set `TEST_DEVICE_IDS`, deploy the dashboard's exclusion (TD1), and only then make the first authenticated request as the test identity. The id is never counted as a unit, even briefly.

## Q-T1 answered, verbatim (source: Daniel, 2026-11-04)

> For: map server, from Daniel: Q-T1 — I don't have staging's provision token. Make a new one as ruled (staging only, written to the file and set through stdin, never displayed).

**The rotation:** `proxy/scripts/rotate-staging-provision-token.sh`.
- **Staging only:** `--env staging` is fixed in the script, and nothing in it names production.
- **The value:** 32 random bytes as 64 hex, made by `node:crypto` and written by redirection straight into `%USERPROFILE%\.config\valar-flasher\provision-token-staging`. It refuses if that file already exists.
- **Setting it:** `wrangler secret put PROVISION_TOKEN --env staging`, with **stdin from the file**. The value is never an argument.
- **Its proof that nothing was displayed:** all of the run's captured output is scanned for the value, with the pattern read from the file.

| row | predicted |
|---|---|
| **R-T1** | the file holds **64** characters (the length is printed, never the value); `wrangler secret put` exits 0 and reports `PROVISION_TOKEN` uploaded to **`blipscope-proxy-staging`**; `wrangler secret list --env staging` lists `PROVISION_TOKEN` (names only); **the value appears 0 times** in everything captured |
| **R-T1's proof that it works** | TM2 on staging: the provision route answers **200** with this token |

## The code and its rows, run (2026-11-04)

**What the code is:**
- **`token-file.ts`:** the five new fields, `tokenFileKeys`, and **`resolveTestIdentity`**, the single refusal point.
- **`mint-test-identity.ts`,** with its selftest.
- **`test-identity.sh` and `with-test-identity.mts`:** the re-run, and the `curl` function that sends the headers on stdin.
- **The callers:** `smoke-prod.sh` and `watch-upstream.sh` run through those; **both ingests** use `resolveTestIdentity`, and `ingest-routes.ts`'s curl now reads its headers from stdin.
- **The dashboard's exclusion** (`testDeviceIds`, `notTestDevice`, in seven counters and both ledger readers).
- **`reconcile-fleet.py`'s exclusion.**
- **Removed:** `blip-creds.sh`, `with-blip-creds.mts`, `resolveBlipCreds` and its CI step. Nothing reads `blip-key` or `blip-device` any more.
- **CI runs all five selftests.**
- **The repo's device-id check** reads OK over the tree at HEAD (1,422 tracked files): every EXAMPLE id is built at run time. One 16-hex literal in the mint selftest's first commit was caught and removed.

| row | result |
|---|---|
| **R-T1** | **as predicted:** 64 characters; `wrangler secret put` exit 0, uploaded to `blipscope-proxy-staging`; listed; the value **0 times** in everything captured (04:49:04Z to 04:49:12Z) |
| **TM1** | **as predicted:** exactly `test-mac`, `test-device` and `test-key` written; the key, MAC and id in the output **0 times**; a second run refused |
| **TM1s** | **as predicted:** exactly `test-key-staging` written, 0 times in the output; a different id refused, with nothing written |
| **TM-R** | as predicted: `records-device` written equal to `blip-device`, the id 0 times in the output |
| **S-TMPRINT** | **as predicted:** a copy that prints the key is caught (1 time) |
| **TM2, the mint** | **as predicted.** Production (04:56:06Z): the route's id equals the Worker's own `deviceIdFromMac`, the key is 64 hex, three fields written. Staging (04:56:11Z): **200 with the rotated token** (R-T1's proof), the same id, a 64-hex key different from production's, `test-key-staging` written. `records-device` added. No value shown. **TM2's `/v1/config` check waits for TD1**, as recorded above |
| **TI1** | **as predicted:** `smoke-prod.sh` and `watch-upstream.sh`, with a board's pair in `BLIP_KEY`/`BLIP_DEVICE`: exit 2, **0 requests** counted |
| **TI1b** | **as predicted:** `test-device` equal to `records-device`: exit 2, 0 requests |
| **S-TIGUARD** | **as predicted:** the refusal removed, a request is sent, so TI1 would fail |
| **TI2** | **as predicted:** 37 requests to the stub; every `X-Blip-Device` is `test-device` and every `X-Blip-Key` is `test-key`; the board's pair **never** sent |
| **TI7** | **as predicted: false.** The key or id was on no process's command line across 71 samples, which saw a curl in 32 |
| **TI7's control** | **as predicted: true.** The old way, the key as a curl argument |
| **TX1** | **as predicted:** every counter leaves the `TEST_DEVICE_IDS` id out; "Devices seen" is 2. Its control: with no secret, 3. A malformed entry is refused, never quoted into the SQL |
| **S-TXNOEXCL** | **as predicted:** the exclusion removed, TX1 fails with **3 where 2 was expected**; its control still passes |
| **TX2** | **as predicted:** `kv-only=0`, consistent, exit 0; the test id never printed |
| **S-TXRECON** | **as predicted:** the exclusion removed, **`kv-only=1`**, exit 1 |

**Two of my own slips, recorded:**
- **The mint selftest deadlocked** on its first run. It blocked its own stub server with `spawnSync`. It was stopped, the hung process chain cleared, and it was rewritten async.
- **A `cd` into a directory I was already in** skipped one edit. It was caught by the count check, and nothing was changed until the re-run.

**What is left, and it waits for the merge:**
- **TD1:** deploy the dashboard with its exclusion and set `TEST_DEVICE_IDS` from the token file through stdin, staged, in a reserved slot. Verify "Devices seen" is the same before and after one production smoke as the test identity.
- **Then TM2's `/v1/config` check** on production and staging.
- **Then item 5's conditions** for Daniel's two lines.
