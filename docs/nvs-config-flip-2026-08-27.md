# OPEN ANOMALY — three info-field keys flipped on COM4 across a flash

**Status: open, not reproduced, fixed forward. Trigger for treating it as real is
at the bottom.**

Logged because an NVS state change with **no identified writer** is a nothing
until it is a [#245](https://github.com/Valar-Systems/valar-scopes/issues/245).
It is being written down at the point it is cheap — while the timestamps are
exact and the board is still on the bench — rather than reconstructed later from
someone's memory of a Thursday.

## What happened

All times 2026-08-27, PDT, board on **COM4** (`Blipscope-31E9D8`,
`192.168.86.63`).

| time | event | evidence |
|---|---|---|
| **11:51** | Config pages fetched for all three bench boards over HTTP. COM4 reported `info-callsign`, `info-speed`, `info-baroalt` **ON**; COM119 and COM16 reported all three **OFF**. | `diffcfg.py` over `scratchpad/cfg/*.html`, re-fetched into those exact paths and mtimes confirmed same-minute (an earlier run of the same diff had silently read the previous day's cached files) |
| 11:51–12:31 | `pio run -e blipscope-s3-128 -t upload --upload-port COM4`. Hash verified, hard reset via RTS. Banner read back off the wire: `env=blipscope-s3-128 fw=v8`. | upload log + serial |
| **≈12:31** | Same board, same three keys, now **OFF**. `info-type` and `info-operator` still ON. | **raw markup**, not a parser: `<input type="checkbox" name="info-callsign">` vs `<input type="checkbox" name="info-type" checked>` |
| 12:32 | Corrected forward: partial POST (`X-Blipscope: 1`, **no** `cfg-form` marker) setting the three to `on`. The whole-form guard left every other setting alone. Read back and confirmed. | HTTP 200 + raw markup |
| 12:33 | 48 h stock-config capture started for #264. | `bench-logs/com4-stockcfg-264-2026-08-27-1233.log` |

The board now carries the factory-default field set — callsign, type, operator,
speed, baroalt on; reg, route, icao, country, vrate, geoalt, heading, squawk,
category off — **verified against the `defaultOn` column of the table in
`src/AircraftInfoFields.cpp` rather than assumed.** `defaultOn` is the FIRST
bool in `AircraftInfoFieldDef`, which was re-read rather than inferred from the
row shape.

## What is eliminated

- **The migration.** `configmigration::Apply()` touches `info-type`,
  `info-operator`, `logbook` and `local-details` and nothing else, and it
  `remove()`s rather than writing — an absent key renders at `defaultOn`, i.e.
  **checked**, which is the opposite of what was observed.
- **A partition-table difference.** `follow-bench-s3-128` is `extends =
  env:blipscope-s3-128`; both resolve to `partitions-s3-16mb-bignvs.csv`. Same
  NVS region before and after.
- **Any other writer.** The `config` namespace is opened read-write in exactly
  three places: namespace creation in `ConfigurationWebServer::Initialise` (no
  field writes), `/save`, and `/enroll-key` (writes `cloud-key-fac`). Only
  `/save` can write an `info-*` key.
- **A disabled control silently truncating the form.** That is a real mechanism
  — a disabled input is dropped from `FormData` and turns a whole-form POST into
  a partial one — and it is gated by rule 4 of `scripts/check-config-form.py`,
  which passes on all eight pages.

## What survives, ranked

1. **A stale browser tab was saved.** `/save` is the only writer, and a
   whole-form POST writes an explicit `"false"` for every unticked box. A config
   page loaded at one moment and submitted later posts **the DOM as it was
   rendered**, not as NVS is now. Daniel ran the local-face eyeball session in
   this window and reported clearing the follow field *and saving*, so at least
   one whole-form POST certainly happened. This requires the tab to have been
   loaded while the three read off — which is unproven and is the weak link.
2. **The 11:51 reading was wrong.** It came from the regex parser, not from raw
   markup; only the post-flash read was eyeballed directly. Against this: the
   same parser, in the same run, read COM119 and COM16 as *off*, and their raw
   markup today confirms *off* — so it demonstrably distinguishes checked from
   unchecked. That is a control in its favour, not proof for the COM4 row.

Both hypotheses have the flash as a **coincidence of timing, not a cause**, and
neither is confirmed.

## Why it matters even though nothing is broken

Because the two surviving hypotheses have very different consequences:

- if (1), this is the documented cost of a whole-form page and the lesson is
  operational — **do not save a config page you did not just load**;
- if neither, an NVS key changed value with no code path that could have changed
  it, on a board whose sibling has an open unexplained fragmentation issue.

## The trigger

**If the three keys flip again across a flash with no `/save` in between and no
config tab open, it is real** — open an issue and treat it as an NVS integrity
problem rather than an operator one. Checking costs one command before and one
after:

```sh
curl -s http://<board>/ | grep -o '<input[^>]*name="info-callsign"[^>]*>'
```

Raw markup, not a parser: the parser is what the weaker hypothesis above turns
on, and this check exists to be trusted.

## Related

- The whole-form POST hazard and why an unticked box becomes an explicit
  `"false"`: [CLAUDE.md](../CLAUDE.md), *"a default only reaches keys that were
  never saved"*.
- The 2026-08-02 scripted POST that cleared five render toggles and produced a
  wrong frame-budget conclusion for a day — same mechanism, different trigger,
  and the reason `cfg-form` exists.
- [#264](https://github.com/Valar-Systems/valar-scopes/pull/264) — the soak this
  board is now running is the reason its config had to be known exactly.
