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

## 0. Preconditions — do not start until all four are true

| | Precondition | Why |
|---|---|---|
| ☐ | **v7 is cut, published, and `scripts/verify-release.sh v7` PASSES** | See below. Non-negotiable. |
| ☐ | `A1` is deployed (`c046e4b` + `1c76d4a` + `d513aa2`) and `/healthz` reports it | Rotating on top of an undeployed tree means two variables. |
| ☐ | Both boards are on the current firmware and **currently authenticating** | Step 1 baselines this. A board that is already broken proves nothing. |
| ☐ | You have ~40 minutes and both boards on serial | The debounce is 15 min by design; recovery adds ~10. |

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

Today that is: **2 bench boards + the `beefbeefbeefbeef` smoke identity.** Each needs a
re-verify (§5, §7, §8). At pilot scale this same procedure means every customer board
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

**Both must hold.** Old-401 alone could mean the Worker is broken; new-200 alone could
mean nothing changed. Together they say precisely one thing: the secret in front of
production is the new one. Only now does a board's silence mean something.

> If old→401 but new→401 too: the secret took but is not what you think it is. Stop and
> re-run `secret put` before touching the boards.

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

### At the latch — confirm all three surfaces, both boards

- ☐ Board #1 serial: `KEY REFUSED` line, numbers checked against the table above
- ☐ Board #1 screen: `NEEDS VERIFY`, red, and it **replaced `NO DATA`** not an amber banner
- ☐ Board #1 config page: reloads to the "needs re-verifying" state, **Verify button present**
- ☐ Board #2: all three, same
- ☐ Neither board rebooted (no boot banner in the serial log)

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

`smoke-prod.sh` is the only thing that checks production, and it refuses to run without
both variables — so a stale `BLIP_KEY` is not a silent failure, but it is a blocked one.

Set both at Windows user level (`HKCU:\Environment`), using the key you minted in §3:

- ☐ `BLIP_KEY` = the **new** `beefbeefbeefbeef` key
- ☐ `BLIP_DEVICE` = `beefbeefbeefbeef`
- ☐ Open a **new** shell (the running session will not see the change)

```sh
./proxy/scripts/smoke-prod.sh
```

- ☐ **29/29 green**, auth path asserted as `device`

---

## 9. Results to record

Append to this file, or to the PR that carries the run:

```
ROTATION <date>
  T0 (secret put)            ..:..
  drain resets observed      0 / 1 / 2 …
  first 401 of final run     ..:..   (board #1)
  KEY REFUSED line           ..:..   → firmware said "for ___s over ___ fetches"
  wall-clock delta           ___ s   (witness 3; must be ≥900 and match the above)
  banner it replaced         NO DATA / STALE  ← must be NO DATA (witness 1)
  board #1 recovered at      ..:..   no reboot: y/n
  board #2 still refused at  ..:..   y/n
  smoke-prod                 __/29
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
