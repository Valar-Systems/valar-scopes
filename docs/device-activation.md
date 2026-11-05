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
