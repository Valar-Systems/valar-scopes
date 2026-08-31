# Rotating `DEVICE_KEY_SECRET` on the bench

**What this is.** The runnable procedure for a fleet-wide device-credential rotation,
and — the first time it is run — the bench proof for the cloud-401 handler (`4391170`).

The two are deliberately the same exercise. A rotation is the *only* thing that produces
a real, sustained, server-side 401 on a real board. Simulating one with a deliberately
wrong key proves the firmware reacts to a 401; it does not prove the thing we actually
ship, which is **a board that was working, stopped being trusted, said so, and recovered
in one action.** Run the real event once and the whole path is exercised end to end.

Rotation recurs (a leaked key, a compromised secret, an operator change), so this is
written as a standing procedure with the one-time A3 assertions marked **[A3]**.

---

## 0. Preconditions — do not start until all five are true

| | Precondition | Why |
|---|---|---|
| ☐ | **v7 is cut, published, and `scripts/verify-release.sh v7` PASSES** | See below. Non-negotiable. |
| ☐ | `A1` is deployed (`c046e4b` + `1c76d4a` + `d513aa2`) and `/healthz` reports it | Rotating on top of an undeployed tree means two variables. |
| ☐ | Both boards are on the current firmware and **currently authenticating** | Step 1 baselines this. A board that is already broken proves nothing. |
| ☐ | You have ~40 minutes and both boards on serial | The debounce is 15 min by design; recovery adds ~10. |
| ☐ | **A camera to hand** | See below. The screen states are the deliverable and they are not reproducible. |

### Photograph the screen at every stage

This is the first — and, if it goes well, the only — time the full credential-recovery
path runs on real hardware with someone watching. Every board that ever shows
`NEEDS VERIFY` after this is a customer's, unwatched.

So the photo record is not documentation of the run, it **is** part of the result. Four
shots, all of the screen itself (a phone photo, not a screenshot — there isn't one):

| | Stage | What it has to show |
|---|---|---|
| 📷 1 | §1 baseline | a live picture, **no banner** — the state we claim to be leaving |
| 📷 2 | §4 mid-ladder | an **amber** `STALE Nm`, ideally two shots as the count climbs |
| 📷 3 | §4 at the latch | `NEEDS VERIFY`, **and the red `NO DATA` shot immediately before it** |
| 📷 4 | §5 after recovery | banner **gone**, live picture back |

Shot 3 is the pair that matters — see the banner-order witness in §4. One photo of
`NEEDS VERIFY` on its own proves the firmware can draw those pixels and nothing else.

☐ Both boards in shot 3, so the control state is on the record too.

### Why the release must land first

**A rotation and a release fail identically from the outside: the board stops showing
live data.** If both are in flight you cannot tell a bad image from a refused key, and
the natural next move — reflash — destroys the evidence for whichever it actually was.
Worse, `NEEDS VERIFY` on a freshly OTA'd board reads as "the new firmware broke auth",
which is the most expensive wrong conclusion available here.

Same family as the entries in CLAUDE.md: two different failures producing one
observation. Serialise them and each has a clean control.

### Blast radius, stated before you do it

Rotation invalidates **every** key derived from the old secret, simultaneously and
permanently. `deviceauth.ts` holds one secret and recomputes; there is no dual-secret
grace window and no per-device carve-out.

**CORRECTED 2026-08-31 — IT WAS 2 BENCH BOARDS AND IT IS FIVE IDENTITIES, ONE OF
THEM IN SOMEBODY ELSE'S HOUSE.** The old figure below was reasoned from the bench
inventory — the boards visible from the desk — rather than measured from the
registry. Do not repeat that; **query it**, every time, because the answer changes
without anyone editing this file:

```sh
npx wrangler kv key list --binding=ENRICH_KV --env production --remote --prefix "enr:dev:"
```

Measured on 2026-08-31: **five enrolled ids** — three bench boards, the synthetic
identity, and **one unit in the field**. The registry is `enr:dev:*`; the other
`enr:` keys are daily counters, and `lb:dev:*` is a useful independent
cross-check (it added nothing, which is what makes it worth running).

### Reading the ledger: a SINGLE enrolment means a WORKING unit, not a dead one

This is the part that goes wrong silently, so it is written out.

`lastAt` in an `enr:dev:` row is **the last ENROLMENT, not the last time the
device was seen.** A device enrols when it needs a key and never again while that
key works. So the counts inverted the obvious reading:

| enrolments | what it actually indicates |
|---|---|
| 1,556 / 1,431 | a **bench board**, reflashed all week, re-enrolling each time |
| 195 | the synthetic identity, minted by hand repeatedly |
| **1** | a unit that enrolled once and has been **working ever since** |

Read casually, "last seen 2026-08-27, 1 enrolment" looks like something that
turned up once and died. It is the opposite: it is the signature of the only
deployment on the list that has never needed attention.

**Confirm each single-enrolment id against the bench log for that timestamp**
before calling it a field unit. That is what identified the two on this list:
one id enrolled at 2026-08-24T17:34Z and the COM16 capture shows `HTTP 401` at
that exact minute, i.e. our own board being flashed; the other enrolled at
2026-08-27T02:47Z with **no bench log written anywhere near that window**, so
nothing of ours was being flashed and it is somebody's Blipscope.

> **IDS ARE NOT WRITTEN OUT IN THIS FILE.** The enrolment registry (`enr:dev:*`,
> queried above) is the source of truth, and it is current in a way a document
> cannot be. Use `0123456789abcdef` as a placeholder when an id-shaped literal is
> needed for illustration; refer to real boards by port or nickname.
>
> One of the ids this section originally named belongs to **somebody else's
> device**. Publishing your own device's id in a public repo is a shrug;
> publishing a friend's without asking is a different thing, and he would have no
> way of knowing it happened. That is the reason for this rule — not tidiness. A
> `pre-commit` hook now refuses id-shaped tokens (see `.githooks/README.md`).

### There is no server-side liveness record, and that is a real limit

Nothing in KV says which of the five is online. Per-device rate limiting is a
Workers **rate-limiter binding**, not a KV namespace, so it enumerates nothing.
The only liveness signal is the Analytics Engine `dev` dimension, and reading it
needs `Account Analytics:Read` on the API token, which the workstation token does
not currently carry (the query returns `code 10000, Authentication error`). Until
that is fixed, **"which devices are alive" is unanswerable from here, and an empty
analytics result must be reported as "cannot observe" rather than "nobody is
active."**

Each identity needs a re-verify (§5, §7, §8). At pilot scale this same procedure means every customer board
lights `NEEDS VERIFY` at once and every owner presses one button — which is exactly the
scenario the copy was written for, and the reason it names an action rather than a fault.

Any soak currently running on a board is ended by this. Plan for it.

---

## 1. Baseline — prove the boards are NOT in the state you are about to induce

**This step is the point.** A banner you have only ever seen in one state is not
evidence. Same shape as running the shared-key curl *before* deleting `BLIP_KEYS`.

Two terminals, one per board. `-f time` is load-bearing — it stamps every line with a
timestamp, giving you a clock **independent of the firmware's own arithmetic**:

```sh
pio device monitor -b 115200 -p COM118 -f time    # Board #2 — the control
pio device monitor -b 115200 -p COM119 -f time    # Board #1 — the one you recover
```

Record, for **both** boards:

- ☐ **Screen: no banner at all.** Not amber, not red. A live picture with aircraft on it.
- ☐ **Serial: no `HTTP 401` lines**, and blips arriving on cadence.
- ☐ **Config page** (`http://<device-name>.local`): **no** "needs re-verifying" text, **no**
  Verify button, **no** enrol prompt. The Access key box shows asterisks or is empty.
- ☐ **Note both device ids** from the config page (the read-only Device ID row). You need
  them in §5 and they are annoying to get once the board is unhappy.
- ☐ **Timestamp.** Write down the wall-clock time you finished this step.

> **[A3] This baseline is half the proof.** The claim under test is "the banner appears
> *because* the key was refused". Without a recorded absent state, the only thing a
> visible banner proves is that the firmware can draw those pixels.

---

## 2. Rotate

Generate the new secret and put it straight into your password manager. It must not
reach a file, a command line, or scrollback you keep:

```sh
openssl rand -hex 32
```

Set it **interactively** so the value never appears as an argument:

```sh
cd proxy
npx wrangler secret put DEVICE_KEY_SECRET --env production
# paste at the prompt
```

**Run it bare — no pipe, no `grep`, no `2>/dev/null`.** This is the exact command that
burned us: `wrangler` puts `Authentication error [code: 10000]` on stderr and a cheerful
banner on stdout, so a filtered read of a *failed* rotation looks like a successful one.
Read the whole output. You are looking for the success line **and** the absence of an
error, not for one of them.

- ☐ Command output read in full, no filter, exit code checked.
- ☐ Note the wall-clock time. This is **T₀**.

---

## 3. THE CONTROL — prove the rotation landed, before you wait for anything

**Do not skip this and do not reorder it.** If `secret put` silently failed, the boards
keep working, no banner ever appears — and after 15 minutes of watching nothing happen,
the natural conclusion is *"the latch is broken"*. That is wrong, unfalsifiable from the
board, and it costs the whole session.

Two worlds, one observation ("no banner"). Separate them here, in 30 seconds, with a
check whose result differs between them.

Mint a key for the smoke identity against the **new** secret, using the enrolment page in
a browser — no local copy of the secret required:

```
https://scopes.valarsystems.com/blipscope/enroll?id=beefbeefbeefbeef
```

Solve the Turnstile, copy the key. Expect `"status":"already_enrolled"` with an
incremented `enrollments` — re-enrolment is idempotent by construction
([enroll.ts:170](proxy/src/enroll.ts#L170)), so this is a re-derivation, not a re-issue.

Then, with the **old** smoke key still in `BLIP_KEY`:

```sh
# OLD key — MUST now be 401
curl -s -o /dev/null -w '%{http_code}\n' \
  -H "X-Blip-Key: $BLIP_KEY" -H 'X-Blip-Device: beefbeefbeefbeef' \
  'https://scopes.valarsystems.com/v1/blips?lat=44.06&lon=-121.32&r=160'
```

```sh
# NEW key (paste into a shell variable, not into this file) — MUST be 200
curl -s -o /dev/null -w '%{http_code}\n' \
  -H "X-Blip-Key: $NEWKEY" -H 'X-Blip-Device: beefbeefbeefbeef' \
  'https://scopes.valarsystems.com/v1/blips?lat=44.06&lon=-121.32&r=160'
```

- ☐ Old key → **401**
- ☐ New key → **200**

### Set `BLIP_KEY` from this mint NOW, not at §8

`smoke-prod.sh` is the only thing that checks production, and until `BLIP_KEY` holds a
valid device key it is not merely failing — it is **unavailable**. Leaving that to §8
means the whole bench runs with no way to ask production a question, and if anything
looks wrong at §4 or §5 the first diagnostic you would reach for is the one you have not
restored yet.

You have the key in your hand right now. Set both at Windows user level
(`HKCU:\Environment`) and open a **new** shell for the rest of the run — a running
session will not see the change, which is the other reason not to do this mid-bench with
two serial monitors already attached:

- ☐ `BLIP_KEY` = the key you just minted
- ☐ `BLIP_DEVICE` = `beefbeefbeefbeef`
- ☐ New shell opened; `smoke-prod.sh` available from here on

> **This composes with the drain caveat below rather than fighting it.** If the mint
> landed on a draining isolate, `BLIP_KEY` is now an old-secret key — and the re-test at
> the latch is already scheduled to catch exactly that. So the one check you were going
> to run anyway now doubles as the check on `BLIP_KEY` itself. If it flips to 401,
> re-mint and re-set both here and in the shell.

#### Not part of the rotation: any authenticated production check you owe

`BLIP_KEY` becoming valid here is the *first moment* several unrelated checks are
possible at all, so this is where they get run rather than being remembered later. They
are listed as a reminder, not as rotation steps — none of them can fail the rotation.

Current standing item: **the C185→C180 photo alias** (`7064a7e`, live since `6393905`).
It is code rather than data, so `/healthz` confirms only that the right commit deployed —
nothing checks that the alias resolves, and a broken one does not error. It silently
returns no photo on a type the library covers.

**Run it anchored, C180 beside C185.** The three outcomes are distinguishable only in
pairs, which is the whole point:

| C180 | C185 | Means |
|---|---|---|
| resolves | resolves | ✅ alias works |
| resolves | **no `p`** | ❌ **the alias** — the library is fine, the mapping is not |
| **no `p`** | no `p` | ⚠️ upstream/auth — **wrong layer, stop debugging the alias** |

Without the C180 arm, the second and third rows produce the same observation and the
alias takes the blame for whichever it actually was.

```sh
# as a FULL-BLEED device, since the square path is what the alias could break
H=(-H "X-Blip-Key: $BLIP_KEY" -H "X-Blip-Device: $BLIP_DEVICE"
   -H 'X-Blip-FW: 7' -H 'X-Blip-Model: s3-128')
curl -s "${H[@]}" "https://scopes.valarsystems.com/v1/enrich/<c185-hex>"
curl -s "${H[@]}" "https://scopes.valarsystems.com/v1/enrich/<c180-hex>"   # the anchor
```

Both should return `"p":"/v1/photo/photo:C180-…"` with `"pk":"type"` — the C185 arm
pointing at a **C180** key is the proof the alias fired, into the square rather than the
rectangle.

> **If no C185 is airborne, the check did not run.** Say so. An empty search is not a
> pass, and the honest state is "two tests and an unverified production path" — which is
> a fine thing to ship, and a bad thing to believe you confirmed. Same family as the
> anonymous-upstream entry in [CLAUDE.md](CLAUDE.md): a result that cannot distinguish
> "no data" from "not looked" is not evidence.

**Both must hold.** Old-401 alone could mean the Worker is broken; new-200 alone could
mean nothing changed. Together they say precisely one thing: the secret in front of
production is the new one. Only now does a board's silence mean something.

> If old→401 but new→401 too: the secret took but is not what you think it is. Stop and
> re-run `secret put` before touching the boards.

### The mint is exposed to the drain too — re-test this key at the latch

§4 describes edge isolates draining after a secret change, and frames it as something
that happens to the *boards*. It happens to **this mint as well**, and that is easier to
miss because the symptom arrives much later.

`handleEnroll` reads `DEVICE_KEY_SECRET` per request and derives fresh
([enroll.ts:163](proxy/src/enroll.ts#L163)) — it caches nothing, and the ledger row it
writes deliberately does not contain the key. So there is no stale *key* anywhere. But an
enrol request that lands on a **draining isolate** is served by the old binding, and
returns a perfectly well-formed 64-hex key derived from the **old** secret. Nothing about
the value says which one it is.

**The two checks above do not fully rule this out.** Old→401 proves *an* auth request hit
a new isolate; new→200 proves *some* isolate accepted the new key. They need not be the
same isolate, so a stale-minted key validated by a stale isolate scores both ticks.

The fix costs one command at a moment you are already sitting watching serial. **Re-run
the new-key curl at the latch** (§4, T₀ + ~15 min — the drain is long over):

- ☐ New key → **200** again, at the latch

A key minted on a draining isolate begins returning 401 once the drain completes; a good
one is unchanged. If it flipped to 401, the key is old-secret — re-mint it (the enrol page
again) and re-run both curls. Nothing about the boards is affected and A3 is not
invalidated; you simply had a bad `BLIP_KEY` for §8.

> **The boards are not exposed to this.** §5/§7 mint at T₀ + 15 min at the earliest,
> long past the drain. The customer recovery path this exercise exists to prove is
> clear of it — only the smoke identity, minted at T₀, sits inside the window.

---

## 4. Watch the debounce actually debounce

Both boards, untouched, for ~15 minutes. Here is the full expected sequence and, for
each stage, what a *wrong* observation would mean.

### Expected timeline

| Elapsed from first sustained 401 | Serial | Screen |
|---|---|---|
| ~0 s | `[WARN] Blipscope Cloud returned HTTP 401; keeping current picture` — then one per poll | last live picture, no banner yet |
| **~15 s** | 401s continuing | **`STALE DATA`** (amber) |
| **75 s** | 401s continuing | **`STALE 1m`** → `STALE 2m` … (amber, counting) |
| ~10 min | cadence drops to idle/night — 401s get sparser | `STALE 9m` |
| **600 s = 10 min** | 401s continuing | **`NO DATA - 10m`** (red) |
| **900 s = 15 min** | **`[cloud] KEY REFUSED for 900s over N fetches -- this board needs re-verifying`** | **`NEEDS VERIFY`** (red) |

Fetch count `N` at the latch: the cloud cadence is 5 s while the board is "active"
(within 10 min of the last touch), then 15 s idle or 60 s at night. Untouched from T₀
that is **≈ 125–140 fetches**. Any `N ≥ 5` satisfies the count gate — at every real
cadence **the time gate is the binding one**, which is the intended design: the count
gate exists only to stop a very fast cadence from latching on a handful of blips.

### Expect the streak to reset once or twice near T₀ — that is correct

Edge isolates drain for a few minutes after a secret change (the same effect `deploy.sh`
waits out for `/healthz`). During the drain, requests can land on old-secret isolates and
**succeed**. A success clears the streak *and* the timer, unconditionally, by design.

So the 15-minute clock starts from **the first 401 of the final unbroken run**, not from
T₀. Expect the latch at roughly T₀ + drain + 15 min. If you see `HTTP 401` lines, then a
quiet gap, then 401s again — nothing is wrong.

☐ **[A3] Note it if it happens.** A board that sees mixed 200/401 and does *not* latch is
a live demonstration of the transient case being ignored — the exact property the
thresholds were written for, arriving for free. It is stronger evidence than the latch
itself, because it is the failure direction that would reach a customer.

### Distinguishing "latched correctly" from "latched early" — three witnesses

The three are deliberately independent. Two of them do not consult the firmware's own
debounce arithmetic at all, so they still work if that arithmetic is the thing that is wrong.

**1. Banner order on the screen — needs no serial, trusts no code.**
`NEEDS VERIFY` and the stale ladder share one slot, so their **order** is the check:

- ✅ **Correct:** `NEEDS VERIFY` replaces **`NO DATA - 10m`** (red → red).
- ❌ **Latched early:** `NEEDS VERIFY` replaces an **amber** banner (`STALE DATA` or
  `STALE Nm`). `NO DATA` is defined at 600 s and the latch at 900 s, so a latch that
  pre-empts it is *definitionally* early. Nothing to measure — the wrong one is a
  different colour.

**2. The latch line's own numbers — self-describing, printed at the moment it fires.**

```
[cloud] KEY REFUSED for 900s over 137 fetches -- this board needs re-verifying
         ▲ elapsed seconds        ▲ consecutive 401s
```

- ✅ `for 9xx s over ≥5 fetches` — both gates held.
- ❌ `for 25s over 5 fetches` — the **time** gate did not hold; the count gate fired alone.
  This is the specific regression the 15-minute threshold exists to prevent.
- ❌ `for 9xxs over 1 fetches` — the **count** gate did not hold.

**3. Wall clock from your own log — the independent witness.** With `-f time`, take the
timestamp of the **first `HTTP 401` of the final unbroken run** and the timestamp of the
`KEY REFUSED` line. The difference must be **≥ 900 s** and must match the number the
firmware printed.

Witness 2 shares its variables with the gate it is reporting on, so it cannot catch a
fault in those variables. Witnesses 1 and 3 are derived from outside the firmware's
arithmetic — the screen from a *different* constant, the log from your machine's clock.
If 2 disagrees with 1 or 3, **believe 1 and 3.**

### Witness 0 — the `tls` counter, which rules out the confound the other three cannot

Read `tls=H/R` off the `[health]` line **at baseline and at every check during the wait.**
`H` counts TLS handshakes, `R` reuses. **If neither number moves between two health lines,
the board issued no HTTP request at all in that interval** — and a board making no
requests walks the stale ladder for reasons that have nothing to do with the key.

| serial during the wait | `tls=H/R` | Meaning |
|---|---|---|
| `HTTP 401` lines arriving | **climbing** | ✅ **the key.** Requests are reaching the server and being refused. The run you wanted. |
| **no 401 lines** | **frozen** | ❌ **the fetch path stopped.** Not the key, and not heap. A known open defect — see below. |
| no 401 lines | climbing, but `rej` up / `tlsOk=0` | heap pressure: enrichment deferred, cards go blank. **No banner should come from this** — positions are not gated. |

A 401 is by definition a *reply*, so it cannot occur without a request. That makes the
request counter independent of everything the debounce reasons about — the property the
other three witnesses lack, since banner order, the latch line and the wall clock are all
downstream of the same silence and therefore agree with each other in every world.

☐ `tls` at baseline: ____ / ____   ☐ at the latch: ____ / ____ (**must have moved**)
☐ `rej` at baseline: ____  ☐ at the latch: ____ (context, not a veto — see the correction)

> #### This section named a different witness, and the code contradicted it
>
> It first named `rej`, on the reasoning that *"a heap-starved board walks the same ladder
> in the same colours"*. **That is false.** `CanHandshake()` has exactly two gate call
> sites — [:4077](src/AircraftManager.cpp#L4077) (detail card) and
> [:5361](src/AircraftManager.cpp#L5361) (background enrichment sweep) — and **neither
> gates the position fetch**; [:1835](src/AircraftManager.cpp#L1835) only prints a warning.
> Heap pressure blanks cards and never touches the stale ladder.
>
> The observation that prompted the section survives, and gets **worse** under the
> correction. On 2026-08-17 board `.55` logged `DATA STALE` with `tls=2/115` **identical
> across a 30 s interval**, while `tlsOk=1` and `rej=0` — the heap gate healthy and never
> once fired. It was written up as heap twice. It was not heap: **the fetch path stopped
> by itself.**
>
> That is the shape of the **2026-07-09 stall** — *"fetches silent 22 min, loop healthy,
> task never dequeued"* — closed 2026-07-21 as not-reproduced and **never root-caused**,
> with its `[soak-state]` telemetry deliberately left in the tree to catch a recurrence.
> It caught one. Investigation open: `-DFETCH_TRACE` +
> [scripts/check-fetchtrace.sh](scripts/check-fetchtrace.sh).
>
> **Until that is root-caused, a stale ladder with a frozen `tls` counter is a known open
> defect and NOT evidence about the key.** Seeing it during a rotation makes the run
> inconclusive: capture the log and stop, rather than reading the banner in either
> direction.
>
> `rej` is still worth recording (it is `heaphealth::TrialRejectionCount()`, one increment
> site inside `CanAllocate`, reached only via `CanHandshake()` — nothing else can move it),
> but as context on enrichment health, never as a verdict on the banner.
>
> The general lesson, since this is now the second entry in this file to earn one: **the
> instrument was right and the explanation beside it was wrong**, which is the more
> dangerous of the two. A wrong number invites checking. A wrong *reason* gets reasoned
> from — at 11pm, by someone who was not here when it was written.

### At the latch — confirm all three surfaces, both boards

- ☐ Board #1 serial: `KEY REFUSED` line, numbers checked against the table above
- ☐ Board #1 screen: `NEEDS VERIFY`, red, and it **replaced `NO DATA`** not an amber banner
- ☐ Board #1 config page: reloads to the "needs re-verifying" state, **Verify button present**
- ☐ Board #2: all three, same
- ☐ Neither board rebooted (no boot banner in the serial log)
- ☐ New key → **200** again (the drain re-test from §3)

### Record what the banner REPLACED, not that it appeared

**The prior banner is the debounce's proof. The new one is not.** `NEEDS VERIFY` looks
identical whether the firmware waited 900 s or 25 s — a correct latch and the exact
regression the thresholds exist to prevent produce the *same photograph*. Only the state
it displaced separates them, and that state is gone the moment it is overwritten.

So this is the one observation in the run that cannot be recovered afterwards. The serial
log persists, the config page persists, the screen re-reads at any time — the previous
banner exists for as long as you are looking at it and then never again.

Write down all three, per board, at the moment it flips:

```
board #1   banner before the latch : NO DATA - 10m        colour: RED
           banner after            : NEEDS VERIFY         colour: RED
           wall-clock of the flip  : ..:..:..
```

- ☐ Prior banner **text** recorded (not just "the red one")
- ☐ Prior banner **colour** recorded — this is witness 1 and it is a colour comparison
- ☐ 📷 both shots (see §0): the red `NO DATA` *and* the `NEEDS VERIFY` that replaced it

> A results note reading *"NEEDS VERIFY appeared at 15 min"* has recorded the one part of
> this that was never in question. Amber → `NEEDS VERIFY` is a **failing** run, and it
> also appears, and at whatever time it appears that sentence describes it equally well.

---

## 5. Re-verify Board #1 ONLY

Leave Board #2 refused. Two boards, two states, one firmware — so a recovery on #1 cannot
be confused with time passing, a Worker change, or the isolates finishing their drain.

On Board #1's config page:

1. Press **Verify**. A popup opens `scopes.valarsystems.com/blipscope/enroll?id=<device id>`.
2. Solve the Turnstile. The popup posts the key back; the page saves it to `/enroll-key`.

Expected serial, in order:

```
[enroll] device key stored
… (re-initialise) …
[cloud] key accepted again -- clearing the re-verify state
```

- ☐ `[enroll] device key stored`
- ☐ `[cloud] key accepted again -- clearing the re-verify state` within ~1 poll
- ☐ Banner **gone** from the screen (not "changed" — gone; the ladder goes quiet too once
  fresh data lands)
- ☐ Config page, after its reload: back to the working state, **no** Verify button
- ☐ **No reboot in the log.** This is a distinct assertion, not a nicety — "reboot it" is
  not an instruction we want to give someone who has just fixed the actual problem.

> **[A3] The latch is not cleared by the save.** `/enroll-key` raises `configChanged`;
> the latch clears only on a **successful fetch** ([AircraftManager.cpp:2012](src/AircraftManager.cpp#L2012)).
> So the banner disappearing is evidence that the *new credential was accepted by the
> server* — not merely that a value was written to NVS. That distinction is why this step
> is worth watching rather than assuming.

**If Turnstile cannot load on the bench machine**, the page falls back to the paste route
(open the enrol URL on a phone, paste into Access key). That path works, but note it in
your results: it writes the `cloud-key` override rather than the factory slot, so it
exercises a *different* branch from the customer one.

---

## 6. Confirm Board #2 is still refused — the control

Go back to Board #2 **without touching it**:

- ☐ Still showing `NEEDS VERIFY`
- ☐ Still logging `HTTP 401`
- ☐ Config page still offering Verify

☐ **[A3]** This is what makes §5 mean something. It rules out the two boring explanations
for #1's recovery — that the banner is transient, or that something changed server-side
that would have healed both. Per-board state, per-board recovery.

---

## 7. Re-verify Board #2

Same as §5. Confirm the same three surfaces clear. Both boards are now on the new secret.

- ☐ Board #2 recovered, no reboot

---

## 8. Restore the smoke identity and re-prove production (this closes A5)

**Both variables were set back in §3**, so this step is the confirmation, not the first
opportunity. If you skipped ahead, go and do §3's "Set `BLIP_KEY` from this mint NOW"
block before reading on — `smoke-prod.sh` refuses to run without both, and a stale
`BLIP_KEY` is a blocked run rather than a failing one.

- ☐ `BLIP_KEY` / `BLIP_DEVICE` still hold the §3 values (re-minted if the latch re-test
  came back 401)

```sh
./proxy/scripts/smoke-prod.sh
```

- ☐ **All green**, auth path asserted as `device`. Expect **30** checks, not the 29
  you saw last run — `77cf18f` added the `/healthz` upstream-posture check.

> **Why this is a confirmation and not a restoration.** Setting the variables here would
> mean the entire bench — the latch, both recoveries, the control board — ran with no way
> to ask production a question. The one tool that checks prod would have been unavailable
> for precisely the window in which you most want it, and its first run would land after
> every interesting state had already been cleared.

---

## 9. Results to record

Append to this file, or to the PR that carries the run:

```
ROTATION <date>
  T0 (secret put)            ..:..
  §3 old key → 401           y/n     §3 new key → 200  y/n
  drain resets observed      0 / 1 / 2 …
  first 401 of final run     ..:..   (board #1)
  KEY REFUSED line           ..:..   → firmware said "for ___s over ___ fetches"
  wall-clock delta           ___ s   (witness 3; must be ≥900 and match the above)

  BANNER BEFORE THE LATCH    ______________  colour: RED / AMBER   ← witness 1
  banner after               NEEDS VERIFY    colour: RED
    (AMBER before = FAILING RUN. Stop, capture, do not re-verify.)

  new key re-tested at latch → 200 / 401     (401 = old-secret mint, re-mint for §8)
  board #1 recovered at      ..:..   no reboot: y/n
  board #2 still refused at  ..:..   y/n
  smoke-prod                 __/30
  photos 1-4 filed           y/n
```

---

## Failure table — what each wrong observation actually means

| Observation | Meaning | Next move |
|---|---|---|
| §3 old key still 200 | Rotation did **not** land | Re-run `secret put` bare. Do not touch the boards. |
| No 401s on either board after several minutes | Isolates still draining, or §3 was skipped | Wait; if §3 was skipped, run it now |
| `NEEDS VERIFY` replaces an **amber** banner | **Time gate broken** — the count gate fired alone | Real bug. Capture the log, do not re-verify (preserve the state). |
| Latch line says `for 25s` | Same bug, second witness agreeing | As above |
| Latch never fires but 401s are continuous past 20 min | Latch condition or the streak is broken | Capture; check for interleaved successes resetting it |
| Board latches, then clears **on its own** | Something is returning 200 — check for a stale isolate or a `cloud-key` override | Investigate before re-verifying |
| §5 stores the key but the banner stays | The new key is not being used, or the server still refuses it | Check `/enroll-key` cleared `cloud-key`; re-check §3 |
| Board **reboots** during recovery | Regression — recovery must be in-place | Real bug, capture the boot log |
| Board #2 recovers without being touched | The state is not per-board | Real bug; §5's result is void |

---

## What this procedure deliberately does not do

- **No dual-secret grace window.** It would halve the value of a rotation (the leaked key
  keeps working through the window) and is a lot of machinery for a fleet this size.
  Revisit when a rotation would inconvenience customers rather than us — and note that
  per-device **revocation** already exists for the single-board case, which is the reason
  rotation is not the routine tool.
- **No attempt to rescue a specific board's old key.** The id is a pure function of the
  efuse MAC and the build salt; the key is a pure function of the id and the secret.
  There is nothing to preserve.
- **No telemetry on how often the banner is seen.** Operational counters on the Worker
  side (enrolment ledger, `enrollments` count) are the record. See CLAUDE.md.

---

## FINDING 2026-08-28 — the workstation held a real board's key, not the smoke identity

**`BLIP_KEY` / `BLIP_DEVICE` on the workstation held the COM119 id — "Bend
Radar2", i.e. COM119, a live soak board — where this runbook specifies the
synthetic `beefbeefbeefbeef`.**

Found the hard way: a shell command printed the environment block while checking
whether an unrelated token was set, and four secrets landed in a session
transcript (see CLAUDE.md, *a presence check prints a boolean, never a value*).
The drift is what turned that from "rotate a smoke credential" into "rotate a
credential belonging to a board mid-experiment".

### Why the drift is expensive, and it is not about this one incident

A per-device key is **derived**: `HMAC(DEVICE_KEY_SECRET, deviceId)`. It cannot be
rotated on its own. Whatever the reason for wanting it gone, there are exactly
two levers and both are blunt:

| lever | blast radius |
|---|---|
| rotate `DEVICE_KEY_SECRET` | every device key in the fleet; all three bench boards drop off cloud at once |
| revoke the device id | that board only, until re-enrolled |

The synthetic identity exists precisely so that neither lever ever has to be
pulled for a workstation exposure: `beefbeefbeefbeef` is not a board, so revoking
it costs a mint and nothing else. Holding a real board's key in an env var
silently converts a cheap incident into one that has to be scheduled around an
experiment — here, the #245 A/B and the #264 capture.

### Disposition

- **Do not rotate now.** The exposure reaches one bench board: an attacker could
  pull feed data on its rate-limit bucket and post to the leaderboard as "Bend
  Radar2". Nothing customer-facing, nothing fleet-wide.
- **Mint `beefbeefbeefbeef` and put that in `BLIP_KEY`/`BLIP_DEVICE`**, which is
  what §3 of this runbook already says to do. No board is touched.
- **Revoke the COM119 id and re-enrol COM119 after the A/B closes.**

> ### CORRECTED 2026-08-31: REVOCATION IS CONTAINMENT, NOT REMEDIATION
>
> **The line above, and the sequencing note below it, are wrong about what
> re-enrolment does.** Both assume a re-enrol mints something fresh. It does not:
>
> ```
> deriveDeviceKey(secret, id) = HMAC(DEVICE_KEY_SECRET, id)
> ```
>
> is a pure function, and the device id is derived on-device from the efuse MAC
> and survives a full erase. `handleEnroll` refuses a revoked id outright, so
> re-enrolling requires **un-revoking first** — at which point the same key comes
> back, character for character. It is the string that leaked.
>
> So revoke-then-re-enrol **refuses** the leaked credential; it does not **retire**
> it. That is containment, and containment is worth having — a refused key opens
> no sockets — but it is not the repair, and the difference matters when
> somebody later asks whether the incident is closed.
>
> **Only a new `DEVICE_KEY_SECRET` kills the string.** So the remediation folds
> into the secret split rather than preceding it, and the ordering argument below
> — which is about bench convenience, "a second moving part introduced during a
> repair" — was answering a smaller question than the one that mattered.
>
> The corrected order is: **revoke (contain) → split the secrets, minting a NEW
> production secret (remediate) → un-revoke → re-enrol against the new secret.**

### Sequencing note: this interacts with the shared-secret issue

[#266](https://github.com/Valar-Systems/valar-scopes/issues/266) records that
staging and production hold the **same** `DEVICE_KEY_SECRET`, so a
production-derived key authenticates against staging (and, the direction that
matters, a staging compromise mints production identities).

**Do the revocation FIRST, then the split.** Revoking the COM119 id today is
a straightforward re-enrol against one shared secret. If the secrets are split
first, the re-enrol mints a key valid in only one environment and the bench has
to start tracking which one it is addressing -- a second moving part introduced
during a repair.

The revocation is also the natural moment to *check* the split once it lands,
rather than assume it: mint against staging, present that key to production, and
require a **401**.

### The check that would have caught it

`smoke-prod.sh` runs against whatever `BLIP_DEVICE` holds and passes either way —
a real board's key authenticates exactly as well as the smoke identity's, which
is why nothing complained for however long this had been true. If the smoke path
is meant to use the synthetic identity, the script should **assert** that:

```sh
[ "$BLIP_DEVICE" = "beefbeefbeefbeef" ] || { echo "BLIP_DEVICE is not the smoke identity"; exit 2; }
```

Same shape as everything else here: the passing and failing states produced the
same observation, so the observation could not tell them apart.
