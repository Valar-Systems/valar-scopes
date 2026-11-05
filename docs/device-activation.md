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
