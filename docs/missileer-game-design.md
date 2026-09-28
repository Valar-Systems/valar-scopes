# Missileer game: the receiving end

**Status: design pass of 2026-09-28, for Daniel's review. No code changes until it is
approved.** Daniel can overrule any of it; it is his product.

This rewrite replaces the launch-side design of 2026-08-04 to 2026-09-28. That version is
in git history at valar-scopes `8c95a19` (the merge of #364) and is not a source for
anything below. Section 9 maps each of its mechanics and data structures to keep, change
or remove, and that table is the plan for the code pass.

---

## 0. The rulings this pass follows (2026-09-28, verbatim)

1. "Execution window: keep 2 s, recorded as our own design choice."
2. "Window shape: T to T+2 s on device and server both; fix the server."
3. "Terminal countdown: 33 s, from the measured standby phase, cited to the clips."
4. "Retarget times and clock-drift figure: dropped. Targeting is out."
5. "Yes: 'nothing from Nuclear Companion' covers all of it — the six-step loop, the 3+3
   enable minigame, NAM/FDM/EAM classes, vote/inhibit/dead-man, '50 LCCs', the console
   part names, the disclaimer style, and the three rows on the sources page. And the
   editorial line applies to the game as it does to the site: no launch procedure,
   targeting or warheads. The game is the receiving end — copy, authenticate (R10),
   acknowledge, report — built from what our recordings show and from cleared sources.
   Do it as a design pass in the design doc first, for Daniel's and my review, before any
   code changes. Daniel can overrule me on the game's scope; it's his product."
6. "values set after the design pass."

Earlier, same day: *"Game timings: replace with our own numbers measured from our own
recordings, documented in the game design doc with the clips they came from. Nothing from
Nuclear Companion."*

---

## 1. Scope

**The game is the receiving end of an HFGCS broadcast.** The player hears a broadcast,
copies it character by character, authenticates, acknowledges, and reports. That is the
whole loop.

**Out, by the editorial line** (OPEN-SOURCES.md: "nothing about launch procedure,
targeting or warheads"; ruling 5): launch procedure, launch keys, launch votes, targets,
target classes, war plans, retargeting, flight, re-entry, detonation, miss distance,
CEP, payloads, sorties and missile inventory.

**Out, by source** (ruling 5): everything that came from Nuclear Companion. That is the
six-step loop, the 3+3 split-knowledge enable, the NAM/FDM/EAM class taxonomy,
vote/inhibit/dead-man, "50 LCCs", the console part names (VDU, RMP, WSP, LEP, LCP, CLS,
OID, TODC, FDD), and the disclaimer style ("procedure depicted is a guess; deliberately
so").

What stays from the old design is our own work that does not depend on any of that: the
tone rails, the clock and scoring-precision measurements, the touch-hardware bench work,
the callsign rules, and the principle that ranking and headline statistics do different
jobs.

---

## 2. Sourcing rule

Every factual claim in this document carries one of three labels.

| Label | Meaning | How it is cited |
|---|---|---|
| **REC** | Our own recordings of on-air HFGCS traffic and the station's decodes of them. CC0 1.0 (PROVENANCE.md, ruling 5 of 2026-09-28). | Message id (`M…Z`), clip id, and the served archive id where there is one. |
| **CLR** | A source cleared in missileer-watch `docs/sources/OPEN-SOURCES.md` (public domain or CC0 only), with a row in `docs/sources/PROVENANCE.md` for anything used as an asset. | The source's name as listed there, with page or section. |
| **OURS** | Our own design. Not a claim about the real world. | "Ours", with the date it was decided or "proposed" if it is new in this pass. |

Anything else is not cited. In particular:

- **Wikipedia and other CC BY-SA text** is "read, don't copy" (OPEN-SOURCES.md), so it is
  not a game source.
- **Nuclear Companion, AAFM, siloworld, blogs and essays** are reference reading only
  (OPEN-SOURCES.md, "Exists, not usable, or ruled out").
- **Priyom, Number Stations Research, NEET INTEL and Milcom Monitoring Post** are the
  listener references in valar-eam-feed `docs/message-types.md`. They are not on the
  cleared list, and their paraphrases are "in progress" (PROVENANCE.md §5, items 10 and
  14). Where they agree with a recording, this document cites the recording.
- **AFGSCI 13-5301 Vol 3** is cleared for reading only: "Out for site and game by the
  editorial line" (OPEN-SOURCES.md).

**Where the cleared list stands for the game.** Every fact in section 3 is REC. No game
fact rests on a CLR source yet. The cleared sources closest to the game (the NPS
*Missile Plains* historic resource study, the NMUSAF "Missile Combat Crews" fact sheet,
SAC's *Combat Crew* magazine, the SAC ICBM operations guide, HD 2001) are about crews and
facilities, not HF reception. Any one of them used later gets cited here by page, and any
asset taken from one gets its PROVENANCE row in the same PR.

---

## 3. What our recordings show

This is the factual base of the game. Each row is REC. The measured timings are in
section 6, with every message and clip in the evidence table (section 12).

| # | What is on the air | Recording |
|---|---|---|
| R1 | An EAM opens with a net call: "All stations, all stations, this is <callsign>, <callsign>, break." | Ear transcript of QHXLPO, 1502Z 23 Sep 2026, archive `eam-7ca8cd24` (valar-eam-feed `test/fixtures/ear/2026-09-23T1502Z_desktop-kiwi-iceland+kph.txt`). Ear transcript of QHDJAS, 1404Z 25 Sep 2026 (`test/fixtures/ear/2026-09-25T1404Z_kph.txt`). |
| R2 | A directed EAM names its recipient: "For <callsign>, for <callsign>", after the net call and again after "I say again". | QHXLPO ear transcript ("4 brad, 4 brad"), identified as the address by station structure rule 8 (2026-09-27). QHCPBK, M20260924T090156Z ("Four headway"). |
| R3 | The six-character preamble is read three times, each followed by "standby", and then "message follows". | QHXLPO and QHDJAS ear transcripts. The 13 messages with a measured standby phase (section 12). |
| R4 | The full string, preamble included, is read, then "I say again", then read again. | QHXLPO ear transcript. The 8 messages with a measured reading (section 12). |
| R5 | The length is not fixed. Some broadcasts announce it: "Message of 35 characters follows" … "This completes message of 35 characters, this is <callsign>, out." | QHGCGK, clip `20260924T180750Z_kph_130s` (corpus count C1, valar-eam-feed `docs/eam-format-observed-2026-09-23.md`). M20260924T163613Z announced 32 characters. |
| R6 | A broadcast ends "this is <callsign>, out". | QHGCGK (R5). HID8HI, clip `20260924T001339Z_kph_059s` ("…this is surprise, out"). |
| R7 | An authentication exchange is three characters, the word "with" (or "and"), then one character, and it runs both ways: a challenge and a reply. | Structure rule 10 (Daniel, by ear, 2026-09-28; valar-eam-feed `docs/standing-rulings.md`). CACJCA, M20260923T211729Z, clip `20260923T211729Z_kph_079s`, served `eam-b9091b78`: "my station authenticates, Charlie, Alpha, Charlie, with Juliet … my station requests you authenticate, November, Lima, Bravo … I authenticate, November, Lima, Bravo, Echo". HID8HI, M20260924T001339Z, served `eam-b95536db`: "THI with D. Say again, THI with D." |
| R8 | An authentication sits inside a net check-in or check-out, which ends in an acknowledgement and a question: "I have you, entering the net, … how copy, over"; "I have you exiting at 0014Z, how copy, over." | CACJCA (R7), check-in. HID8HI (R7), check-out, Daniel's ear reading. |
| R9 | Two-way traffic acknowledges by name: "Main sail, main sail, I have you…". | B42340, M20260923T231735Z, clip `20260923T231735Z_iceland_050s`, served `eam-61148f5c` (regraded `traffic`). |
| R10 | A radio check is a spoken test count: "1, 2, 3, 3, 2, 1. This main cell with test count out." | M20260923T191701Z, clip `20260923T191701Z_iceland_015s`. |
| R11 | The same message is broadcast again later. | 32 rebroadcast intervals over 9 messages (section 6). |

**What the recordings do not show, and so the game does not claim:**

- **Any reply to an EAM.** None of the recordings above has a receiving station answering
  an EAM on the air. The acknowledge and report steps below are therefore OURS. (We have
  not swept the corpus for one; that is an open item, Q11.)
- **Anything that happens after a message is received.** Our recordings are one side of
  a broadcast. They say nothing about what a recipient does with a message, and the game
  does not invent it (section 1).
- **What a message means.** EAM content is ciphertext. The game never decodes, classifies
  or interprets a real message.

---

## 4. The loop

Five steps, in the order ruling 5 gives them. For each: what the player does, what it is
built from, and its label.

### 4.1 Hear

A real message reaches the device through the feed: only rows whose server grade is
`message` (valar-eam-feed `docs/message-types.md` §2b; `auth`, `traffic`, `radio-check`,
`8888`, `fragment` and `unconfirmed` never reach a device). The device shows a **static
banner** that says a message is waiting, with its time. Nothing moves until the player
touches it (rail 1, section 7).

- Built from: R1 to R6 (the broadcast), the station's grade filter.
- Label: REC for the traffic; OURS for the banner.
- The audio is the archive clip for that message, served from `/missileer/archive` and
  R2 (CC0). The device has no speaker, so the player listens on a receiver, a web SDR,
  or the archive player (see Q3).

### 4.2 Copy

The player copies the message **character by character**, as the reader reads it.

- **Input.** A character picker on the bezel: drag to a character, lift to enter it. The
  alphabet is the station's own: A to Z and 2 to 7, with 0, 1, 8 and 9 read as `?`
  (station structure rule 6, `docs/message-types.md`). A "not made out" entry is `?`,
  the same rule the ear transcripts use ("`?` for anything not made out. No guesses.").
- **Pace.** The reader gives one character about every **1.7 s** (median character slot,
  31 messages; section 6). The picker has to be usable at that rate. A copy is not timed
  against the reader: the player may enter from memory, a notepad, or the second reading.
- **Length.** Whatever the message is. Nothing cuts a copy to 30 (R5; the station's own
  rule, valar-eam-feed CLAUDE.md "EAM length is not fixed").
- **Scoring.** Character agreement with the reference copy (Q4), counted only over
  characters the reference is sure of. The player's copy is never shown the reference
  until it is submitted.
- Label: REC for the alphabet, pace and length; OURS for the picker and the scoring.

### 4.3 Authenticate

The player answers an **authentication challenge in the recorded shape**: three
characters, "with", one character, both ways (R7, R8).

- **The table is ours and invented**, and it is **EXERCISE only.** Real authentication
  exchanges are graded `auth` and are never pushed, never served to a device and never
  shown (`docs/message-types.md` §2), so the game has no real exchange to work from and
  must not pretend to. The challenge and the expected reply come from a table the game
  generates (for example from the message id), shown to the player on a printed card
  or on the device's Reference screen.
- **Two-way, as recorded:** the device challenges ("authenticate CAC"), the player
  replies with the one character; then the player challenges and the device replies.
- Label: REC for the shape; OURS for the table, the card and the order.

### 4.4 Acknowledge

The player acknowledges the copy with one **hold** on the screen.

- There is no on-air acknowledgement of an EAM in our recordings (section 3), so the
  step is ours. Its **wording** is modelled on the acknowledgements we did record in net
  traffic: "I have you … how copy, over" (R8, R9). The screen reads, for example,
  `I HAVE YOU · QHXLPO · 35 CHARACTERS`.
- The hold is the gesture the bench work already measured (section 11): debounced
  contact, auto-sleep off, a late start that never fails the attempt.
- Label: OURS, wording modelled on REC.

### 4.5 Report

The player's copy, authentication and acknowledgement go to the server as one
**report**. The squadron-scale receipt board of the old §5 ("COPY CONFIRMED · 6/10")
becomes the net's receipt board (section 8).

- **Timed report (proposed, Q1).** The report is due at a report time **T**, derived from
  the message id as today (hash to an offset of 5 to 15 minutes, a rare 2-minute snap;
  OURS, 2026-08-04). It counts when it lands in **[T, T+2 s]** (rulings 1 and 2), and the
  player's timing inside that window is scored in the measured bucket (section 5).
- **Countdown.** The last **33 s** before T are the STANDBY countdown (ruling 3; the
  measured standby phase, section 6), shown in the same face as the clock.
- Label: OURS, with the 33 s taken from REC.

### 4.6 Around the loop

- **Radio check (proposed).** A player's first report of a session can be a check-in to
  the net with a test count, as recorded (R10). No score; it is how a callsign appears on
  the net's board for the day.
- **Rebroadcast.** A rebroadcast of a message already worked is not a new message
  (R11); the device offers it once. A player who missed the first broadcast may copy the
  second.
- **Practice.** EXERCISE traffic, built from our own archive clips (CC0) and marked
  EXERCISE everywhere, amber only. Certification before a first live report stays
  (OURS, 2026-08-05), rebuilt on this loop.

---

## 5. Scoring

Values are set after the design pass (ruling 6). This section fixes what is scored and
what is not.

| Stat | What it measures | Kept from the old design? |
|---|---|---|
| **Copy accuracy** | Characters that agree with the reference, over characters the reference is sure of. | New |
| **Report timing** | Where the report landed in [T, T+2 s], in the measured bucket. | Changed: was the key turn's deviation from T |
| **Authentication** | Right or wrong, per exchange. | New |
| **Watch days** | Days with at least one report, additive, capped per day. Never a streak. | Kept (old §5, OURS) |

**Kept, because they are our own measurements or principles:**

- **The clock floor is 199 ms and the display bucket is 0.2 s.** Measured on the bench
  (valar-eam-feed `test/fixtures/ntp-corrections-2026-08.json`; old §4 amendment and §13
  A.3). A single report shows whole buckets, rounded up; a cycle average may show
  hundredths.
- **Ranking and headline statistics do different jobs.** Rank on a sensitive statistic
  (a mean); headline a robust one (a median), labelled as what it is.
- **Score the human, not the antenna.** Receptions are the antenna's; copies and reports
  are the player's. Ignoring a message costs nothing.

**Removed:** miss distance, CEP, the 240 m benchmark, SHACK, the deviation-to-metres
curve and its anchor ("300 ms late ≈ 300 m off"). They are targeting and warhead
framing (ruling 5, editorial line).

**Early is never valid.** The window is [T, T+2 s], on the device and the server both
(ruling 2). A report before T is a failed report, not an early one inside the window.
The server was fixed to match (valar-eam-feed, "Execution window is [T, T+2 s]" PR).

---

## 6. Timings

| Constant | Value | Where | What it drives | Status |
|---|---|---|---|---|
| Report window (code: execution window) | **2 s**, shape **[T, T+2 s]** | device `Config::window_us`; server `timing.executionWindowS` | When a report counts | **OURS**, our design choice (ruling 1). Shape: ruling 2 |
| STANDBY countdown (code: terminal countdown) | **33 s** | device `Config::terminal_us` (still 30 s in code until the code pass) | The countdown before T | **From REC**: the measured standby phase, 13 messages (ruling 3; clips in section 12) |
| T offset | normal 300–900 s (90 %), snap 90–150 s (10 %) | server `timing.tOffset` | How long after the message T falls | OURS (2026-08-04) |
| Report cutoff (code: ack cutoff) | T−60 s | server `ackCutoffS` | Last moment a report can be started | OURS (2026-08-05) |
| Late copy | 120 s | server `lateCopyNonScorableS` | A message arriving later than T−120 s is not offered | OURS (2026-08-05) |
| Paper-strip print | 1.2 s | device `Config::print_us` | The print animation after a report | OURS (cosmetic) |
| Hold / rejoin | 300 ms / 250 ms | device `KeyTurnParams` | Gesture debounce | Bench (section 11) |
| Bucket / clock floor | 0.2 s / 199 ms | server `scoring` | Report timing display | Bench |
| Retarget times | — | — | — | **Dropped** (ruling 4) |
| Clock drift figure | — | — | — | **Dropped** (ruling 4) |

The enable limit (60 s), dead-man timer (10 min), auto-decode (300 s), stand-down and
regen (24 h / 3 days) and storm damping belong to mechanics this pass removes (section
9). Their values go with them.

### What our recordings measure

Source: the station's own word timestamps (whisper large-v3, `--dtw`) in
`E:\eam\full\*.segments.json`, one per message archive clip. Only messages the station
graded `message` are used. Words become characters and anchors through the station's own
vocabulary (`pi-agent/nato_snap.py`, exact matches only); Whisper loop collapse (three
or more tokens on one timestamp) is never measured; where a message has two cuts of the
same audio, one is used. Reproduce with `python3 scripts/game_timings.py --root
/mnt/e/eam` in valar-eam-feed; these figures are its run over `messages.json` built
2026-09-28T18:20:54Z (`E:\eam\yard\scratch\gametimings\game_timings.json`). Any single
interval is good to a few tenths of a second; read the medians.

| Quantity | How it is measured | n | Median | Range | p10–p90 |
|---|---|---|---|---|---|
| **Character slot** | one spoken character to the next, adjacent words only | 31 messages (1,547 pairs) | **1.7 s** | 1.22–2.20 s (per-message medians) | 1.34–2.02 s |
| **Standby cycle** | one "standby" to the next in the opening | 24 cycles, 13 messages | **12.3 s** | 9.1–14.9 s | 11.1–13.9 s |
| **Standby phase** | first preamble character to MESSAGE FOLLOWS | 13 messages | **33.0 s** | 26.4–39.5 s | 27.5–39.0 s |
| **One reading** | MESSAGE FOLLOWS to I SAY AGAIN | 8 messages | **52.6 s** | 39.5–95.9 s | — |
| Second receiver | the same broadcast's clip start on the other receiver | 31 clip pairs, 17 messages | 0 s | 0–4 s | 0–2 s |
| Rebroadcast | one transmission of a message to its next, one receiver | 32 intervals, 9 messages | 30.2 min | 6.4–91 min | 10.2–57.2 min |

**The 33 s, cited to its clips** (ruling 3). The standby phase of each of the 13
messages, in seconds:

| Message | Preamble | Archive | Clip | Standby phase |
|---|---|---|---|---|
| M20260926T215637Z | QHRSNB | eam-487597f2 | `desk-M20260926T215637Z-kph-215607-188s-p2` | 26.4 |
| M20260923T214732Z | ACZAA5 | eam-c93611e3 | `desk-M20260923T214732Z-kph-214732-96s` | 27.5 |
| M20260924T170328Z | QHJY7J | eam-ed75798a | `desk-M20260924T170328Z-iceland-170325-208s-p2` | 28.6 |
| M20260923T173253Z | AC4AUL | eam-356f1a01 | `desk-M20260923T173253Z-iceland-173253-126s` | 31.9 |
| M20260923T203321Z | AC7VGQ | eam-edf34881 | `desk-M20260923T203321Z-kph-203251-132s-p2` | 32.0 |
| M20260923T203822Z | ACNA3M | eam-1c188907 | `desk-M20260923T203822Z-iceland-203819-104s` | 32.2 |
| **M20260923T193554Z** | **VQTMAK** | **eam-212a3f98** | **`desk-M20260923T193554Z-iceland-193524-243s-p2`** | **33.0** (median) |
| M20260925T143430Z | 56ZF26 | eam-66387c6e | `desk-M20260925T143430Z-kph-143400-225s-p2` | 34.6 |
| M20260924T093155Z | QHSI7J | eam-028fb7df | `desk-M20260924T093155Z-kph-093125-250s-p2` | 36.2 |
| M20260926T133512Z | VQR5NV | eam-2122b29c | `desk-M20260926T133512Z-kph-133442-213s-p2` | 36.2 |
| M20260926T130220Z | QH54V4 | eam-c1cce232 | `desk-M20260926T130220Z-kph-130150-299s-p2` | 37.4 |
| M20260924T211959Z | QHAWC5 | eam-103c0101 | `desk-M20260924T211959Z-kph-211956-173s-p2` | 39.0 |
| M20260924T153003Z | QHGCGK | eam-52a62382 | `desk-M20260924T153003Z-iceland-163317-179s-p2` | 39.5 |

**The 2 s is ours.** It is not a measurement of anything (ruling 1). It happens to sit at
the slow end of the character slot (p90 of the per-message medians is 2.02 s), but that
was noticed after the fact and is not its basis.

Notes on the last three measured rows. **One reading** has only 8 messages because
Whisper rarely hears both anchors in one cut; the 95.9 s is 56MIE4
(`M20260925T181819Z`), whose I SAY AGAIN is probably a later one, so read the median.
The **second receiver** is not a delay: both receivers hear the same transmission, and
the 0–4 s is the detector's trigger and the clip id's one-second resolution. The
**rebroadcast** interval includes transmissions we missed (the 60 and 91 min intervals
are two and three cycles).

---

## 7. Tone rails (kept)

These are ours, settled 2026-08-04 and in valar-eam-feed `docs/standing-rulings.md`
("Tone rails"). None came from Nuclear Companion.

1. **Real traffic never auto-animates.** A message arriving makes a static banner.
   Everything that moves is started by a person.
2. **Remote hold flag.** One served bit returns the whole fleet to plain monitoring.
3. **EXERCISE marking everywhere** on synthetic traffic, and **amber means EXERCISE
   only.**
4. **Real preemption.** A new real message suspends a report in progress; it never
   costs the player anything (the old §10 preemption rule, OURS).
5. **Presence only from affirmative acts.** The net's board shows what a player did
   (a report, a check-in), never what the device observed about them. This is the
   repo-wide non-goal on behavioural telemetry, applied to the social layer.

**The disclaimer is replaced.** The Nuclear-Companion-style line ("based on unclassified
public sources; procedure depicted is a guess; deliberately so") goes (ruling 5). Proposed
wording, ours, for the device's Reference screen and every `/missileer/*` page:

> *Missileer is built from our own recordings of HFGCS broadcasts and from public-domain
> sources. It is the receiving end: hear, copy, authenticate, acknowledge, report. It has
> no launch procedure, targeting or warheads.*

---

## 8. The net (social layer)

**Proposed (Q6):** the social unit is **the net**, as recorded: stations check in and
check out with an authentication (R7, R8). A player's callsign is on the net while they
are checked in. The net's receipt board lists reports in arrival order:
`QHXLPO · REPORTED · 6 STATIONS`.

- **Removed:** wings as teams, squadrons, flights and capsules, seats (commander and
  deputy), the capsule picker, "50 LCCs", "45 capsules × 2 = 90 seats", squadron
  nicknames, command-post ranks and countermand authority, Olympic Arena, Twentieth Air
  Force as Higher Authority. They are the launch force's structure, and most of them came
  from Wikipedia or Nuclear Companion.
- **Kept:** callsigns (length, charset, light profanity filter; OURS 2026-08-05), the
  stable 6×6 callsign glyph, attribution by callsign only (never a real name), the
  monthly cycle and an annual season (OURS; the word "proficiency" and the Olympic Arena
  framing go).
- **Two operators (proposed, Q7).** Two players who copy the same message
  independently and agree are a **confirmed copy**. This is the station's own
  read-two-ways rule (R10 of `standing-rulings.md`: "Both models must see the pattern"),
  applied to people. It replaces the two-person launch crew.

---

## 9. From the old design to the new: keep, change, remove

This is the plan for the code pass. Nothing here changes code now.

### 9.1 Device (valar-scopes `src/game/`)

| Item | Decision | Why |
|---|---|---|
| `MsgClass { Nam, Fdm, Execution }` (`Derive.h`) | **Remove** | NAM/FDM/EAM taxonomy is Nuclear Companion (ruling 5); FDM is targeting. The device offers what the server graded `message`; it never invents a class. |
| `Derive()` class draw, `ClassWeight`, `DeriveParams.weights` | **Remove** | Same. |
| `Derive()` tier and offset, T on a Zulu minute; SHA-256; `ParseIsoUtcMs` | **Keep** (if Q1 keeps a timed report) | Our own T derivation (2026-08-04), graded against the server's fixture. Becomes the report time. Fixture regenerated without the class. |
| `DrillMachine` (the "six-step REACT drill") | **Change**: rename and re-phase | The six-step loop is Nuclear Companion. The pure, host-tested state machine and its rails stay. |
| `Phase::Idle`, `Offered` | **Keep** | Rail 1: arrival makes a static offer. |
| `Phase::Printing` | **Change** | The paper strip prints the player's own copy after a report, not the decode. |
| `Phase::Decoded` (class reveal) | **Remove** | No classes. |
| `Phase::Authenticate` (padlocked SAS safe) | **Change** | Becomes the R10-shaped exchange (4.3). The SAS safe and two-lock visual go (console hardware, from Wikipedia). |
| `Phase::WarPlan` | **Remove** | Targeting. |
| `Phase::Enable` | **Remove** | The 3+3 enable is Nuclear Companion. |
| New phases `Copy`, `Acknowledge` | **Add** | Steps 4.2 and 4.4. |
| `Phase::Armed`, `Window` | **Change** | Countdown to T and the report window [T, T+2 s]. Already [T, T+window] in `ResolveTurn`. |
| `Phase::Committed` (vote registered, parks for the vote) | **Remove** | No vote. Replaced by `Reported`, waiting for the server's receipt. |
| `Phase::Terminal` (30 s launch countdown) | **Change** | 33 s (ruling 3), and it becomes the STANDBY countdown before T rather than a countdown after it. |
| `Phase::Complete`, `Aborted` | **Keep** | |
| `Event::PlayerConfirmWarPlan`, `PlayerEnable` | **Remove** | Targeting; Nuclear Companion. |
| `Event::PlayerKeyArc`, `PlayerKeyTurn`, `PlayerKeyRelease` | **Change** | The launch key goes. The same gesture events become the report hold. |
| `Event::VoteResolved`, `VoteOutcome` (`Seconded`, `Launched`, `Inhibited`) | **Remove** | Vote/inhibit/dead-man is Nuclear Companion. `Failed` and `Aborted` survive as report outcomes, with the rule that a player who stops is never shown FAILED. |
| `Event::Hold`, `FeedReconnected`, `OtherMessageArrived` | **Keep** | Rails 2 and 4. |
| `Config::window_us` = 2 s | **Keep** | Ours (ruling 1). Comment loses "not final"; shape stays [T, T+2 s]. |
| `Config::terminal_us` = 30 s | **Change** to 33 s | Ruling 3. |
| `Config::print_us`, `key_confirm_us`, `bucket_us` (0 = unknown) | **Keep** | Ours; the bucket asymmetry rule stands. |
| `State.cls` | **Remove** | No classes. |
| `State.deviation_us`, `executed`, `withdraw_at_us`, `note` | **Keep** | Report timing and the report cutoff. |
| `State.until_impact_us` | **Remove** | Impact. |
| `KeyTurnGesture` (`KeyTurn.h`), `TouchCadence.h` | **Change** (framing only) | The bench-measured hold engineering stays; the "key" and "arc" framing goes. The bezel drag may carry the copy picker. |
| `DrawDrill` | **Change** | New screens: copy pad, authentication, acknowledgement, report countdown. War plan, enable, vote and terminal art go. |
| `DrillPolicy`: `ClockFresh`, `ClockAgeS`, `MonoForUtcMs`, `EndedDwellOver`, `UsbLongPressArmed`, touch rects, `ConfigPollIntervalS` | **Keep** | Ours, not launch-specific. |
| `DrillPolicy::DecideOffer` | **Change** | No class; offer only a `message`-graded row; `PastCutoff` stays. |
| `DrillPolicy::AutoDecoded` | **Remove** | It revealed a class. |
| `GameProtocol::ClassWire` | **Remove** | No classes on the wire. |
| `BuildCommitBody`, `BuildExecuteBody` (`enable_ok`) | **Change** | Commit without class; the report carries timing, copy and authentication, no `enable_ok`. |
| `DeviationMsForServer`, `ClassifyReply` | **Keep** | |
| `ResolveOutcome` (`seconded`, `inhibit_reason`) | **Change** | Report outcomes only. |
| `GameClient` (Claim, Commit, Execute, Status, Abort) | **Change** | Commit, Report, Status, Abort; no squadron vote polling. |
| `GameFormat`: `FormatDeviation`, `Sense`, `BucketDecimals` | **Keep** | The figure is graded against strings the server produced; "sortie" wording goes. With the window at [T, T+2 s], `Sense::Early` only appears on a failed report. |
| `src/eam/` monitor (clock, logbook, feed) | **Keep** | Not game. The clock face stays a clock; the name "TODC" goes. |

### 9.2 Server (valar-eam-feed `src/game/`, the pages, the migrations)

| Item | Decision | Why |
|---|---|---|
| `config.ts` `decode.weights` (NAM/FDM/execution), `stormDamping` | **Remove** | Class taxonomy. |
| `config.ts` `timing.tOffset`, `tCeilingS`, `ackCutoffS`, `lateCopyNonScorableS`, `maxClockSyncAgeS`, `executeSlackS` | **Keep** | Ours; they time the report. |
| `config.ts` `timing.executionWindowS` = 2 | **Keep** (rename to a report window in the code pass) | Ruling 1. Shape fixed now (Part B PR). |
| `config.ts` `enableLimitS`, `deadManS`, `autoDecodeS` | **Remove** | Enable, dead-man, class reveal. |
| `config.ts` `economy` (stand-down, regen, capsule sorties, RV count) | **Remove** | Missiles and sorties. |
| `config.ts` `scoring.bucketS`, `clockFloorMs` | **Keep** | Bench-measured. |
| `config.ts` `scoring.metresPerBucket`, `shackFloorM`, `maxMissM` | **Remove** | Miss distance. |
| `config.ts` hold flag, enrollment flag | **Keep** | Rail 2; enrollment stays closed until the code pass lands. |
| `derive.ts` | **Change** | Drop the class; keep T. |
| `votes.ts` `claim()` contract | **Keep** | Exactly-once resolution is ours and sound. |
| `votes.ts` commit | **Change** | Commits a report, not a vote; no class, no target. |
| `votes.ts` `executeVote` | **Change** | Becomes the report: window [T, T+2 s] (already fixed by the Part B PR), plus copy and authentication. |
| `votes.ts` second, inhibit, dead-man re-arm, `vote.opened` to the squadron | **Remove** | Vote/inhibit/dead-man. |
| `votes.ts` `V1_TARGET_CLASS`, `war_plan_target`, `target_class` enum | **Remove** | Targeting. |
| `votes.ts` preempt; outcomes `PENDING`, `FAILED`, `ABORTED`, `PREEMPTED` | **Keep** | Ours (preemption rule). `LAUNCHED`, `INHIBITED` go; `REPORTED` is added. |
| `sweeper.ts` | **Change** | FAILED at the deadline stays; the dead-man branch goes. |
| `scoring.ts` bucket and formatting (`formatSortieDeviation`, `formatAverageDeviation`, `bucketDecimals`) | **Keep** (rename "sortie") | Display precision is ours and measured. |
| `scoring.ts` `scoreDeviation` miss/SHACK, `meanMissM`, `shackCount` | **Remove** | Miss distance, SHACK, CEP. Copy accuracy is added. |
| `placement.ts` (wing, squadron, capsule, seat) | **Remove** (Q6) | Launch-force structure; replaced by the net. |
| `identity.ts` (callsigns) | **Keep** | |
| `season.ts` | **Keep** | Monthly cycle, annual season (ours). |
| `events.ts`, `sse.ts` | **Keep** (event kinds change) | The mechanism is ours. |
| Migrations 001–004 (vote, war plan, inventory, placement) | **Change** by new migrations | Never edit an applied migration; new ones drop or rename. |
| `/missileer/leaderboard` | **Change** | Drops CEP, miss and SHACK columns and "RANKED BY AVG MISS · CEP SHOWN IS MEDIAN". Ranks on report timing and copy accuracy. It already takes `Math.abs` of deviations and counts only resolved rows, so the window shape needs no change there. |
| `/missileer/log` | **Change** | Drops the launch credits ("LAUNCH … SECONDED … DEVIATION") and `target_class` rendering; lists reports. |
| `/missileer/sources` register | **Change** (section 10) | The three Nuclear Companion rows go. |
| Layout disclaimer ("procedure depicted is a guess — deliberately so") and `test/web-pages.test.ts`'s check for it | **Change** | Nuclear Companion's disclaimer style (ruling 5). New wording in section 7. |
| `docs/standing-rulings.md` "Device" rulings (terminal countdown parks for the vote; "§12's 2-second window is part of the fiction") | **Change** | The vote is gone; the window is our design choice. |
| `docs/game-derivation.md`, device fixtures (`emit-derivation-fixture.ts`, `emit-device-fixture.ts`) | **Change** | Regenerated without the class. |

---

## 10. Items for the code pass

In order. Each is its own PR off `main` unless two depend on each other.

1. **valar-eam-feed `/missileer/sources`: remove the three Nuclear Companion rows**
   (ruling 5), in `src/web/sources.ts` `REGISTER`:
   - "Six-step REACT launch procedure" (Nuclear Companion);
   - "Split-knowledge enable (3+3)" (Nuclear Companion);
   - "Vote / inhibit / dead-man timer triad" (Nuclear Companion; Wikipedia, REACT).

   The same register also has rows that are launch, targeting or warhead content, or
   cite Wikipedia or a commercial video: the wing/squadron structure (Wikipedia), the
   flight sequence and MIRV path (Wikipedia; NG 2007; minutemanmissile.com), the CEP,
   range and speed (Wikipedia), "T derivation (hash → launch time)", the message-class
   weights, the deviation-to-miss curve and the payload roster. Proposed: remove them
   too, and add rows for what the game now rests on (Q9).
2. **Disclaimer.** Replace the §1.5 line in `src/web/layout.ts` and the `POSTURE` block
   in `src/web/sources.ts`, and the regex in `test/web-pages.test.ts`.
3. **Server data model.** New migrations; `votes.ts`, `sweeper.ts`, `derive.ts`,
   `config.ts`, `scoring.ts` as in 9.2.
4. **Pages.** Leaderboard and log as in 9.2.
5. **Device.** `Derive`, `DrillMachine`, `DrawDrill`, `GameProtocol`, `GameClient` as in
   9.1, with the fixtures regenerated from the server.
6. **Docs.** `standing-rulings.md` device rulings; `game-derivation.md`; this document's
   section 9 marked done row by row.
7. **PROVENANCE.md** (missileer-watch) "The game" section: the two Nuclear Companion
   timings are gone (2 s is ours; 33 s is from our recordings).

---

## 11. Hardware facts the input rests on (kept)

Measured on the bench 2026-09-24, the Missileer board (Kit S3 1.28", CST816T, USB serial
`90706931E9D8`), firmware `feat/game-client` @ `325cb69` built with `-DKEYTOUCH_BENCH`.

- **The panel reports every ~13.7 ms (about 73 Hz)** over 5,357 intervals; each I2C read
  took 408–416 µs on average, 2.6 ms at worst. A report hold is timed at that resolution.
- **A 10 s hold held**: one touch of 10,228 ms with no release and no silences, reading
  only on the chip's report signal. AutoReset is 50 s and LongPressTime 60 s on this
  chip.
- **Release debounce: 250 ms, a bound, not a measurement** (ruled 2026-09-25). Runs A
  (30 s drag, no lifts) and B (ten deliberate lifts) on a `KEYTOUCH_BENCH` build replace
  it: the constant goes between A's longest dropout and B's shortest lift.
- **Gesture spec** (bench 2026-08-05, `docs/gametest-results-2026-08-05.md`): a rejoin
  window of at least 100 ms; the driver's `getTouch()`, not the `TouchNum` register;
  auto-sleep off; a late start never fails the attempt.
- **Screen:** 240×240 round (GC9A01). A copy picker has to fit it.

---

## 12. Evidence: the messages and clips

The measured messages. "Clip" is the archive clip (`pushed.json`, served at
`/missileer/archive`) whose word timestamps were measured; "Archive" is its archive id.
Slot is the median character slot, with the number of pairs. Standby cycle, phase and
reading are in seconds; — means not heard cleanly enough to measure.

| Message | Archive | Clip | Preamble | Slot s (pairs) | Standby cycle | Phase | Reading |
|---|---|---|---|---|---|---|---|
| `M20260922T010500Z` | eam-6b7a259f | `desk-M20260922T010500Z-kph-010449-183s-p2` | ACH53Z | 1.98 (33) | — | — | — |
| `M20260922T010904Z` | eam-d4e4a141 | `desk-M20260922T010904Z-kph-010901-53s` | ACBBAW | 1.54 (9) | — | — | — |
| `M20260922T150813Z` | eam-86d80b13 | `desk-M20260922T150813Z-kph-150743-230s-p2` | QHUDS2 | 2.10 (37) | — | — | — |
| `M20260922T160115Z` | eam-f27e2d58 | `desk-M20260922T160115Z-iceland-160045-429s-p2` | ACC6GX | 1.98 (88) | — | — | — |
| `M20260923T050256Z` | eam-31a26f7f | `desk-M20260923T050256Z-kph-050249-136s-p2` | ACRGIQ | 1.80 (56) | — | — | — |
| `M20260923T173253Z` | eam-356f1a01 | `desk-M20260923T173253Z-iceland-173253-126s` | AC4AUL | 1.54 (52) | 12.9, 11.2 | 31.9 | 50.8 |
| `M20260923T190247Z` | eam-36183461 | `desk-M20260923T190247Z-kph-190217-158s-p2` | ACRUZR | 1.22 (39) | — | — | — |
| `M20260923T193249Z` | eam-8e5774ca | `desk-M20260923T193249Z-kph-193244-139s-p2` | ACU6NQ | 1.54 (51) | — | — | — |
| `M20260923T193554Z` | eam-212a3f98 | `desk-M20260923T193554Z-iceland-193524-243s-p2` | VQTMAK | 1.64 (35) | 11.2 | 33.0 | — |
| `M20260923T193928Z` | eam-4fb93c48 | `desk-M20260923T193928Z-iceland-193924-38s-p2` | ACHAVK | 1.56 (10) | — | — | — |
| `M20260923T203321Z` | eam-edf34881 | `desk-M20260923T203321Z-kph-203251-132s-p2` | AC7VGQ | 1.56 (33) | 11.2, 11.3 | 32.0 | 52.9 |
| `M20260923T203822Z` | eam-1c188907 | `desk-M20260923T203822Z-iceland-203819-104s` | ACNA3M | 1.55 (32) | 11.2, 11.1 | 32.2 | 52.3 |
| `M20260923T214732Z` | eam-c93611e3 | `desk-M20260923T214732Z-kph-214732-96s` | ACZAA5 | 1.42 (37) | 11.7 | 27.5 | — |
| `M20260924T021943Z` | eam-50a93ef8 | `desk-M20260924T021943Z-kph-021943-186s-p2` | ACV3KE | 1.78 (41) | — | — | — |
| `M20260924T035208Z` | eam-484f63ba | `desk-M20260924T035208Z-kph-035138-228s-p2` | AC3QFD | 1.72 (35) | — | — | — |
| `M20260924T090156Z` | eam-c1286db5 | `desk-M20260924T090156Z-kph-090126-216s-p2` | QHCPBK | 1.70 (53) | — | — | — |
| `M20260924T091810Z` | eam-8e55b9b0 | `desk-M20260924T091810Z-iceland-091740-142s-p2` | QHMUSD | 1.82 (18) | — | — | — |
| `M20260924T093155Z` | eam-028fb7df | `desk-M20260924T093155Z-kph-093125-250s-p2` | QHSI7J | 1.68 (65) | 12.7, 12.3 | 36.2 | 57.2 |
| `M20260924T153003Z` | eam-52a62382 | `desk-M20260924T153003Z-iceland-163317-179s-p2` | QHGCGK | 1.62 (49) | 13.0, 13.9 | 39.5 | 62.8 |
| `M20260924T154010Z` | eam-e987fde1 | `desk-M20260924T154010Z-kph-153944-186s-p2` | 56YJ2I | 1.26 (22) | — | — | — |
| `M20260924T170328Z` | eam-ed75798a | `desk-M20260924T170328Z-iceland-170325-208s-p2` | QHJY7J | 1.74 (54) | 11.6, 12.5 | 28.6 | — |
| `M20260924T211959Z` | eam-103c0101 | `desk-M20260924T211959Z-kph-211956-173s-p2` | QHAWC5 | 1.82 (61) | 14.7, 14.9 | 39.0 | — |
| `M20260925T143430Z` | eam-66387c6e | `desk-M20260925T143430Z-kph-143400-225s-p2` | 56ZF26 | 1.80 (44) | 11.7, 12.4 | 34.6 | — |
| `M20260925T181819Z` | eam-2d82e0de | `desk-M20260925T181819Z-kph-181749-253s-p2` | 56MIE4 | 1.40 (89) | — | — | 95.9 |
| `M20260925T195946Z` | eam-2d82e0de | `desk-M20260925T195946Z-kph-195916-205s-p2` | 56MIE4 | 1.34 (57) | — | — | — |
| `M20260926T035338Z` | (not pushed) | `desk-M20260926T035338Z-kph-035308-371s-p2` | 563HBW | 1.73 (100) | — | — | — |
| `M20260926T130220Z` | eam-c1cce232 | `desk-M20260926T130220Z-kph-130150-299s-p2` | QH54V4 | 2.02 (64) | 13.7, 13.8 | 37.4 | — |
| `M20260926T133512Z` | eam-2122b29c | `desk-M20260926T133512Z-kph-133442-213s-p2` | VQR5NV | 1.92 (53) | 12.2, 13.4 | 36.2 | 44.9 |
| `M20260926T135341Z` | eam-f68719e2 | `desk-M20260926T135341Z-kph-135311-255s-p2` | 56VK2V | 2.09 (60) | — | — | — |
| `M20260926T215637Z` | eam-487597f2 | `desk-M20260926T215637Z-kph-215607-188s-p2` | QHRSNB | 1.28 (68) | 9.1, 9.2 | 26.4 | 39.5 |
| `M20260927T130152Z` | eam-d8c099b0 | `desk-M20260927T130152Z-kph-130122-221s-p2` | QHSNJF | 1.68 (67) | — | — | — |
| `M20260927T131754Z` | eam-c8731e05 | `desk-M20260927T131754Z-kph-131724-237s-p2` | QHIXMC | 2.20 (35) | — | — | — |

ACBBAW's cut has 9 pairs and is left out of the per-message slot median (the floor is 10).

**Second receiver** (the clip pairs are in the script's `--out` JSON): M20260922T150813Z,
M20260922T160115Z, M20260923T190247Z, M20260923T193249Z, M20260923T193928Z,
M20260923T203321Z, M20260923T214732Z, M20260924T021943Z, M20260924T090156Z,
M20260924T091810Z, M20260924T093155Z, M20260924T153003Z, M20260924T154010Z,
M20260924T170328Z, M20260924T170650Z, M20260924T184310Z, M20260924T211959Z.
**Rebroadcast**: M20260922T010500Z, M20260923T190247Z, M20260924T035208Z,
M20260924T153003Z, M20260924T154010Z, M20260924T170328Z, M20260924T170650Z,
M20260925T000043Z, M20260926T135341Z.

---

## 13. Open questions for Daniel

Each is a choice about scope, stated plainly, with what this pass proposes. Daniel can
overrule any of them, and the scope itself.

**Q1. Does a timed moment stay in the game?** Rulings 1 and 2 keep a 2 s window at
[T, T+2 s]. On the receiving end nothing on the air sets a T; the recordings show no
reply to an EAM at all. Options: (a) keep T as a **report time** derived from the
message id, as proposed in 4.5; (b) drop T, and the report is untimed (the 2 s window and
the 33 s countdown then have nothing to bind to); (c) tie the window to something on the
air, e.g. report within 2 s of the reader's "out" (needs live audio timing we don't
serve to devices). **Proposed: (a).**

**Q2. Where the 33 s countdown sits.** Ruling 3 sets it from the standby phase. Options:
(a) the last 33 s before T, labelled STANDBY (proposed); (b) the opening of a practice
broadcast, before the copy pad opens, mirroring the on-air standby phase; (c) both.
**Proposed: (a), with (b) in practice mode.**

**Q3. What the player copies from.** The device has no speaker and already holds the
station's decode. Options: (a) the player listens on their own receiver, a web SDR or the
archive player, and the device **hides the text** until the report is in; (b) copy is a
web-only activity on `/missileer/archive`, and the device only acknowledges and reports;
(c) the device shows the text and the player copies it over, which is not copying.
**Proposed: (a), with (b) as the practice surface.**

**Q4. The reference copy.** The station's copy is sometimes wrong. Options: (a) score
only characters the station is sure of, on `message` rows that are not partial, and send
disagreements to the ear queue instead of penalising the player; (b) score against the
station's copy as served; (c) score against a listener-corrected copy only, when one
exists. **Proposed: (a).** A player's copy could then correct the archive, which is a
separate ruling.

**Q5. The authentication table.** Real exchanges are never served (section 4.3).
Options: (a) an invented table, EXERCISE only, printed on a card or on the Reference
screen; (b) drop authentication from live messages and keep it only in practice; (c)
drop it. **Proposed: (a).** Ruling 5 names R10 in the loop, so (c) needs Daniel's
overrule.

**Q6. The social unit.** Options: (a) **the net**: check in, check out, a receipt board,
callsigns only (proposed, section 8); (b) keep wings as teams, sourced from a cleared
source (the NPS *Missile Plains* study covers the fields), without capsules, seats or
squadrons; (c) no social layer beyond the board. **Proposed: (a).**

**Q7. A second person.** Options: (a) two independent copies that agree make a confirmed
copy, the read-two-ways rule applied to people (proposed); (b) solo only. The two-person
launch crew is gone either way.

**Q8. Scoring values** (ruling 6: after this pass). Which statistic ranks (proposed:
mean copy accuracy, then mean report timing) and which headlines (proposed: median
report timing, labelled as a median), and the daily cap on watch days.

**Q9. The rest of the sources register.** Beyond the three Nuclear Companion rows
(section 10, item 1), the register has launch, targeting and warhead rows and Wikipedia
rows. **Proposed: remove them all** and list what the game now rests on: our recordings
(CC0), R10, and our own design.

**Q10. Words.** "Drill", "sortie", "execution", "commit", "key" are launch-side.
**Proposed:** "copy", "report", "window", "standby", "net". The edition keeps the
Missileer name.

**Q11. Replies to EAMs.** We have not swept the corpus for a receiving station answering
an EAM. **Proposed:** sweep it (the same way R10 was swept) before the acknowledge step's
wording is final.

**Q12. Enrollment.** `GAME_ENROLLMENT_OPEN` stays `false` until the code pass lands.
**Proposed: yes.**

---

## 14. Source register

| Source | Label | What the game takes from it |
|---|---|---|
| Our recordings and decodes (station, `E:\eam`, the archive at `/missileer/archive` and R2) | REC, CC0 1.0 | The broadcast's structure (R1 to R11), the timings (section 6), the authentication shape, the acknowledgement wording, the alphabet and lengths, and the audio for practice. |
| Daniel's ear transcripts (`test/fixtures/ear/`, valar-eam-feed) | REC | Observations 1 and 2 (QHXLPO, QHDJAS). |
| Station structure rules 6, 8, 9 and R10 (`docs/message-types.md`, `docs/standing-rulings.md`) | REC (our rulings on our recordings) | The alphabet, the address, message boundaries, the authentication exchange. |
| Bench measurements (`docs/gametest-results-2026-08-05.md`; this doc, section 11; `test/fixtures/ntp-corrections-2026-08.json` in valar-eam-feed) | Ours, measured | Clock floor, display bucket, touch report interval, the hold. |
| OPEN-SOURCES.md cleared list (NPS, HAER, NMUSAF, SAC, HD 2001, USGS) | CLR | Nothing yet. Candidates are named in section 2. |
| Nuclear Companion | Not a source (ruling 5) | Nothing. |
| Wikipedia, Northrop Grumman 2007 video, minutemanmissile.com, AiTelly, NUKEMAP, TWZ, CSIS, the GMD test record, the Lambert time-of-flight model | Not sources for the game | Nothing. They supported the launch-side design, which is out. |
