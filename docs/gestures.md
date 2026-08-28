# Gesture cheat-sheet

Every touch the radar app answers to, in one place. Written from the code
([HandleTap](../src/AircraftManager.cpp) / `HandleSwipe`), not from memory.

Touch exists on **every** SKU — `s3_128` and `s3_21` (CST816), `s3_146`
(SPD2010), `s3_175_amoled` (FT5X06) — so nothing here is variant-gated.

## Detail card

| gesture | what it does |
|---|---|
| tap | photo page → data page; on the data page, or a card with no photo, **close** |
| swipe **up** | toggle the **pin**, then close |
| swipe **down** | **follow this flight for the session** — only when it has a route; otherwise close ([tap-to-peek.md](tap-to-peek.md)) |
| swipe left / right | close |

## Radar

| gesture | what it does |
|---|---|
| tap a contact | open its card. Repeated taps on the same spot cycle overlapping contacts |
| tap empty space | **clear the pin** |
| tap the alert ring | dismiss the current military/emergency episode (re-arms once every alerting contact has left) |
| swipe left / right | next / previous screen |

## List

| gesture | what it does |
|---|---|
| tap a row | open that aircraft's card |
| swipe up / down | scroll a page |
| swipe left / right | next / previous screen |

## Follow face

| gesture | what it does |
|---|---|
| swipe **down** | clear a **session** follow. A target set on the config page is never touched — a gesture must not delete a setting somebody typed |
| swipe left / right | next / previous screen |

## Reset menu

Discrete taps only. **Never a press-and-hold** — see below.

---

## How long does a pin last?

**A pin survives the contact dropping out and re-lights when it comes back.** It
is not cleared with the contact, and that is worth knowing because nothing in the
UI says so.

`pinnedIcao` holds a bare `icao24` string. The prune loop in `Update()` erases
the map entry once a contact has been absent past the grace window (2 poll
intervals, bounded 5–30 s) and **does not touch `pinnedIcao`**. The pin is only
ever read by comparing it against contacts currently in the table, so:

- **while the contact is gone** — nothing is highlighted, and the pin looks lost;
- **when the same `icao24` returns** — the comparison matches again and the
  highlight comes back.

That is the right behaviour for the case it was built for: OpenSky routinely
drops a state vector for a poll, and the local feed's box-edge clip makes fringe
contacts flap at 1 Hz. A pin cleared on every flap would be useless.

**It has no expiry, though.** The pin is cleared by exactly three things:

1. tapping empty radar,
2. swiping up again on that aircraft,
3. a reboot — it lives in RAM, never in NVS.

So an airframe pinned this afternoon will light up again if it flies over next
week. Probably harmless and arguably a nice surprise; recorded because it is
unbounded rather than because it is wrong, and because "the pin came back on its
own" is otherwise a mystery.

---

## Why there is no press-and-hold, anywhere

**Not a design preference — the hardware.** The reset menu was deliberately
converted *from* a 2 s hold *to* discrete taps because

> The CST816D may report **no change interrupt under a static contact**, so a
> held finger can register as nothing at all.

Two of the four SKUs use that part. A hold would work on the other two and fail
intermittently on those, which is worse than failing everywhere: it presents as a
flaky feature rather than an absent one.

**So do not propose a long-press for any control on this panel.** The constraint
does not change until the hardware does.
