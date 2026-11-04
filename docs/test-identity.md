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
