# Activation allowlist: only boards Daniel sold can activate

**Status: predictions frozen before any code.** A server-side follow-up to #545 (the factory
image plan), ruled 2026-11-05 ~17:35Z. It changes nothing in the factory image or in any firmware.

Today any board that solves the Turnstile challenge on the enrol page gets its device key
(`docs/device-enrollment.md`). After this change, a board activates only if its device id is on
an allowlist on the server. A board that is not on the list is refused with a message it can act
on, and it can still use its owner's own receiver. The first activation of an id wins; a second
one needs Daniel to reset that id.

## The design

### Where the list lives

- **One KV row per allowed id, in the fleet KV (`ENRICH_KV`):** `act:<id>`, holding
  `{ v: 1, state: "listed" | "activated", listedAt, activatedAt?, resets }`.
  - The id is on the list if and only if its row exists.
  - `activated` means the one permitted activation has been used.
- **The list never appears in the repo.** It is built from three sources:
  - the factory's per-unit MAC list or its flash logs (ids derived on the server);
  - Daniel's records: the pilot units, his stock boards, and any board he flashes at home;
  - ids Daniel adds one at a time.
- **The enrolment ledger (`enr:dev:<id>`) stays the volume log.** It records every mint, as
  before, and the gate never reads it.

### The enrol route (`POST /blipscope/enroll`)

The checks run in this order. Only the step marked **new** is added:

1. method, configuration, a well-formed id
2. the fake-id guard (production): before Turnstile, as today
3. a token is present
4. **the Turnstile solve, verified by the server**
5. revocation
6. **new: the activation gate**
   - no row: **403** `{ error: "not_listed", id, support }`
   - row `activated`: **409** `{ error: "already_activated", id, support }`
   - row `listed`: write `activated` first, then continue to step 7
7. derive the key, update the ledger and the day counter, log the mint (unchanged)

**Success is always `status: "enrolled"`.** `already_enrolled` can no longer occur, because a
repeat activation is refused.

**Why the gate comes after the solve.** If it came first, anyone could find out whether an id is
on the list without solving the challenge, which makes the endpoint an oracle for which ids belong
to real units. After the solve, each answer costs one challenge.

**Why it comes after revocation.** A revoked id is refused as revoked, whether or not it is on
the list. It never learns its list state, and it never uses up its activation.

`support` is `support@valarsystems.com`. That is the address the support page and the hub already
publish, and both the route and the page take it from one constant (`src/support.ts`).

### The enrol page

The page now handles two new refusals.

**`not_listed`** says that the board is not registered as a Valar unit. It shows:
- the device id;
- the support address;
- an email link (`mailto:`) whose subject and body are pre-filled with that id, so a stuck
  customer can send it in one tap;
- a line saying the board can still show aircraft from the owner's own receiver
  (dump1090/readsb) or their OpenSky account.

**`already_activated`** says the board has already been activated. It shows the same id, address
and email link, and says that a board that was fully erased can be re-opened by support.

### The admin route and the tool

**`POST /blipscope/activation`** sits beside the bench mint and uses the same credential:
`X-Provision-Token`, checked against `PROVISION_TOKEN` in constant time, and refused (fails
closed) when the secret is unset.
- The body is `{ op: "add" | "reset" | "status", ids?: [...], macs?: [...] }`.
  - Ids must be exactly 16 lowercase hex characters.
  - MACs are normalised and turned into ids with the mint's own derivation (`deviceIdFromMac`).
- **At most 250 entries per call.** That keeps a call inside the Worker's limit of KV operations
  per invocation. The tool splits larger lists.
- **One malformed entry refuses the whole call before anything is written.** The response never
  repeats the entry.
- **Responses contain counts or a state, never an id:**
  - `add`: `{ listed, already_listed, already_activated }`. `add` never re-opens an activated id.
  - `reset`: `{ reopened, already_open, not_listed }`. `reset` never puts an id on the list.
  - `status` (one id only): `{ state: "not_listed" | "listed" | "activated" }`.
- **Why the bench token, and not a new secret.** That token can already mint a key for any MAC
  directly, which is a larger power than editing this list. A new secret would add a second
  credential for no reduction in what a leak exposes.

**`proxy/scripts/activation.ts`** is Daniel's one-liner:

```
npx tsx scripts/activation.ts add <16-hex id> [--staging]
npx tsx scripts/activation.ts reset <16-hex id> [--staging]
npx tsx scripts/activation.ts status <16-hex id> [--staging]
npx tsx scripts/activation.ts import <file> [--staging]   # MACs, one per line, or esptool logs
```

- It reads the token from `~/.config/valar-flasher/provision-token`, or `provision-token-staging`
  with `--staging`. That is the file `mint-test-identity.ts` and the bench already read, so the
  secret has one copy on disk, not two.
- The token goes in a request header, never on a command line.
- **It prints counts and states, never an id,** including the ids it read from a file. A line in
  an import file that is not a MAC is reported by its line number only.

## Facts found while designing, for the reviewer

1. **Enrolment has no numeric cap today.**
   - The per-IP limiter runs only past the device-auth gate (`src/index.ts:332`), and
     `/blipscope/enroll` is dispatched above it.
   - The route has no daily ceiling.
   - Each mint costs exactly one Turnstile solve, and the volume is logged (`enr:day:`).
   - The 120-a-day cap belongs to the bench mint (`/blipscope/provision`), not to enrolment.

   So "the caps holding" can only be tested as: the Turnstile gate, the fake-id guard,
   revocation, and the bench mint's cap. **The accepted risk, guessing a real unit's
   near-consecutive id, therefore costs one challenge per guess, not a fixed number of guesses.**
   If a numeric limit on activations is wanted, it is a separate change and needs its own ruling.
2. **First-wins removes how a device recovers from a rotation.**
   - Today a repeat enrolment re-derives the key from the current secret
     (`src/enroll.ts:175-192`, pinned by the test "after a SECRET ROTATION, already_enrolled
     carries a NEW key"). That is how a device recovers after `DEVICE_KEY_SECRET` is rotated.
   - With this change a repeat is refused, so a rotation needs a reset for each device first.
   - The test is restated so the property is still pinned: a reset, then an activation under the
     rotated secret, returns the key derived from that new secret.
3. **Only a USB full erase makes a customer need a reset.**
   - A device's own factory reset keeps the key (`src/FactoryReset.cpp`, `PRESERVE_KEY =
     "cloud-key-fac"`).
   - Switching editions by OTA keeps NVS.
   - A USB full erase loses it, so only that customer needs a reset.
4. **Deploy order.** The gate and the admin route arrive in one deploy, and the list is empty at
   that moment. Any activation between the deploy and the first import is refused, showing the
   support message, and succeeds once the id is added. The import should run immediately after
   the deploy. If an off switch is wanted instead, that needs a ruling.
5. **KV is eventually consistent.** Two activations of one listed id from two locations inside
   KV's propagation window (up to about 60 s) can both succeed. That falls under the same remedy
   as the accepted risk, a reset or a revocation. Making it strict would need a Durable Object;
   that is not proposed.
6. **Boards that enrolled before this change.**
   - Their keys keep working, because device auth is not touched.
   - A re-activation is refused as `not_listed` until Daniel adds them.
   - They have ledger rows but no `act:` row. The gate does not read the ledger, by design.

**The known limit, accepted (ruling).** The firmware is public, and the MACs in one batch are
nearly consecutive. Anyone who knows one unit's MAC can derive its neighbours' ids, so a
determined person could guess a real unit's id and activate it before its owner. The remedy is
the reset, which re-opens the id for its owner, and revocation, which kills a key. Fact 1 states
what one guess costs.

**Not adopted (ruling):** making ids live only when their order ships. It needs per-unit tracking
at packing.

## Predictions (frozen before any code; the suite is run only after the code is written)

### S-0: the existing tests against the change, before any of them is adapted

The full change (route, page, admin route, tool) is run against the **unmodified** existing
tests. These are predicted red, and exactly these:

| file | case | why |
|---|---|---|
| `enroll.test.ts` | issues the derived key when the challenge verifies | the id is not listed: 403 |
| `enroll.test.ts` | returns the SAME key and says already_enrolled | the first activation is already 403 |
| `enroll.test.ts` | after a SECRET ROTATION, already_enrolled carries a NEW key | as above |
| `enroll.test.ts` | logs the repeat in the ledger rather than blocking it | no ledger row is written for a refusal |
| `enroll.test.ts` | counts enrollment volume, which is the only abuse signal we get | the counter is never bumped |
| `fakeids.test.ts` | CONTROL production: a real-shaped id is NOT refused by the guard | expects not-403; gets `not_listed` 403 |
| `fakeids.test.ts` | staging: a fake id is not refused by the guard | as above |

**Seven red.** Every other case stays green, including the four in `enrollpage.test.ts`.
- **"CONTROL: without a rotation the repeat key is unchanged" stays green only vacuously:** both
  keys are `undefined`. It is named here so that it gets rewritten rather than read as evidence.
- **Any other red, or any of the seven going green, is outside the set:** stop and report.

The seven, and the vacuous control, are then adapted: the id is listed first, and the
repeat-enrolment cases become the A3/A4 behaviour.

### The new checks, each with its sabotage

Each sabotage is planted in a copy, asserted to have applied (the bytes changed), run, and then
restored by bytes with a sha check. **A sabotage that changes no result did not apply, and is
itself a stop.**

| id | the check | the sabotage | predicted under the sabotage |
|---|---|---|---|
| **A1** | an unlisted id with a good solve gets 403 `not_listed`, `body.id` = the id, `body.support` = the address, and no key; no `act:` row and no ledger row are written; the day counter does not move | the gate's refusal skipped | A1 red (200 with a key) |
| **A2** | the page, run as a script: a `not_listed` 403 renders the id, the address, a `mailto:` whose decoded subject and body both contain the id, and the own-receiver line. CONTROL: `already_activated` renders its own message, also carrying the id | the page drops the id from the `mailto:` body | A2 red |
| **A3** | `add`, then activate: 200 and key = `deriveDeviceKey(SECRET, id)`, row `activated`; a second activation: 409 `already_activated` with the id and address, and no key | the `activated` write skipped | A3 red (the second activation gets 200) |
| **A4** | after A3: `reset`, then activate: 200; again: 409. Rotation restated: a reset, then activation under a rotated secret, returns a key ≠ the first and = `deriveDeviceKey(rotated, id)` | `reset` made a no-op | A4 red |
| **A5** | **no oracle.** An unlisted id with a failed solve gets 403 `unverified`, not `not_listed`; a listed id with a failed solve gets 403 `unverified` and its row stays `listed` | the gate moved above the solve | A5 red |
| **A6** | **the caps hold:** a revoked listed id gets 403 `revoked` and its row stays `listed`; in production a listed fake id gets 403 `fake_device_id` with **0** siteverify calls; the bench mint still refuses at 120 (the existing provision tests, unchanged) | the `activated` write moved above the revocation check | A6 red (the row reads `activated`) |
| **A7** | the admin route: a missing or wrong token gets 403 `forbidden` and nothing is written; an unset `PROVISION_TOKEN` gets 403 | the token check skipped | A7 red |
| **A8** | admin responses carry no id: the response text contains none of the submitted ids and no 16-hex run. CONTROL: it carries the counts | the response echoes its ids | A8 red |
| **A9** | `add` by MAC: for each fixture MAC in `test/fixtures/mint-equivalence.json`, `add` gives `listed`. That is checked on the id whose sha256 is the one the fixture records (the fixture stores no raw id), and that id's `status` is `listed` | the salt dropped from the derivation | A9 red |
| **A10** | `add` on an activated id leaves it `activated` (counted `already_activated`); `reset` on an unlisted id leaves no row (counted `not_listed`) | `add` overwrites | A10 red |
| **A11** | 251 entries get 400; one malformed entry among good ones gets 400, the response does not repeat it, and **none** of the good ones is written | each entry written as it is validated | A11 red (a partial write) |
| **A12** | the tool's pure parts: its summary lines contain no 16-hex run (CONTROL: they carry the counts); `parseImport` finds MACs in a plain list and in esptool `MAC:` lines, and names a bad line by its number only | the summary includes the ids | A12 red |

**Typecheck:** `npm run typecheck` and `npm run typecheck:scripts` both exit 0.

**Suite total:** the baseline is read from `vitest list` at this branch's base, `975816d`, before
the change, not taken from an earlier run. The total is then baseline + the new cases, with the
adapted cases neither added nor lost. **The run must be all green, under the port-exhaustion gate
(TIME_WAIT under 8,000 at the start).**

**Outside the set.** Any result not in the tables above, or a suite total that is not
baseline + new, means stop: report it, and fix nothing until it has been ruled on.

**No deploy from this session.** The route ships with a deploy that the deploy authority decides
on, followed straight away by the import (fact 4).

## Results

### S-0, 2026-11-05 17:57Z: off-prediction (one extra red), waiting for a ruling

The run used the full change (route, page, admin route, tool) with the existing tests unmodified:
901 tests, 892 passed, 8 failed, 1 skipped.

- **The seven predicted reds failed exactly as named:** five in `enroll.test.ts` and two in
  `fakeids.test.ts`.
- **An eighth red is outside the set:** `photolicense.test.ts`, "Q-C1: equals the render of the
  committed manifest".

**Diagnosis of the eighth red, read-only.** This worktree checks files out with
`core.autocrlf=true`.
- `proxy/photos/credits.html` is 105,991 bytes on disk, with 291 CRLF line endings.
- The committed blob is 105,700 bytes, with 291 LF and no CR.
- 105,991 - 105,700 = 291: the difference is exactly the carriage returns.
- Q-C1 compares that file on disk with the render byte for byte.

**The alternative, that the change broke Q-C1, is excluded** for two reasons:
- The change touches no file that the render or Q-C1 reads (`src/enroll.ts`,
  `src/enrollpage.ts`, `src/index.ts`, `src/provision.ts`, plus new files).
- The line endings account for the whole byte difference.

CI checks the file out with LF. **Not fixed; it waits for a ruling.**

**The baseline, corrected.** I recorded the baseline above as 1040, but that was the line count
of `vitest list`'s output, which includes 140 lines of runtime warnings. The list holds **900**
tests. The run counted 901: the one extra is `basemap-md2.test.ts`'s conditionally skipped case,
which the run reports and the list leaves out. Comparisons stay list against list.

### The next run's predictions (frozen 2026-11-05, before it)

- **The list.** `vitest list` at head shows **930** tests: 900 plus 30 new.
  - 20 in `activation.test.ts`: A7's loop makes three.
  - 8 in `activation-lib.test.ts`.
  - 2 for A2 in `enrollpage.test.ts`.

  The seven adapted cases are rewritten in place: none is added and none is lost.
- **The run.** Every case is green **except Q-C1**, which stays red in this CRLF checkout for the
  reason above, unless a ruling changes what this run should include.
- **Then each sabotage** A1-A12 turns red exactly the check its row names, and the restore by
  bytes returns the run to the line above.

### The next run, graded (2026-11-05 18:13Z): as predicted

- **The list:** `vitest list` read **930**.
- **The run:** 931 = 929 passed, 1 skipped (`basemap-md2`'s conditional case), and 1 failed: **Q-C1
  only**, the CRLF artifact, recorded as a miss with its cause named (ruling ~18:15Z).
- **Two attempts before it were void as instruments and are not results:**
  - The first crashed with `ECONNRESET` while shutting down and reported 71 files with no test
    results. It ran straight after a `vitest list` that was still releasing its runtimes.
  - The second I started at TIME_WAIT **15,626**, over the frozen gate. That is my slip, the second
    one today, so the gate is now a script that refuses to start above 8,000 instead of a habit.

### The sabotages' red sets, evaluated test by test before any of them runs

The row-level rule above, "each sabotage turns red exactly the check its row names", is
**incomplete for four plants.** I found that by applying each plant to every test in the six files
it can reach, before running anything: S-A3, S-A5, S-A8 and S-A11 also break tests in other rows
that rely on the same property. The sets below replace that sentence for grading. **Each is the
exact set of failing tests**, by file and title, across `activation`, `activation-lib`, `enroll`,
`enrollpage`, `fakeids` and `provision`. A control with no plant must run all green first.

| plant (where) | predicted red, exactly |
|---|---|
| **S-A1**: only the `not_listed` refusal skipped (`enroll.ts`) | A1 "403 not_listed, no key, and nothing written" |
| **S-A2**: the id removed from the `mailto:` body (`enrollpage.ts`) | A2's two cases: "not_listed: the id, ..." and "CONTROL already_activated: ..." |
| **S-A3**: the `activated` write skipped (`activation.ts` `claimActivation`) | A3 "the first activation gets the derived key; ..."; A4 "reset re-opens exactly one more activation" (the reset then finds a `listed` row); A10 "add on an activated id leaves it activated"; `enroll` "refuses the repeat, and after a reset returns the SAME key" |
| **S-A4**: the reset's write skipped | A4 "reset re-opens exactly one more activation"; A4 "after a SECRET ROTATION, reset + activation ..." |
| **S-A5**: the gate moved above the solve (and so above revocation) | A1 (siteverify is never called); A5's two cases; A6 "a revoked listed id ..."; `enroll` "REFUSES when the solve does not verify — the oracle test"; `enroll` "refuses to enroll a revoked id, and issues no key" |
| **S-A6**: the claim moved above the revocation check | A6 "a revoked listed id is refused as revoked and keeps its activation unused" |
| **S-A7**: the admin token check removed | A7's three token cases (missing, wrong, unset on the Worker) |
| **S-A8**: the `add` answer echoes its ids | A8 "add, reset and status answer in counts or a state"; A10 "add on an activated id leaves it activated" (its exact-body check) |
| **S-A9**: the salt dropped from the admin route's MAC derivation | A9 "each fixture MAC lists the id whose sha256 the fixture records" |
| **S-A10**: `add` overwrites an activated row | A10 "add on an activated id leaves it activated" |
| **S-A11**: `add` writes each id as it is validated | A3 "the first activation gets ..." (its `listed` count reads 0); A8; A10 "add on an activated id ..."; A11 "one malformed entry refuses the call, ..." |
| **S-A12**: `summarize`'s add line appends an id (`activation-lib.ts`) | A12 "add, reset and status lines carry no id even when the answer does" |

Every plant is asserted to have applied, run against those six files under the port gate, and
restored by bytes with a sha check. **A set that differs in either direction is a stop.**

## The reviewer's ruling of ~18:15Z: frozen before its code

**Ruled:**
- There is no activation cap, and none is added. The allowlist plus one Turnstile per guess is the
  gate.
- Refused activations are counted, so a guessing pattern would be visible; a cap comes later only
  if that count calls for one.
- "Activated" is scoped to the key generation, so a rotation re-opens every listed id once without
  manual resets.
- The tool's credential file is accepted.

### G: activation scoped to the key generation

- **The generation.** The row records `gen` when it is activated: the first 12 hex characters of
  `HMAC-SHA256(DEVICE_KEY_SECRET, "activation-gen")`. It is a keyed fingerprint of the current
  secret, never the secret and never a plain hash of it.
- **The rule.** An `activated` row refuses only when its `gen` equals the current one. A row
  activated under an earlier secret activates once more, and is rewritten with the new `gen`. A
  row with no `gen` refuses (fail closed); nothing deployed writes one.
- **`status` reports the effective state:** `listed` for a row activated under an earlier
  generation.
- **The known limit grows by one line.** After a rotation, every listed id is open once, so a
  guesser can race the owner for that one activation. The remedy is the same: a reset, or
  revocation.

| | check | sabotage |
|---|---|---|
| **G1** | activate under secret A: 200; again under A: 409; under rotated B: 200 with key = `deriveDeviceKey(B, id)`; again under B: 409. The row's `gen` is 12 hex and changes with the secret | **S-G1**: `gen` ignored, so an `activated` row always refuses |
| **G2** | `status`: an id activated under A reads `activated` asked under A, and `listed` asked under B | (S-G1) |

**S-G1 predicted red, exactly:** G1 and G2. A4's rotation case goes through a reset, so it stays
green.

### R: refused activations counted, and read where people already look

- **The counter.** Each refusal by the gate bumps `enr:refused:<yyyy-mm-dd>:<reason>`, where the
  reason is `not_listed` or `already_activated`. It is best effort, like `enr:day:`, with the same
  40-day TTL, and it never carries an id.
  - A refusal before the gate (no solve, revoked, a fake id) is not an activation refusal and
    bumps nothing.
  - A mint bumps only `enr:day:`, as before.
- **The reader.** `scripts/reconcile-fleet.py` prints refused activations per day beside
  enrolments per day. That tool is run whenever anyone asks about the fleet, so the count appears
  without anyone choosing to look for it.

| | check | sabotage |
|---|---|---|
| **R1** | one `not_listed` refusal and one `already_activated` refusal make exactly `1` and `1` under today's keys; a mint and an `unverified` solve add nothing | **S-R1**: the bump removed |
| **R2** | no key or value under `enr:refused:` contains the id; every value is an integer | (S-R1's control: R2 stays green) |

**S-R1 predicted red, exactly:** R1.

**The reader against production, read-only, now.** Nothing deployed writes these counters yet, so
the predicted output is the stated absence: no `enr:refused:` counters, with KV shown readable by
the `fw:` listing above it. The reconcile's other sections print as they always do.

**Counts.** 4 new tests (G1, G2, R1, R2), so `vitest list` reads **934**. The run is all green
except Q-C1, until the `.gitattributes` fix is on this branch's base.

### The seeding (deploy order): waiting on Daniel

The ruling seeds the list from Daniel's bench records before the deploy. Those are provisioning
records, which this session may not read. The plan, for Daniel's answer:
- **(i)** a seed script **Daniel** runs on his records, printing counts only, with the count he
  expects stated before it runs;
- **(ii)** a production coverage check this session can run: every device id already seen
  authenticating in production (`fw:` rows) and every id in the enrolment ledger (`enr:dev:`) must
  have an `act:` row. It prints counts only (seen, listed, missing), and missing must be 0 before
  the deploy.

Predictions for both are frozen once Daniel answers.

### A1-A12's sabotages, graded (2026-11-05 18:19Z): all twelve as predicted

The control (no plant) ran 82 tests across the six files, all green. Each plant turned red exactly
its frozen set:
- **1 test:** S-A1, S-A6, S-A9, S-A10, S-A12.
- **2 tests:** S-A2, S-A4, S-A8.
- **3 tests:** S-A7.
- **4 tests:** S-A3, S-A11.
- **6 tests:** S-A5.

Every file was restored by bytes with its sha checked, and the tree was clean afterwards.

### A correction to R's reader check, made before running it

**R said** "the reader against production, read-only, now". That cannot be run by this session.
`scripts/reconcile-fleet.py` reads `provisioned.csv` (the `--log` default) **before** it prints
any instrument, and stops if the file is absent. That file is a provisioning record, which this
session does not read. It is absent from this worktree, so the tool would stop before the new
block.

**The check is replaced by a harness** that loads the script, stubs its two KV reads
(`kv_list_names`, `kv_get_raw`) and calls `report_instruments` directly, with no production read
at all:

| | stub | predicted lines in the refused section |
|---|---|---|
| **H1** | no `enr:refused:` keys | the stated absence (`no enr:refused: counters at all ...`) |
| **H2** | `2026-11-05:not_listed`=3, `2026-11-05:already_activated`=1, `2026-11-06:not_listed`=2 | `2026-11-05  not_listed      3  already_activated      1` and `2026-11-06  not_listed      2  already_activated      0`, in that order |
| **H3** | one value unreadable (`None`) | `?` in its column |
| **S-H** | the new block removed | H1, H2 and H3 all red, because the heading is gone |

The real run against production happens on Daniel's machine, where the record exists, as part of
the ordinary reconcile.

### G and R, graded on `869c556` (2026-11-05 18:24-18:29Z): all as predicted

- **The run:** 933 passed, 1 skipped (`basemap-md2`), 1 failed (Q-C1 only, the CRLF artifact).
- **The list:** `vitest list` read **934**, from a drained start (TIME_WAIT 73). One earlier list
  returned no tests at all: it started at TIME_WAIT above 15,000 straight after the sabotage runs.
  That was an instrument failure, not a count.
- **S-G1** (`gen` ignored): red exactly {G1, G2}, from a control of 86 all green.
- **S-R1** (the bump removed): red exactly {R1}.
- **The reconcile reader:** H1-H3 pass. Under S-H (the block removed) all three fail.
- Every planted file was restored by bytes, and the tree was clean afterwards.

## Seeding before the deploy (ruled ~18:45Z): frozen before its code

**The order:**
1. Daniel seeds the list from his bench records with the tool.
2. This session runs the production coverage check as the control. **Any device id that has
   authenticated in production in the last 30 days and has no row stops the deploy.**
3. The factory's list is imported with the same tool when it arrives.

### The seed: `activation.ts seed`, run by Daniel

The admin route does not exist in production until #548 deploys, so the seed writes KV directly
with `wrangler kv bulk put`, under Daniel's own `wrangler login` session.
- **Input.** It reads a CSV with a `device_id` column (the shape `provisioned.csv` has), a plain
  list of 16-hex ids, or MACs and esptool logs (ids derived with the mint's own function).
- **It never rewrites an existing row.** A re-run, or a seed after an activation, leaves every
  existing `act:` row alone.
- **It writes, verifies, repairs and verifies again,** because a bulk put can drop keys
  silently. That is CLAUDE.md's practice, and a dropped key here is a customer refused.
- **It prints counts only:** ids read, already present, written, repaired, missing after repair.
  The id file it hands to wrangler is a temporary file, deleted afterwards. Exit 0 complete,
  1 incomplete, 2 usage.

| | check (in `activation-lib`, with an in-memory store) | sabotage |
|---|---|---|
| **K1** | CSV mode: ids come from the `device_id` column; a row whose `device_id` is not 16 hex is reported by line number; other columns are ignored | |
| **K2** | list mode (16 hex per line) and MAC/log mode (MACs returned for derivation) | |
| **K3** | the plan skips every id that already has a row | **S-K3:** the plan returns every id. Red: K3 |
| **K4** | a store that silently drops one key on the first bulk put: the repair writes exactly that key, and the result reads written 3, repaired 1, missing 0 | **S-K4:** the repair pass removed. Red: K4 |
| **K5** | a store that drops the same key on every put: the result reads missing 1, not complete, and never claims success | (S-K4's control: K5 stays green) |

### The production check: `activation-coverage.ts`, run by this session

Every input is read-only: the `kv-read` and `analytics-read` lines of the token file. Nothing is
written.
- **Who authenticated in the last 30 days, from two instruments:**
  - Analytics Engine: the verified device id (`blob5`) on per-request and boot points;
  - the `fw:` rows whose `lastSeen` is within 30 days.

  The check takes the union. `fw:` alone would miss a device that authenticates without sending a
  firmware version. Ids on the repo's fake-id allowlist are excluded: production refuses them.
- **It compares that union with the `act:` rows,** and prints counts only:
  - the size of each source, and of the union;
  - how many have a row, and how many are missing;
  - of the missing, how many have an enrolment-ledger row (`enr:dev:`, boards that self-enrolled)
    and how many do not;
  - one boolean: whether the test identity (the token file's `test-device`) is among the missing.
- **Anchor controls.** Each source must return at least one id, and the `act:`/`enr:dev:` listings
  must be able to see `cfg:fleet`. Any that cannot is exit 2, "cannot see", never a pass.
- **Exit:** 0 missing none, 1 missing some, 2 cannot see.

| | check (the pure `coverage()`) | sabotage |
|---|---|---|
| **K6** | given a union, the listed set, the ledger and the test id, it returns the exact counts and the boolean | **S-K6:** listed ids counted as missing. Red: K6 |

**Counts:** 6 new tests (K1-K6), so `vitest list` reads **940**.

**The runs:**

| run | when | predicted | outside the set |
|---|---|---|---|
| **C0** | now, before any seed (the negative control) | exit 1, missing = the union, because no `act:` row exists | 0 missing, or exit 2: **stop**, the check is blind |
| **R0** | a staging rehearsal of the seed by this session: three fake allowlisted ids, run twice | first run: read 3, written 3, missing 0; second run: already 3, written 0 | anything else: **stop** before Daniel runs it |
| **C1** | after Daniel's seed | exit 0, missing 0 | missing > 0: **the deploy stops**. Read the breakdown, which is named below |

**The likeliest breakdown if C1 misses, named before it runs:**
- **Self-enrolled boards:** a missing id **with** a ledger row is a board that self-enrolled and is
  not in Daniel's bench records.
- **The test identity:** it authenticates in production every day (`smoke-prod.sh`), so it is in
  the union and will be missing unless the seed includes it.

Both need a ruling (seed them, or exempt them) rather than an improvised fix.

### The seed and the coverage check, first results (2026-11-05 ~18:45Z, on `e33553d`)

- **The full run:** 939 passed, 1 skipped (`basemap-md2`), 1 failed (Q-C1 only, the CRLF
  artifact). That is 940 listed, as predicted; the list itself is re-read on drained ports with
  the K sabotages.
- **R0, the staging rehearsal: as predicted.**
  - Run 1: `read 3, already present 0, written 3, repaired 0, missing 0 -> COMPLETE`, exit 0.
  - Run 2: `read 3, already present 3, written 0, repaired 0, missing 0 -> COMPLETE`, exit 0.

  This proves the wrangler write, the `--remote` listing (it saw its own three rows), and that a
  seed never rewrites a row. **Three fake-id rows are left in staging KV**: `act:` for the
  allowlisted fakes `1111222233334444`, `aaaabbbbccccdddd` and `0123456789abcdef`, all `listed`.
- **C0: outside the set, stopped.** It exited 2, "cannot see", where exit 1 was predicted.
  - **Cause:** the token file has no `kv-read` line. It holds `analytics-read`, but nothing that can
    read KV over REST.
  - **The anchor did its job:** the check refused instead of reporting coverage it could not see.
  - **C0 and C1 wait for Daniel** to add `kv-read=<a Workers KV Storage: Read token>`.
    Nothing else changes.

### K's sabotages and the list (2026-11-05 18:48-18:50Z): as predicted

- **The control** ran 92 tests across the six files, all green.
- **S-K3** (the plan returns every id): red exactly {K3}.
- **S-K4** (the repair pass removed): red exactly {K4}. K5 stayed green under it, as named.
- **S-K6** (listed ids counted as missing): red exactly {K6}.
- **Restored by bytes** (`activation-lib.ts` `410b19e2c8c0`). `vitest list` read **940** from a
  drained start (TIME_WAIT 2,569).

**What remains before #548 can deploy:**
1. Daniel adds `kv-read=` to the token file.
2. C0 runs (predicted exit 1, everything missing).
3. Daniel seeds from his records.
4. C1 runs (predicted exit 0).
5. A missing id stops the deploy, and the breakdown above names who it is likely to be.

### C0, second attempt (2026-11-05 19:13Z): stopped again, cause named, not fixed

- **The `kv-read` line was present** (a boolean check), and the KV anchor passed: `cfg:fleet`
  read back, and the absent key read null.
- **The check still exited 2,** "cannot see": **Analytics Engine answered HTTP 422.**
- **Cause, from a read-only probe that printed the error text and counts only:** the API answers
  `selection used unsupported features: SELECT DISTINCT`. Analytics Engine's SQL does not support
  `SELECT DISTINCT`. The same selection written as `... GROUP BY dev` answers HTTP 200 (25 rows,
  20 of them 16-hex; no row printed). `usage-stats.ts`, which works, uses `GROUP BY` for the same
  reason.
- **The alternative, that the token lacks the scope, is excluded:** the `GROUP BY` probe used the
  same token and was answered.
- **Proposed, for a ruling:** change the one query to `GROUP BY dev`, then re-run C0 under its
  frozen prediction (exit 1, everything missing). The 422 itself shows the anchor working: it
  refused instead of passing on a source it could not read.

### C0, re-run after the accepted `GROUP BY` fix (2026-11-05 19:21Z, `feffe1d`): as predicted

The fix is ruled an instrument fix (~19:35Z), and the miss stays recorded: Analytics Engine's SQL
does not support `SELECT DISTINCT`.

```
activation coverage (production, last 30 days)
  authenticated: Analytics Engine 20, fw: 20, union 20
  with an act: row 0, MISSING 20
    missing with an enrolment-ledger row (self-enrolled): 18
    missing without one: 2
    the test identity among the missing: true
VERDICT: MISSING ROWS -- the deploy stops
```

**Exit 1, with every authenticated device missing,** because no `act:` row exists yet. The check
can fail, so a C1 of 0 will mean something. The two sources agree on 20.

**What this says about C1, before Daniel seeds:**
- 18 of the 20 have an enrolment-ledger row, which means they self-enrolled.
- 2 do not, and one of those is the test identity.

If Daniel's bench records do not cover the self-enrolled 18, C1 stops the deploy on them: the
breakdown named in advance, which needs a ruling.

## Grandfathering (ruled ~19:50Z): frozen before its code and before any production write

**Ruled:**
- Every device with an enrolment-ledger row is grandfathered, as `activated`.
- The test identity and `records-device` from the token file are added the same way, with no id
  printed.
- Daniel seeds the rest from his bench records.
- Then C1.

**Read first, read-only, counts and booleans only:**
- **The ledger:** 22 `enr:dev:` rows, all 16-hex, none fake. That is the 18 seen in the last 30
  days plus 4 not seen recently. There are no `act:` rows yet.
- **The token-file ids:**

  | line | in the ledger | authenticated in 30 days |
  |---|---|---|
  | `test-device` | no | yes |
  | `records-device` | no | yes |
  | `feeder-device` | yes | yes |

  So C0's 20 are exactly the 18 ledger devices plus these two.

**The generation: this cannot be done exactly as written, so it is surfaced here.** "Activated in
the current key generation" means a `gen` of `HMAC(DEVICE_KEY_SECRET, ...)`. That secret has no
copy outside the Worker, so no tool can compute it.
- **The seed writes these rows `activated` with no `gen`.** The gate already treats such a row as
  activated under every generation (fail closed), so every re-activation is refused, as ruled.
- **What it does not yet do** is re-open these rows on a rotation, as G does for activated rows.
- **The fix, proposed for #548 before it deploys:** the Worker stamps the current `gen` onto a
  gen-less `activated` row the first time it reads one. A post-deploy step then runs the tool's
  `status` over the grandfathered ids (counts only), so all of them carry the deployed generation
  before any rotation. This waits for a ruling.

**The tool:** `seed` takes `--state listed|activated` (default `listed`) and two more sources,
`--ledger` (every `enr:dev:` id) and `--token-ids test-device,records-device`. Counts only, as
before.

| | check | sabotage |
|---|---|---|
| **K7** | `seedWith(store, ids, "activated")` writes `activated` rows, and leaves an existing `listed` row untouched | **S-K7:** the state ignored, so it always writes `listed`. Red: K7 |

The list reads **941**: 940 plus K7.

**The production runs, in this order** (mine before Daniel's, so his `listed` rows cannot pre-empt
the grandfathered `activated` ones, given that a seed never rewrites a row):

| run | predicted |
|---|---|
| **G-1:** `seed --ledger --state activated` | `read 22, already present 0, written 22, repaired 0, missing 0 -> COMPLETE` (a dropped key may be repaired; missing must be 0) |
| **G-2:** `seed --token-ids test-device,records-device --state activated` | `read 2, already present 0, written 2, ... missing 0 -> COMPLETE` |
| **C1**, straight after G-2 | exit 0: union 20, with a row 20, **missing 0**. The 30-day window can move a device in or out; any missing id is a stop |
| **Daniel's seed** | `listed` rows for the rest of his records, skipping the 24 already written |

### G-1, 2026-11-05 20:00Z: outside the set, stopped, nothing written

- **Ruled ~20:15Z:** write the grandfathered rows, activated without a generation, as G-1 and G-2.
  Any reading other than the predicted ones stops the run.
- **G-1** (`seed --ledger --state activated`, production) **exited 1 with no output.**
- **It failed at its first step,** listing the ledger, before any write. Production holds **no**
  `act:` rows afterwards: a by-hand listing reads `[]`.
- **Why the cause is hidden:** the tool's failure message showed the last 600 characters of
  wrangler's stderr, which were two copies of the "unsafe fields" config warning. The error itself
  went to stdout, which the tool drops on failure. **The tool hid the cause,** the
  filtered-output failure CLAUDE.md warns about.
- **The same listing by hand, a minute later,** exited 0 and returned 22 keys (counted, not
  printed). So the failure was transient, or depended on something in the tool's call that a hand
  call does not have. The two cannot be told apart from what was kept.
- **Proposed, for a ruling:**
  1. On failure, the tool prints stdout's non-key lines as well as stderr, with ids masked and the
     known config warning dropped, so a failure names its cause.
  2. G-1 is then re-run under the same prediction.

  Nothing else changes, and Daniel's seed and C1 wait.

### The lazy generation write (accepted ~20:15Z): frozen before its code

- **The change.** When the Worker reads an `activated` row with no `gen`, through the gate or
  through `status`, it writes the current `gen` onto that row **before** evaluating it. The row
  then behaves like every other activated row: refused under its generation, re-opened once by a
  rotation. `effectiveState`'s fail-closed branch for a row without a generation stays as
  defence, but is no longer reached through these paths.

| | check | sabotage |
|---|---|---|
| **L1** | a row seeded `activated` with no `gen`: one `status` through the Worker leaves it carrying `keyGeneration(SECRET)` (12 hex), and an activation is then 409 | **S-L:** the write removed |
| **L2** | after that stamp, a simulated rotation (a different `DEVICE_KEY_SECRET`) re-opens it once: 200 with the rotated key, then 409 | (S-L) |

- **S-L predicted red, exactly:** {L1, L2}. Without the stamp, L1's row has no `gen`, and L2's
  rotated activation is refused (fail closed).
- **The list reads 943:** 941 plus L1 and L2.
- **After #548 deploys, the sweep.** `activation.ts sweep --ledger` and `sweep --token-ids
  test-device,records-device` call `status` through the Worker for each grandfathered id, then
  read the rows back with the `kv-read` token. They print counts only.
  - **Predicted:** 24 ids swept, 24 `activated`, **24 carrying a 12-hex `gen`**, 0 without.
  - The sweep's own code and test land with this change. Its production run waits for the deploy.

**Amendment, before any run (the count contradicted the text).** The block above says the sweep's
code and test land with this change, but it counted only L1 and L2, so its **943** was wrong as
written.
- **M1, the sweep's arithmetic:** given the states `status` returned and the `gen` values read
  back, the counts are exact (swept, activated, stamped, unstamped), and the line it prints holds
  no id.
- **S-M:** "stamped" counted from the state instead of the `gen`. Predicted red: exactly {M1}.
- **The list reads 944.** S-L is still predicted red exactly {L1, L2}.

**L and M, graded (2026-11-05 ~20:06Z): as predicted.**
- **The control** ran 96 tests across the six files, all green: 93 plus L1, L2 and M1.
- **S-L** (the lazy write removed): red exactly {L1, L2}.
- **S-M** (stamped counted from the state): red exactly {M1}.
- Both files were restored by bytes, sha checked. The list count (944) is read from drained ports
  before the push.

### G-1's instrument fix (ruled ~20:35Z): frozen before its code

**Ruled:** G-1 is recorded as an instrument stop with nothing written. The miss is that the tool
hid its own cause.

**The fix.** On a failed wrangler call, the tool prints stderr and stdout's non-key lines through
one function, `failureText`:
- every 16-hex run is masked as `<id>`;
- lines carrying a key (`"name":`) are dropped;
- the known "unsafe fields are experimental" config warning, and the header lines around it, are
  dropped;
- the result is capped.

| | check | sabotage |
|---|---|---|
| **F1** (unit) | given a stdout of id-shaped key lines plus an `[ERROR]` line, and a stderr of the config warning: the text keeps the error, masks every id (no 16-hex run survives), and drops the warning | **S-F:** the mask removed. Red: exactly {F1} |
| **F2** (one CLI run against a planted failure) | a fake `npx` on `PATH`, in a scratch directory and for that one run only, exits 1. Its stdout carries three id-shaped key lines (allowlisted fake ids) and an error line. `activation.ts seed --ledger --state activated --staging` must exit 1, and print the error line with **no 16-hex run** in all its output. **CONTROL:** that output contains `<id>` or the planted error text, so it said something | |

**The list reads 945:** 944 plus F1.

**Then, once each, in order:**

| run | predicted | outside the set |
|---|---|---|
| **G-1** (re-run once) | `read 22, already present 0, written 22, repaired 0, missing 0 -> COMPLETE` (a repair may appear; missing must be 0) | **stop and report, with the cause now visible.** No retry |
| **G-2** | `read 2, already present 0, written 2, ... missing 0 -> COMPLETE` | stop |
| **C1** | exit 0: union 20, with a row 20, missing 0 | stop |
| **Daniel's seed** (`provisioned.csv`, once) | counts only, ending in `COMPLETE` | Daniel reports it; I read nothing of the file |
| **C1 again** | exit 0, missing 0 | stop |
