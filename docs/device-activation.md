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

**F1 and F2, graded (2026-11-05 ~20:22Z): as predicted.**
- **F1:** the control ran 97 tests across the six files, all green. **S-F** (the mask removed)
  turned red exactly {F1}. The file was restored by bytes.
- **F2:** the CLI against the planted failure exited **1**. Its whole output held **0** 16-hex
  runs; it showed the planted `Authentication error [code: 10000]` line with its id as `<id>`,
  and the key lines were gone. The fake `npx` lived in a scratch directory and was on `PATH` for
  that one run only.

### G-1 (re-run once), G-2 and C1, production, 2026-11-05 20:21-20:22Z: all as predicted

| run | got |
|---|---|
| **G-1** `seed --ledger --state activated` | `read 22, already present 0, written 22, repaired 0, missing 0 -> COMPLETE`, exit 0 |
| **G-2** `seed --token-ids test-device,records-device --state activated` | `read 2, already present 0, written 2, repaired 0, missing 0 -> COMPLETE`, exit 0 |
| **C1** | exit 0: Analytics Engine 20, `fw:` 20, union 20; **with a row 20, missing 0**; the test identity not missing |

**One difference from the stopped run, recorded and not interpreted:** the tool was launched with
`node_modules/.bin/tsx`, not `npx tsx`. Its own wrangler calls still go through `npx wrangler`,
where the first run failed, so this neither explains nor excludes that failure. The cause stays
unknown, and the tool would now print it.

**Next:** Daniel's seed (`provisioned.csv`, once), then C1 again. The rows written so far are inert
in production until #548's gate deploys.

### Daniel's seed and C1 again (2026-11-05 ~20:25Z): as predicted

- **Daniel's seed** (`provisioned.csv`, run once, by Daniel) reported `read 5, already present 5,
  written 0, repaired 0, missing 0`. Every bench-provisioned board in that file was already
  covered by G-1 and G-2, so it wrote nothing. Nothing of the file was read by this session.
- **C1 again:** exit 0. Analytics Engine 20, `fw:` 20, union 20; **with a row 20, missing 0**.
- **#548's deploy gate is met.** Stock boards Daniel holds that were never provisioned are not in
  `provisioned.csv`. If any is to activate, its MAC goes in through `import` (or `seed` before
  the deploy).

### A MAC list with or without colons (committed 21:13Z, `c7479f6b`, from #545's bench): frozen before its test

The Flash Download Tool shows the STA MAC as 12 characters without colons; esptool prints it with
colons. The factory's list may come either way.
- **The code already accepts both.** `parseImport`'s bare-MAC rule takes an optional separator, and
  A12's list case includes a MAC with no separator.
- **K8 pins it on its own,** in the shapes the factory will actually send: uppercase without
  colons (as the tool shows it), lowercase with colons, and the same MAC in both forms counted
  once. It runs through both `parseImport` (`import`) and `parseRecords` (`seed`), with no bad
  line.

| | sabotage | predicted red, exactly |
|---|---|---|
| **S-K8** | the bare-MAC rule requires a separator | {K8, A12's "a plain list: one MAC per line, any separator, deduplicated; comments and blanks skipped"} |

**The list reads 946:** 945 plus K8.

**K8, graded (2026-11-05 21:14Z, `7dd3a0e8`): as predicted.** The control ran 98 tests across the six files,
all green (97 plus K8). **S-K8** turned red exactly {K8, A12's list case}. The file was restored by
bytes.

**`vitest list` read 946**, as predicted, at 21:29Z on `7dd3a0e8`.

**Record fix (ruled at about 21:29Z by the clock).** Two time labels above ran ahead of the clock,
and are now replaced with commit times: "ruled ~21:20Z" became `c7479f6b`'s 21:13Z, and "graded
~21:30Z" became `7dd3a0e8`'s 21:14Z. **The other reviewer-block labels cited in this doc carry the
same skew of roughly 15-20 minutes:** "~19:50Z", "~20:15Z", "~20:35Z" and "~21:20Z". For example,
G-1's first run (20:00:08Z by the clock) followed a ruling quoted as "~20:15Z". They are left as
quoted, and this note dates them. From here on, every time in this doc is read from `date -u` or
from a commit.

## Watch mode for production (Daniel's decision; ruled at about 21:29Z by the clock): frozen before its code

**This rules Fact 4's off-switch question.**
- **The var.** `ACTIVATION_GATE = "watch" | "enforce"`: production `"watch"`, staging
  `"enforce"`. A missing or unknown value acts as **enforce** (fail closed). The tests set no var,
  so every existing row runs under enforce, unchanged.
- **What watch does.** The gate reads and writes exactly as under enforce:
  - a listed id's first activation writes `activated`;
  - the generation write and the post-deploy sweep are unchanged.

  Where enforce would answer 403 `not_listed` or 409 `already_activated`, watch carries on to
  minting and returns the key, as main does today. Each would-be refusal bumps
  `enr:watch:<yyyy-mm-dd>:<reason>`, **never** `enr:refused:`. Watch writes no `act:` row for an
  unlisted id, and no watch key or value contains an id.
- **The reader.** `reconcile-fleet.py` prints the watch counts per day beside the refused counts,
  labelled as would-be refusals.
- **Staging under enforce stops nothing in use today** (point e), read before this change:
  - staging has **no** `enr:day:` counters in their 40-day window, **no** ledger rows, and only the
    three fake rows from R0;
  - its 35 enrol requests in 30 days are 21 at 200 (page loads: there were no mints), 7 at 403 and
    7 at 400 (the smoke's POSTs, refused before the gate in either mode).

| | check | sabotage | predicted red, exactly |
|---|---|---|---|
| **W1** | watch: an unlisted id gets 200 with `deriveDeviceKey(SECRET, id)`, `enr:watch:<day>:not_listed` = `1`, no `act:` row, no `enr:refused:` key, and no id in any `enr:watch:` key or value | **S-W1:** watch refuses anyway (the mode ignored) | {W1, W2}: W2's second activation is refused too |
| **W2** | watch: a listed id's first activation is 200 and its row reads `activated`; its second is 200 with the key, and `enr:watch:<day>:already_activated` = `1` | **S-W2:** watch skips the write (a read instead of the claim) | {W2} |
| **W3** | a missing value, and the unknown values `"WATCH"`, `"off"` and `""`, refuse an unlisted id with 403 `not_listed`, as A1 does | **S-W3:** default to watch (anything but `"enforce"` is watch) | every test that expects a gate refusal with no var set: {A1, A3 "the first activation gets ...", A4 "reset re-opens exactly one more activation", G1, R1, L1, L2, W3, enroll's "refuses the repeat, and after a reset returns the SAME key"}. Nine, each checked against the plant before running |
| **W4** | `scripts/check-activation-gate.sh`: the production and staging dry-runs list `env.ACTIVATION_GATE ("watch")` and `("enforce")` respectively; exit 0 | **S-W4:** production's var removed from `wrangler.toml` | the script exits 1, naming production |
| **H4** | the reconcile harness: given `enr:watch:` keys, the would-be-refusal section prints them per day; with none, it says so | **S-H2:** the watch block removed | H4 red (H1-H3 green) |

- **The list reads 949:** 946 plus W1, W2 and W3. W4 and H4 are scripts, not vitest cases.
- **CI for the reviewed head,** predicted from the latest finished proxy-code PR run (#540 at
  `e3b94fc`: 33 check-runs, 30 success, 3 skipped). #548 touches the same workflows, so: **33,
  30 success, 3 skipped, 0 failed**.
  - **Workers on the merge with main:** main's 911 plus #548's 49 (the list's 949 minus its base
    900) is **960 passed, 1 skipped**, and Q-C1 passes, because CI checks out LF.
  - **dashboard 104.**
- **(f)** Production goes to `"enforce"` only when Daniel says so: a one-line PR, a deploy from
  that named commit, and W4's check on it. The watch counts reach him first.

**Watch mode, graded (2026-11-05 21:31-21:35Z by the clock): all as predicted.**
- **The control** ran 101 tests across the six files, all green (98 plus W1, W2 and W3).
- **S-W1:** red exactly {W1, W2}. **S-W2:** exactly {W2}. **S-W3:** exactly the nine named
  above.
- **W4:** the dry runs list production `("watch")` and staging `("enforce")`, exit 0. **S-W4**
  (production's var removed from `wrangler.toml`): exit 1, naming production. `wrangler.toml` was
  restored by bytes, sha checked.
- **H4:** the reconcile harness passes H1-H4. **S-H2** (the watch block removed) fails H4 alone.
- Every planted file was restored by bytes.

## #548 merged, and its deploy: results held for the review, then predictions frozen before the deploy

**Held back so the head would not move under the review** (all read at `f98e9b18`):
- **CI, finished:** 34 check-runs, 31 success, 3 skipped (`promote`, `version`, `ship-alert`),
  0 failed. Tested merged into `7cca898`.
- **Workers:** 960 passed, 1 skipped (961). **Dashboard:** 104.
- **The miss on the count:** I predicted 33. The cause is `ota-identity`, a job #552 (`8440965`)
  added to main after #540's run, the one I predicted from. #548 touches no workflow file. The
  miss is kept and re-registered at 34 (reviewer, the block labelled ~00:56Z).
  - **The rule from it, for all sessions:** a CI baseline run must postdate main's last workflow
    change, and the prediction names that commit.
- **Local:** `vitest list` read 949. The full suite read 948 passed, 1 skipped, and Q-C1 (CRLF)
  as the only failure, on a re-run from TIME_WAIT 11.
  - **The first full run was void:** `ConnectEx` #1225 on all 71 files, from port exhaustion,
    while the map server's smoke driver was running too. The rule from it, for all sessions: one
    whole-suite run at a time, and before any whole-suite vitest or sabotage driver, no other
    session's `vitest` or `sabotage-` process may be running, and TIME_WAIT must be under 2,000.

**The merge.** Approved at `f98e9b18`, and squash-merged as **`5c300417`** at 00:58:26Z (by the
clock).
- **Main's CI on it, predicted before reading:** 34 check-runs, 32 success, 2 skipped (`promote`,
  `version`), 0 failed.
  - **The baseline is #548's own run.** Main's last workflow change, `12569fa0` (#555),
    postdates that run, but it adds two steps to the existing `device-ids` job and no job. On
    main, `ship-alert` runs.

**The deploy, predicted before any of it runs:**

| step | predicted | outside the set |
|---|---|---|
| **What it carries** beyond production's `3e0a444` | only #548 touches `proxy/`. Changed and **in the bundle** (esbuild metafile inputs): exactly `src/activation.ts`, `src/enroll.ts`, `src/enrollpage.ts`, `src/index.ts`, `src/provision.ts`, `src/support.ts`. **Not in it:** `src/types.ts` (type-only imports, also absent from #540's inputs) and every `proxy/scripts/*` file. `wrangler.toml`'s two vars are config, checked by W4 | any other changed file in the inputs: **stop and ask** |
| **W4** on `5c300417` | `check-activation-gate.sh`: production `("watch")`, staging `("enforce")`, exit 0 | **stop** |
| **Rollback targets**, re-read just before | staging `968d9f23`, production `5e7f481c` (#540's deploys) | a different version serving: **stop** |
| **Staging** (`deploy.sh staging`, from `5c300417`) | `/healthz` reads `5c30041` on two reads; smoke 38 PASS, 0 FAIL as the test identity. Its enrol POSTs are refused before the gate | **stop**, and production waits |
| **Production** (outside 06:00-08:00Z, #540's night window) | `/healthz` reads `5c30041` on two reads; smoke 38 PASS, 0 FAIL; the test identity's `/config` unchanged (`pollNightMs` 60000) | **stop**; the rollback target is `5e7f481c` |
| **The sweep**, through the Worker | `sweep --ledger`: swept 22, activated 22, **stamped 22**, unstamped 0. `sweep --token-ids test-device,records-device`: swept 2, activated 2, **stamped 2**. **24 stamped** in all | any unstamped: **stop and report** |
| **C1 again**, after the sweep | exit 0, missing 0 | **stop** |

Production stays on `"watch"` until Daniel says otherwise. The watch counts reach him before that
decision.

**The deploy, as run.**

- **What it carries: outside the set, stopped (01:14:48Z, by the clock).**
  - **The reading.** The dry run (`--env production`, from `5c300417`, clean tree) exited 0. Its
    metafile (`53f98177f73d`, 28,089 B) lists 51 inputs. The changed list is
    `git diff --name-only 3e0a444 5c300417 -- proxy`: 17 files, 7 under `src/` and 4 under
    `scripts/`. The two lists' intersection came back **empty**, and `src/types.ts` read as absent.
  - **Why it stopped.** The row's only outside-the-set branch was "any other changed file in the
    inputs". An empty intersection was not registered, so the deploy stopped there.
  - **The cause is the instrument, not the bundle.** Python on Windows wrote the inputs list in
    text mode: 50 CR bytes in 51 lines. The changed list, written by `git`, has 0 CR bytes. `comm`
    compares bytes, so `src/activation.ts\r` can never equal `src/activation.ts`. The `^src/types.ts$`
    grep is blind for the same reason, so "types.ts absent" is unread too. Both files were read as
    bytes at 01:16:32Z.
  - **The alternative, and why it is excluded:** a bundle that really carries none of the changed
    files, for example a dry run built from another tree. Excluded because the same inputs file's
    first line is `src/activation.ts`, which is a changed file. That one line is all of the result
    seen so far. The rest of the intersection, and whether `src/types.ts` is absent, are unread.
  - **W4 ran in the same command,** before the bundle result was read: production `("watch")`,
    staging `("enforce")`, exit 0. **As predicted.**
  - **Nothing has been deployed.** The rollback targets are re-read just before staging, as the
    table says.
- **The proposed instrument fix (written, compiled, not run):** `b548_bundle.py` in the session
  scratchpad, sha256 `e048618fd17a`. One Python process makes the whole comparison in memory, with
  no file written between the two sides:
  - it reads the metafile's inputs, and the `git diff` output captured to a file and read as bytes;
  - it splits both on `\n` **only**, and strips the `proxy/` prefix. `splitlines()` would quietly
    fold a CRLF away, which would leave A-CR unable to fire and S-B1 unable to fail;
  - it grades membership with one test. Before any absence is believed, three anchors must pass, in
    this order, stopping at the first that fails:
    - **A-SAME:** 51 inputs and 17 changed; the metafile's sha256 still reads `53f98177f73d`.
      It is the same artifact, not rebuilt.
    - **A-CR:** no element of either list contains a `\r`.
    - **A-ENTRY:** `src/index.ts`, the entry module, is found in the inputs by the same membership
      test that grades the result.
  - Any anchor that fails means "cannot see": exit 2, stop.
- **Two sabotages of the fixed instrument (proposed, not run).** They run after the graded run, and
  the graded run is believed only if both read as predicted:
  - **S-B1, the CRLF plant** (`--inputs-from` the old CRLF list). Predicted: A-SAME passes, then
    `CANNOT SEE: A-CR (50 inputs and 0 changed lines carry a CR)`, exit 2.
  - **S-B2, the drop plant** (`--drop src/enroll.ts`, applied after the anchors so it tests the
    grading itself). Predicted: anchors pass; the intersection is the other five;
    `OUTSIDE THE SET: predicted and absent: src/enroll.ts`; exit 1.
- **New frozen predictions for the re-run** (the same metafile):

| | predicted | outside the set |
|---|---|---|
| anchors | A-CR, A-ENTRY and A-SAME pass | any fails: **cannot see, stop** |
| changed and in the bundle | exactly `src/activation.ts`, `src/enroll.ts`, `src/enrollpage.ts`, `src/index.ts`, `src/provision.ts`, `src/support.ts` | another changed file in the inputs: **stop and ask**. One of the six absent: **stop and ask** |
| not in the bundle | `src/types.ts`, and all 4 `scripts/*` files | any of them present: **stop and ask** |
| verdict | `VERDICT: as predicted`, exit 0 | exit 1 or 2: **stop** |

  Then the rest of the table above, unchanged. If the ruling lands after 06:00Z, production waits
  for 08:00Z, the end of #540's night window. Staging can go as soon as the re-run reads as
  predicted.

**The ruling (~02:40Z):** the cause is accepted, and `b548_bundle.py` is approved to run as written.

- **The bundle proof, re-run at 02:58:07Z: as predicted.**
  - Graded run: the anchors pass (meta `53f98177f73d`, 51 inputs, 17 changed). Changed and in the
    bundle: exactly the six. Exit 0.
  - **S-B1:** `CANNOT SEE: A-CR (50 inputs and 0 changed lines carry a CR)`, exit 2.
  - **S-B2:** the other five, then `OUTSIDE THE SET: predicted and absent: src/enroll.ts`, exit 1.
- **Rollback targets, re-read at 02:58:17Z: as predicted.** Staging `968d9f23`, production
  `5e7f481c`.
- **Staging, 03:00:58Z to 03:02:08Z: as predicted.**
  - `deploy.sh staging` from `5c300417`, exit 0. The new version is `6833b32c`, and the binding
    list shows `env.ACTIVATION_GATE ("enforce")`.
  - `deploy.sh`'s own fetch: "2 commit(s) behind origin/main, none touching proxy/".
  - `/healthz` read `5c300417` in the confirmation and on two more reads.
  - Smoke as the test identity: PASS 38, FAIL 0.
  - The enrol POSTs are refused before the gate: 403 `unverified` and 400 `bad_request`.
  - **#548 staging verified at 03:02Z.**
- **Production: outside the set, stopped (03:02:43Z).**
  - **The reading:** `deploy.sh production` refused, exit 1, with "this tree is 2 commit(s) behind
    origin/main in proxy/". Nothing was uploaded, and `/healthz` still reads `3e0a444` on two reads.
  - **The cause:** main moved. Two `proxy/` commits from other sessions landed between 03:01Z and
    03:02:40Z: `9db83163` (#556, the opuk split) and `9b511dc2` (basemap A1).
  - **The alternative, and why it is excluded:** a stale ref inside `deploy.sh`. Excluded because
    `deploy.sh` fetches `origin main` itself, and, from the worktree root, `git rev-list` reads 2
    commits in `proxy/` and names exactly these two.
  - **My own pre-check was blind.** It read "0 behind", but it ran from inside `proxy/`, where
    `-- proxy/` names `proxy/proxy/`, so it could only ever read 0. The guard that executes caught
    what that check could not. A path-scoped check runs from the root.
- **What the two commits carry.** Neither changes production's config:
  - `9b511dc2`'s `wrangler.toml` change adds the `BASEMAP_PKG` bucket for dev/test and staging only
    ("production gets one at D4"). Its code (`src/basemap/*`, `src/types.ts`) is in the proxy
    bundle.
  - `9db83163` changes the wx-ingest Workers' own tomls and entry files. The proxy bundle imports
    neither entry file.
  - Production runs `3e0a444`, which has neither commit. So deploying `5c300417` reverts nothing
    production runs. `deploy.sh`'s word "REVERTED" is measured against main's tip.
- **Proposed, for a ruling (not run):**
  - **(a), recommended:** `ALLOW_BEHIND=1 ./scripts/deploy.sh production` from `5c300417`. That is
    the commit whose bundle was proven and that staging runs. Main's two commits reach production
    in their own sessions' deploys, which carry #548 as an ancestor.
  - **(b):** merge main into the deploy tree and deploy its tip. That would put basemap A1's code
    in production ahead of D4, and the opuk split's code outside its session's slot. It would also
    need a new bundle proof. Not recommended.
- **Frozen predictions for (a):**

| step | predicted | outside the set |
|---|---|---|
| `deploy.sh`'s banner | `ALLOW_BEHIND=1`, listing exactly `9b511dc2` and `9db83163` | any other commit listed: **stop and ask** |
| the upload | the binding list shows `env.ACTIVATION_GATE ("watch")` | anything else: **stop** |
| production's checks | `/healthz` `5c30041` on two reads; smoke 38 PASS, 0 FAIL; the test identity's `pollNightMs` 60000 | **stop**; roll back to `5e7f481c` |
| the sweep, then C1 | as in the table above: 24 stamped, then exit 0 | as above |

  Production goes only outside 06:00-08:00Z; if the ruling lands after 06:00Z, it waits until
  after 08:00Z. The UK session's R3′ waits on the "#548 production verified" line. Staging now
  runs `5c300417`, without basemap A1's code; a staging deploy from main carries #548 as an
  ancestor.

**The ruling (~03:25Z): (a).**

- **Rollback target, recorded first (03:26:50Z):** production on `5e7f481c`, `/healthz`
  `3e0a444` on two reads. From the root, main's lead in `proxy/` was exactly `9b511dc2` and
  `9db83163`.
- **Production, 03:27:08Z to 03:28:12Z: as predicted.**
  - `ALLOW_BEHIND=1 ./scripts/deploy.sh production` from `5c300417`, exit 0. The warning listed
    exactly `9b511dc2` and `9db83163`. The new version is `3053d6b4`, and the binding list shows
    `env.ACTIVATION_GATE ("watch")`.
  - `/healthz` read `5c300417` in the confirmation and on two more reads.
  - The test identity's `/config`: 200, `pollNightMs` 60000.
  - Smoke: PASS 38, FAIL 0. The enrol POSTs are refused before the gate: 403 `unverified` and
    400 `bad_request`.
- **The sweep: outside the set, stopped (03:28:47Z).** The token-id sweep and C1 have not run.
  - **The reading:** `sweep --ledger` exited 1: "23 unique id(s) from the enrolment ledger;
    swept 23, activated 22, stamped 22, unstamped 1". Predicted: 22, 22, 22, 0.
  - **The cause:** the enrolment ledger grew after it was seeded. G-1's ledger seed read 22 ids
    at 20:21:39Z. A read-only check (`diag548/extra-id.mts` in the session scratchpad; it prints
    counts, states, field names and times to the minute, and never an id) found exactly one
    ledger id that is not activated and stamped:
    - it has **no `act:` row**;
    - its `enr:dev:` row says first and last enrolment **2026-11-05T23:30Z**;
    - its `fw:` row is an `s3-128`, first and last seen 23:30Z.
    It enrolled about three hours after the seed, while production still ran `3e0a444` with no
    gate, so nothing refused or counted it.
  - **The alternative, and why it is excluded:** one of the 22 seeded rows lost its stamp or its
    state. Excluded because the 22 ids the seed wrote all answered `activated` with a stamped
    `gen`, and the one odd id has no row at all. It is a 23rd id, not one of the 22.
  - **Whose board, as inference only:** not the #545 bench board on the timing. That board's
    Wi-Fi setup was at 20:47Z (P4), and it was fully erased again at 21:03Z (the Download Tool
    run). `fw:` refreshes `lastSeen` at most hourly while a device is in use (`src/fleet.ts`), so
    a `lastSeen` of 23:30Z, read at 03:30Z, means no request since about then. A board online for
    under an hour fits a test or a short setup better than a desk unit left running, but the data
    does not exclude a customer's unit.
  - **The gap this exposes:** the ledger seed was a snapshot taken seven hours before the
    deploy. The sweep's "22" was frozen from that snapshot and never re-read just before the
    deploy: an anchor against a target that was still moving.
- **Proposed, for a ruling (not run):**
  - **(A), recommended:** grandfather it, since it enrolled before the gate existed. Re-run
    `seed --ledger --state activated`. A seed never rewrites an existing row (`planSeed`), so it
    writes only rows that are missing. **The guard comes first:** the ledger can now also grow
    by watch-mode enrolments of unlisted boards, and a ledger seed would grandfather those too.
    So immediately before the seed, `extra-id.mts` must read 23 ledger ids and exactly one
    without an activated row, with a first enrolment before 03:27:08Z.
  - **(B):** leave it unlisted. Watch counts it if it ever enrols again, and C1 reads exit 1,
    missing 1, until it is decided. It must be decided before enforce.
- **Frozen predictions for (A):**

| step | predicted | outside the set |
|---|---|---|
| the guard (`extra-id.mts`) | ledger ids 23; exactly 1 not activated and stamped, no `act:` row, first enrolment 2026-11-05T23:30Z | anything else: **stop, nothing written** |
| `seed --ledger --state activated` | read 23, already present 22, written 1, repaired 0, missing 0 -> COMPLETE | read not 23, or written not 1: **stop and report** |
| `sweep --ledger` | swept 23, activated 23, stamped 23, unstamped 0 | any unstamped: **stop** |
| `sweep --token-ids test-device,records-device` | swept 2, activated 2, stamped 2, unstamped 0 | any unstamped: **stop** |
| C1 (`activation-coverage.ts`) | exit 0, missing 0 | **stop** |

  25 stamped in all. Under (B): the token-id sweep as above, then C1 exit 1, missing 1.

**The ruling (~04:22Z): (A).** The "#548 production verified" line is released by the ruling:
**#548 production verified at 03:28Z.**

- **The dry run.** `activation.ts seed` has no dry-run flag. Its write guard is `planSeed`: it
  writes only ids with a ledger row and no `act:` row, and never rewrites a row. So the dry run is
  the read-only guard frozen in (A), which computes the same set, with counts only.
- **The guard, 04:23:55Z: as frozen.** 23 ledger ids, exactly one not activated and stamped. That
  one has no `act:` row and a first enrolment of 2026-11-05T23:30Z.
- **The seed: outside the set, stopped (04:24:12Z).**
  - **The reading:** `seed --ledger --state activated` exited 1 after 4 s, with nothing on stdout.
    Stderr: "wrangler kv key list failed (exit 1)", then `Authentication error [code: 10000]`
    from the KV keys API. Wrangler then reported the OAuth login, with `workers_kv (write)` among
    its scopes.
  - **Nothing was written.** The listing that failed is the seed's first step, and its refusal
    comes before `planSeed` and before any put. Read back at 04:24:56Z: still 23 ledger ids and
    one with no `act:` row.
  - **The cause: a transient authentication failure at the KV API.** At 04:25:16Z the same
    `wrangler kv key list --prefix=enr:dev: --remote` (a read; counts printed) exited 0 with 23
    keys and no code 10000.
  - **The alternatives, and why they are excluded:**
    - *A `CLOUDFLARE_API_TOKEN` shadowing the login:* absent in the shell and at user level, and
      the command ran with it unset.
    - *A login without KV permission, or an expired login:* the same login listed the keys 64 s
      later, and its scopes include `workers_kv (write)`.
  - **Likely the same as G-1's first failure,** which also stopped at the ledger listing, before
    `failureText` could show why. That run re-ran once and came out as predicted.
- **Proposed (not run):** re-run unchanged, once: the guard, then the seed, with the frozen
  predictions of (A). If the seed fails the same way again, stop: a second failure is not a
  transient.

**The ruling (~04:30Z): one retry, unchanged.** Every step read as predicted:

| step (by the clock) | read |
|---|---|
| the guard, 04:32:29Z | 23 ledger ids; exactly one not activated and stamped, with no `act:` row and a first enrolment of 2026-11-05T23:30Z |
| `seed --ledger --state activated`, 04:32:32Z | exit 0: read 23, already present 22, written 1, repaired 0, missing 0 -> COMPLETE |
| `sweep --ledger`, 04:32:51Z | exit 0: swept 23, activated 23, stamped 23, unstamped 0 |
| `sweep --token-ids test-device,records-device`, 04:32:57Z | exit 0: swept 2, activated 2, stamped 2, unstamped 0 |
| C1 (`activation-coverage.ts --env production --days 30`), 04:33:16Z | exit 0: authenticated 21 (Analytics Engine 21, `fw:` 21, union 21); with an `act:` row 21; **missing 0** |

25 rows are stamped. Production stays on `"watch"` until Daniel says otherwise, and the watch
counts reach him before that decision.

## Follow-ups (ruled 2026-11-06 ~18:12Z): Daniel's decisions, and the watch counts

### The watch counts since the deploy: predicted before they are read

**What is read:** production's `enr:watch:<day>:<reason>` counters since #548 went live (03:28Z, 2026-11-06), and the
`enr:refused:` counters. Read with the `kv-read` token, through `scripts/kv-rest.ts`. The keys name a day and a reason
and nothing else; their values are counts. No id is read or printed.

**The anchor control:** `cfg:fleet` must read back and a known-absent key must read null, as the coverage check
requires. Otherwise the read is "cannot see".

| | counter | predicted |
|---|---|---|
| **WC1** | `enr:watch:2026-11-06:not_listed` | **absent (0)**. The one board that enrolled since the ledger seed did so at 23:30Z on 11-05, before the gate existed, and has not been seen since |
| **WC2** | `enr:watch:2026-11-06:already_activated` | **absent (0)** |
| **WC3** | any `enr:refused:` key | **none**: production is in watch, which counts and never refuses |

**Outside the set:** any count is reported as read. **A `not_listed` count is said plainly: it is a real board that
would have been refused.**

### The watch counts, read (2026-11-06, 18:46Z): one row off

The anchor held: `cfg:fleet` read back, and a known-absent key read null.

| | read | verdict |
|---|---|---|
| **WC1** `not_listed` | **absent** | **as predicted**: no unlisted board has enrolled since the deploy |
| **WC2** `already_activated` | **1** (on 2026-11-06) | **OFF** (predicted absent) |
| **WC3** `enr:refused:` | **none** | **as predicted** |

**What WC2's 1 is:** one enrolment since 03:28Z came from an id whose `act:` row was already activated. Watch mode
let it through, gave it its key, and counted it. **Under enforce it would have been refused with 409
`already_activated`, and needed a reset to recover.**

**Whose it is, the counts cannot say, and the candidates are named rather than chosen:**
- **(i)** a customer's unit that lost its key (a factory reset, or a Wi-Fi reset that re-enrols) and enrolled again:
  the case enforce turns into a support reset;
- **(ii)** the board that enrolled at 23:30Z on 11-05, grandfathered as activated at 04:32Z, enrolling again
  afterwards;
- **(iii)** a bench or test board. Not the COM16 board: today's proof left it at SETUP, with no Wi-Fi.

**Proposed (not run), for a ruling, since the ruling asked for counts only:** a read-only characterisation in the
shape of `extra-id.mts`. For the ledger rows whose last enrolment is after 03:28Z: the count, each one's first and
last enrolment to the minute, its `act:` state, and its `fw:` model. Never an id. That separates (i) from (ii), and
says whether (iii) is a known bench board.

### Item 4 (ruled ~18:12Z): the stale line, the example MAC, and Daniel's decisions. Predicted before the edits

| | check | predicted |
|---|---|---|
| **A4-1** | the diff against the merge base | exactly `docs/device-enrollment.md` (one sentence near line 109), `proxy/src/provision.ts` (`normalizeMac`'s comment, one line) and this file |
| **A4-2** | `provision.ts` | its only changed line is the comment. No Espressif-prefixed (`90:70:69`) example is left, and the new one is from the RFC 7042 documentation range, `00:00:5E:00:53:xx` |
| **A4-3** | before the push | `check_device_ids.py`, `check_wifi.py` and `skus.py --check-readers` pass |
| **A4-4** | CI on the PR, read with nothing queued, twice | **34: 31 success, 3 skipped** (`promote`, `version`, `ship-alert`), 0 failed. Baseline: #556's PR run (34: 31 success, 3 skipped), which also touched `proxy/` and postdates main's last job-set change, `84409651`; main's last workflow change is `259e8688`, a step |

**Outside the set:** stop and report.

### Daniel's decisions (2026-11-06, ruled ~18:12Z)

- **The list is filled a whole batch at a time.** The factory's MAC list is imported with `activation.ts import` when
  each batch arrives. **No shipped gate and no Shopify link get built.**
- **The accepted limit:** a shelf board activated by someone who guessed its id stays accepted. The fix is a reset
  (`activation.ts reset`).
- **Production stays in watch mode for a while.** Daniel wants data before any refusals. No enforce PR is prepared
  until he asks.

**Item 4, as made:**
- `docs/device-enrollment.md`'s historical argument now says the stateless design *had* no allowlist, and points
  here.
- `normalizeMac`'s comment uses `00:00:5E:00:53:01` (RFC 7042's documentation range) in place of an
  Espressif-prefixed example that read like a real unit's.

### WC2's characterisation (approved ~18:54Z): predicted before the read

**What is read (read-only, `kv-read`):** the ledger rows (`enr:dev:`) whose last enrolment is after 03:28Z on 11-06.
For each: its first and last enrolment to the minute, its `act:` state and whether it carries a `gen`, and its `fw:`
model and last-seen time. **Never an id.** It runs behind the same anchor control.

**Today's board activity, to set beside it:**
- **W-CR** on Weatherscope's board, 18:08-18:26Z;
- the **COM16** proof, 18:40-18:42Z, which left that board at SETUP with no Wi-Fi;
- **E2:08:** not flashed today (the bench window did not run).

| | check | predicted |
|---|---|---|
| **WX1** | ledger rows enrolled after 03:28Z | **exactly 1** |
| **WX2** | that row | `act:` **activated, stamped**; its last enrolment **inside W-CR's window (18:08-18:26Z)**; its model Weatherscope's |
| **WX3** | the COM16 board and E2:08 | **no** row last enrolled in the COM16 proof's minutes, and none for E2:08 today |

**Outside the set:** report what is read, and say which activity it matches, if any.

### WC2's characterisation, read (18:55Z)

The anchor held. Ledger rows: 23.

| | read | verdict |
|---|---|---|
| **WX1** | rows last enrolled after 03:28Z: **1** | **as predicted** |
| **WX2** | that row: first enrolment **2026-11-05T23:30Z**, last **2026-11-06T16:30Z**, 2 enrolments; `act:` activated, stamped; `fw:` **s3-128**, last seen **18:05Z** | **OFF**: not inside W-CR's window and not Weatherscope's model |
| **WX3** | no row for the COM16 proof or for E2:08 | **as predicted** |

**Which activity it matches: none of the three.** It is the board that enrolled at 23:30Z on 11-05, three hours after
the ledger seed, and was grandfathered as activated by this morning's guarded re-seed (04:32Z). It enrolled again at
16:30Z today, which is what WC2 counted. It is live, seen at 18:05Z, two hours after the second enrolment.

**My miss:** WX2 assumed the count came from today's bench activity. The data excludes that: W-CR is 18:08-18:26Z and
the enrolment was at 16:30Z; the model is `s3-128`, not Weatherscope's.

**Why it matters for enforce, stated plainly:** a live, grandfathered s3-128 enrolled a second time in normal use.
Under enforce, that second enrolment would have been refused with 409 `already_activated`, and the unit would have
needed a support reset. Whatever makes a working unit enrol again (a Wi-Fi reset, a settings reset, or something
else) is not known from these rows. That question bears on the enforce decision, and is left open here.

### WX2's board is a customer's unit (Daniel, ~19:20Z)

Daniel confirms that the re-enrolled s3-128 (first enrolment 23:30Z on 11-05, again 16:30Z on 11-06) is not his: it
is a customer's unit. No name and no id are recorded here or anywhere in this repo.

### What makes a board enrol again, from the v18 source (~19:20Z; read-only, no board)

A board enrols again only when its device page offers enrolment, and the page offers it only when the board holds no
key (`enrolled` is `cloud-key-fac` being non-empty, `ConfigurationWebServer.cpp:2648`) or its key has been refused
(page JS `:321-322`, `stNeedKey`). The enrolment page itself is on our server, so it can also be opened from outside
the board (a printed card, a bookmark, browser history) whatever state the board is in.

| action | clears the stored key? | where (v18) |
|---|---|---|
| factory reset (config page, or the Stats-screen hold set to "factory") | **no**: `config` is cleared and `cloud-key-fac` is restored in the same handle | `FactoryReset.cpp:30` (`PRESERVE_KEY`), `:40-64`, `:97-101`; called from `ConfigurationWebServer.cpp:4071`, `AircraftManager.cpp:9584-9585` |
| Wi-Fi reset or a new network (`/reset-wifi`, the boot touch-hold, the Stats-screen hold set to "Wi-Fi") | **no**: only the Wi-Fi credentials and the fast AP are forgotten | `FactoryReset.cpp:86-93`; called from `ConfigurationWebServer.cpp:4042`, `main.cpp:470`, `AircraftManager.cpp:9585` |
| firmware update (OTA) | **no**: an app-partition write; the one `p.clear()` is the OTA record's own namespace | `OtaUpdater.cpp:450`, `:536` |
| a reflash with an erase (a full chip erase, or the factory image at 0x0, whose NVS range is all 0xFF) | **yes**: NVS is gone, the page offers enrolment | `ConfigurationWebServer.cpp:2648`, `:321-322` |
| a key that never landed (the browser's post to the board's `/enroll-key` fails) | **yes, in effect**: `/enroll-key` is the only writer of the key, so the board stays unenrolled while the server has counted an enrolment | `ConfigurationWebServer.cpp:3966-3969`, `:3990`, `:4020` |
| the server rejects the stored key | **no**, but the page then says the board needs re-verifying and offers enrolment, which overwrites the key | latch `AircraftManager.cpp:3100`, surfaced via `main.cpp:817`; overwrite `ConfigurationWebServer.cpp:4020` |

The key landing does not restart the board (`ConfigurationWebServer.cpp:4029-4031`: "No reboot -- AircraftManager
re-reads the key when it re-initialises").

### Between its two enrolments: predicted before the read (ruled ~19:29Z)

The ruling asked for authenticated requests from this board between 23:30Z on 11-05 and 16:30Z on 11-06, predicting
0 if the key never landed. **The source excluded 0 before the read:** `fw:` rows are written only past the auth gate
(`proxy/src/index.ts`, `setDeviceAttribution` then `recordFleetFirmware`), and the 03:30Z read already showed this
board's `fw:` row. `lastSeen` moves only when the previous value is at least an hour old (`fleet.ts`, `REFRESH_MS`).

Frozen at 19:32:27Z, before the tool was written (19:32:47Z) or run (19:32:54Z). Analytics Engine points whose `blob5`
(the verified device id) is this board's, counted in four spans; the id was used only inside the query and never
printed.

| | span | predicted |
|---|---|---|
| **C1** | ledger rows with a first enrolment in 23:30Z on 11-05 | exactly 1 |
| **C2** | the id matches the attribution pattern (`metrics.ts`, `setDeviceAttribution`) | true |
| **C3** | fleet points with a `blob5` over the same span | > 0 |
| **B1** | first enrolment to 00:31Z | >= 1 (the request that created the `fw:` row) |
| **B2** | 00:31Z to 03:28Z | 0 (any authenticated request would have moved `lastSeen` by the 03:30Z read) |
| **B3** | 03:28Z to the second enrolment | no prediction |
| **B4** | the second enrolment to the read | no prediction |

Outside the set, also frozen: B1 = 0 beside an existing `fw:` row would mean the two instruments disagree, so no
conclusion; B2 > 0 would mean authenticated requests without a firmware header.

### Between its two enrolments, read (19:32Z)

| | read | verdict |
|---|---|---|
| **C1** | 1 row: first 23:30Z on 11-05, last 16:30Z on 11-06, 2 enrolments | **as predicted** |
| **C2** | true | **as predicted** |
| **C3** | 23,728 | **as predicted** |
| **B1** | **24** (1 boot, and blips, config, airports, enrich) | **as predicted** |
| **B2** | **0** | **as predicted** |
| **B3** | **52** (3 boots) | no prediction |
| **B4** | **706** | no prediction |

The `fw:` row at the read: model `s3-128`, first seen 23:30Z on 11-05, last seen 19:05Z on 11-06, no firmware change.

**What it shows:** the first key landed at 23:30Z and kept working, through three boots, until the second enrolment.
This board is **not** a key-never-landed case. The ruling's premise read the 03:30Z last-seen as "never used"; the
reviewer accepted the correction (~19:43Z).

**My miss:** B4's meaning table was incomplete. The device key is HMAC(`DEVICE_KEY_SECRET`, id)
(`proxy/src/deviceauth.ts`), so a second enrolment mints the same key, and B4 = 706 is produced equally by "the
second key landed" and "the board still held its first". B4 says only that the board authenticates now.

### What caused the second enrolment: G1, frozen before the read (approved ~19:43Z)

Two causes remain: an erase before 16:30Z, or the enrolment page opened again while the board still held its key. A
refusal of the key is unlikely (production is on watch and the derived key changes only with the secret) and was not
read. Because the key landing does not reboot the board, the first authenticated request after an enrolment is a
non-boot request under both causes, so whether it is a boot (G2) cannot separate them: G2 is read and reported, not
graded. Graded on G1 alone, in minutes, never as a clock time (the board's cadence on that day: 15 s when idle):

| G1: minutes from the board's last authenticated request before 16:30Z to the enrolment | meaning |
|---|---|
| **<= 3** | the board was authenticating with its key at the enrolment: an erase is **excluded** (it leaves no authenticated request between the erase and the key landing, and a reflash, Wi-Fi setup and enrolment take longer), and so is the refusal latch (it needs a run of rejected requests, which carry no id). Remaining cause: the page opened again on a working board |
| **> 3** | does **not** discriminate: an erase, a board that was simply off (B3 holds 52 requests over about 13 h, so off is likely), or the refusal latch. G1 is reported and no cause is stated |

Stated before the read: given B3's sparsity, "> 3, no cause" is the more likely outcome.

### G1, read (19:51Z): an erase is excluded; G2 is off my premise

Controls: the same single ledger row, its last enrolment still 16:30Z with 2 enrolments, and the board's points
between its two enrolments reproduce the 19:32Z read exactly (76 = 24 + 0 + 52).

| | read | verdict |
|---|---|---|
| **G1** | **2.4 min** | **"<= 3"**: an erase is excluded, and so is the refusal latch. The remaining cause is the enrolment page opened again on a working board |
| **G2** | **1.6 min**, and that request **is a boot** | reported, not graded, and **off my stated premise** |

**My miss:** I wrote that the first authenticated request after an enrolment would be a non-boot request under both
causes. The board runs firmware 18 (its `fw:` row), whose key landing does not reboot, so the key landing did not cause
this boot: something else restarted the board 1.6 min after it enrolled. That does not touch G1's grading, which
rests on the requests before the enrolment. The boot's reset reason (one token from a known set, no id) would say
whether it was a power cycle, a software restart or a crash. Its meanings were frozen at 19:52Z; it read `SW`, a
software restart (R1, below).

So the customer's working, grandfathered board enrolled a second time **while it held a working key**, most likely by
opening the enrolment page again. Under enforce that is a 409, as the next section states.

### Restarts around setup: boot reasons, predicted before the read (rulings ~19:55Z, ~19:58Z, ~19:59Z)

Daniel reported that the Bend feeder restarted by itself several times during its setup on 11-04, and recalls that it
was right after activation; with the customer board's boot 1.6 min after its second enrolment, that is two boards
restarting around setup. Read-only, counts and reset-reason tokens only; the feeder was not touched, and its id came
only from the operator's token file and was never printed.

**What a boot record can see, from v18's source.** The Worker writes a boot point only past the auth gate
(`recordBoot`, `proxy/src/metrics.ts`), from the `X-Blip-Boot` header, which the board sends once per boot on its first
check-in and keeps pending until a check-in is acknowledged (`include/BootReportPolicy.h`). The token is that boot's
reset reason (`OtaUpdater.cpp:277-291`): `SW` is the firmware's own `ESP.restart()`; `PANIC`, `INT_WDT`, `TASK_WDT`,
`WDT` and `CPU_LOCKUP` are crashes. So a board with no key reports no boot at all, recorded boots are a lower bound,
and a crash loop shows as at most one recorded boot.

Frozen at 19:59:58Z, before the tool existed; read at 20:00Z, every control held:

| | predicted | read | verdict |
|---|---|---|---|
| **R1** the customer board, boots in the 10 min after its second enrolment | >= 1, the first at +1.6 min, a crash token | **1**, at **+1.6 min**, **`SW`** (fw 18) | **OFF**: a software restart, not a crash |
| **R2** the feeder, boots from 30 min before its first enrolment (11-04) to 2 h after | 0 before (invisible); >= 1 after, at least one a crash token | **2**: **+0.0 min `SW`**, **+3.4 min `SW`** (fw 18) | **OFF**: no crash token |
| **R3** the fleet, 14 days, boots by reason | `SW` most common; >= 1 device with a crash token | 335 boots, none without a reason: `SW` 142, `POWERON` 118, `USB` 71, `PANIC` 2, `TASK_WDT` 2, all s3-128 fw 18; **3 devices** with a crash token, neither the feeder nor the customer board | **as predicted** |

**My misses:** I predicted crashes for both boards, following the report, and both read `SW`. And my table missed a
property of the instrument: a boot point is stamped when the board **delivers** its reason, not when it booted. The
feeder's "+0.0 `SW`" is a restart from before its key, delivered at its first authenticated check-in; that fits the
restart v18 makes after its setup portal succeeds (`main.cpp:557`). Restarts before a board's key remain invisible to
this instrument, so a crash loop during the feeder's Wi-Fi setup is neither shown nor excluded.

**What it shows:** after activation, each board restarted itself by software once within minutes (the customer board
at +1.6 min, the feeder at +3.4 min), and neither restart was a crash. v18's software restarts each print their own
serial line just before `ESP.restart()` (the portal-success restart, `main.cpp:557`; the deferred update reboot,
`OtaUpdater.cpp:261-270`; a reset tier, `main.cpp:689`; Wi-Fi down for 10 min, `main.cpp:730`; the portal failing,
`main.cpp:540`; the touch watchdog, `AircraftManager.cpp:1471`/`1503`), so a serial capture names which one fired. The
fleet's 4 crash boots on 3 devices are a separate firmware finding, reported on their own.

### The design property, stated as a property

**The gate writes `activated` before the key reaches the board.** `claimActivation` runs at the top of the
enrolment (`proxy/src/enroll.ts`, the `gate` line), before the key is derived and returned to the browser, and the
browser then has to post it to the board's `/enroll-key`. So, under enforce:

- if a key never reaches the board, the board's next enrolment is refused with 409 `already_activated` and the unit
  needs a support reset, through no fault of the customer, at first setup;
- **a re-opened enrolment page on a working board also meets 409**, and so does a board erased and set up again.

Recorded, not designed: no change is proposed here. Daniel decides after the data.
