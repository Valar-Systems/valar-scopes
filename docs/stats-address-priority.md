# The Stats screen evicts the address a stuck customer needs

A customer in the field could not reach his device's config page. The board was
showing **NEEDS VERIFY**, and the only remedy is the Verify button *on that
page*. The product had built him a loop.

## Why it fails on the devices it fails on

**The IP disappears in proportion to how much history a device has, so it fails
first on long-serving units — the ones that survive long enough to meet a
credential rotation, go NEEDS VERIFY, and then cannot say where to reach them.**

That sentence is the whole finding. A fresh board shows its IP; a board that has
been collecting a lifelist for months does not. The failure is invisible on the
bench for exactly as long as the bench board is new, and it is guaranteed on the
devices most likely to need it. Anyone reading this later as a cosmetic layout
tweak should read that sentence again first.

## Step 1 — the numbers, measured before anything was changed

`fontHeight()` is 8: no custom font is ever set anywhere in the tree, and
`setTextSize(1)` is the only size call in `DrawStats`. So on the s3-128:

```
lh          = 8 + 10          = 18
clockRow    = 240 - 30        = 210
wifiRowTop  = 210 - 18 - 2    = 190
hostRowTop  = 190 - 18        = 172
clockTop    = hostRowTop      = 172
y starts at                     48
```

The IP row's guard is `y + lh <= clockTop`, so it passes **iff `y <= 154`** —
a budget of **106 px from y=48, about five rows.**

### The guard is NOT dead code, and the stronger claim was wrong

It was proposed that the guard might never pass, which would have meant no
shipped unit had ever displayed its IP. **That is false, and it was worth
checking rather than building on.** The behaviour is state-dependent:

| state | y at the address block | IP drawn? |
|---|---|---|
| empty sky, logbook off | 72 | yes |
| 4 aircraft rows only | 126 | yes |
| LIFELIST alone (4 rows + gap) | 150 | yes, *just* |
| LIFELIST + FEED block | 174 | **no** |
| aircraft + TODAY | 168 | **no** |
| aircraft + LIFELIST | >=180 | **no** |

`line()` refuses to advance `y` when a row will not fit, so rows cannot
individually overrun. The inter-block gaps (`y += 6`) are **unguarded**, so `y`
can be pushed past the ceiling by whitespace alone — see the separate defect
below.

## PRE-REGISTERED, 2026-09-15, BEFORE THE HARDWARE RUN

Written before the board was provisioned, because this run can produce a false
clearance and the moment to decide what counts is now.

**A freshly provisioned board has an empty lifelist and, per the table above,
WILL draw the IP.** So:

| observation | what it means |
|---|---|
| first `[stats-geom]` lines show `ipDrawn=1` | **NOT a clearance.** It is the empty-device row of the table reproducing exactly as predicted. Reporting it as "the bug did not occur" would be reading the instrument backwards |
| `ipDrawn` observed going **1 -> 0** as LIFELIST/TODAY populate | the finding confirmed on hardware — this is what the run exists to capture |
| `ipDrawn=1` throughout a run that never reached the loaded state | **the run tested nothing.** Log longer, or seed the logbook |
| `ipDrawn=0` from the very first frame | outside the predicted set — stop and re-read the geometry before concluding anything |

**A run that never reaches the loaded state has not tested the bug.**

## The real defect: drop order is emergent, not stated

Swapping two rows would fix this customer and leave the mechanism intact.

**What gets dropped when the screen fills is currently decided by source order.**
Whatever is written before the budget runs out survives; everything after it is
silently deleted. No file states the priority, so no one has ever decided it —
and the next person adding a row wins a place by being early rather than by
being important. That is how the Reset-WiFi control once vanished, how the host
name vanished after it, and how the IP vanishes now: three instances of one
mechanism, each fixed locally.

So the fix is an explicit, named priority consumed by the renderer — the
`ChordWidthPx` pattern, the rule living once with every caller going through it.
Then "what gets dropped" is a decision someone made and recorded, and adding a
row forces an answer instead of an accident.

## Separate defect in the same block: whitespace is spent on content never drawn

`line()` is guarded and will not advance `y` past the ceiling. The block gaps
are bare statements:

```cpp
y += 6;              // unguarded
line("LIFELIST");    // may draw nothing at all
```

So a block whose heading does not fit still consumes its 6 px gap, and the gap
is charged against the budget for rows that were never rendered. It is latent
anywhere this pattern is used, not only here.

## Version: v12, and the first answer was wrong in a checkable way

The initial reading was "this belongs in v11, because cfg-rev 5 defaulting the
logbook ON is what spends the budget." Both halves are false, and both were
settled by reading artifacts rather than arguing:

```
version.txt = 11      (live)      tag v11 exists      FW_VERSION = 11 on main
```

**v11 is already published.** "Do not ship the eviction with v11" was never
available — it shipped. And the budget-spender is not rev 5:

```
3  spotting logbook default ON (v8)     <- LIFELIST, the 4 rows + gap
5  tz-offset "0" -> auto (v11)          <- what v11 actually added
```

So the LIFELIST block has been default-ON since **v8**, and this defect has been
in the field across v8, v9, v10 and v11. That is not a reason to hold a release;
it is a reason to cut v12 promptly. It is a fix to a long-standing defect, not a
regression anyone is about to introduce.

Queues behind `fix/single-provenance-caption` (confirmed unmerged). B3 runs on
whatever the final merged image is, not on an image taken before these land.

**Consequence of v11 shipping without the caption fix:** the data card still
draws the representative-photo caption, so the existing product photography
remains accurate. The reshoot moves to whenever v12 publishes.

## The class: an expected consequence that existed nowhere in the product

The field unit's NEEDS VERIFY was **the expected v10 -> v11 key migration**. Not a
rotation, not a KV fault, not a bug in the latch. It was going to happen, to
those units, and somebody knew.

**The expectation existed nowhere a customer or a future maintainer could meet
it.** Not on the device, not on the config page, not in the release notes, not in
a support doc, not in a comment beside the code that would produce it. So the
product did the thing it was designed to do, and every observer -- owner,
maintainer, and two days of investigation -- read it as a fault.

That is the class, and it is worse than an undocumented bug:

| | a bug | this |
|---|---|---|
| someone knows | no | **yes** |
| it surprises the owner | yes | yes |
| it surprises the maintainer | yes | **yes, including the person who knew** |
| there is something to fix | the code | **nothing -- the code is correct** |

There is no failing test to write. The remedy is that a known consequence gets
encoded where it will be encountered: a line the device says, a note on the page
the customer lands on, a comment beside the code that causes it. Prose in
somebody's head is not an encoding, and neither is a plan.

Same shape as the enrolment ledger that recorded a runaway loop faithfully for
twenty days while nothing read it. The signal was perfect; nothing surfaced it.
Here there was not even a signal -- only an expectation.

## Does v12 do it again? Not by itself, from the firmware side

Checked in the firmware, which is the half I can settle without KV archaeology:

- `cloud-key-fac` is a **stored 64-hex value, sent verbatim** as `X-Blip-Key`.
- `FW_VERSION` rides as a **separate header**, `X-Blip-FW`.
- `FW_VERSION` appears in **no key derivation anywhere** in the tree -- only in
  the OTA gate, the config page's version label, and that header.
- The v10..v11 diff of the key path is **tz-offset work and one added header**
  (`X-Blip-Boot`). Nothing touches the key.
- The partition table is **identical** in v10 and v11, so the "relocated NVS
  makes the key unreachable" hazard is not it either.

**So bumping the version alone cannot invalidate a key.** If v10 -> v11 did, the
mechanism is server-side, and that side has to answer whether v12 repeats it.

**The sequencing call, regardless of that answer: the delivery below ships WITH
v12, not after it.** The cost is asymmetric. If v12 does not repeat the
invalidation, shipping the delivery early costs nothing -- it fixes "the error
and its remedy live on different screens" for every other cause too. If v12 does
repeat it and the delivery shipped afterwards, the fleet takes the outage first
and gets the instructions second, which is precisely the order that produced
this incident.

## v12 item: a migration log line shaped to be misread

This one earns its place because **it produced a wrong conclusion today, in
someone reading carefully with the source open.** The line is:

```
[cfg-migrate] rev 0 -> 5: cleared logbook=0 (was off); the spotting logbook now defaults ON
```

Every word is true. A factory reset drops `cfg-rev` to 0, every migration
replays, and the rev-**3** logbook step logs underneath a banner naming the
**span** `0 -> 5`. The natural reading — the only available reading, really — is
that rev 5 did it. That misattribution went on to become a version argument for
shipping a fix into an already-published release.

**A message that is technically accurate but shaped to be misread is a defect**,
the same family as the truncated enum that became a plausible-looking category.
Both hand back something that has the shape of a fact and is not one.

The fix: each step logs the rev that performed it, so a replay reads as a
sequence of attributed actions rather than one banner with a list of
consequences beneath it.

```
[cfg-migrate] rev 3: logbook default ON -- cleared stored logbook=0
[cfg-migrate] rev 5: tz-offset '0' -> auto (derived -28800 s)
[cfg-migrate] rev 0 -> 5 complete (2 steps applied)
```

The span line survives, but it can no longer be the only thing carrying
attribution.
