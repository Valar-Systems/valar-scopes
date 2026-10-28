# Removing ntfy, completely: a plan with its predictions (no code)

**The ruling** (reviewer chat, pasted by Daniel, 2026-10-28).
- **Daniel's words:** "I don't want phone alerts any more. I thought got rid of ntfy. If not, i want it completely
  gone".
- **Scope:**
  - every edition (radar, Missileer, Seismic, Birding, Fishing, Claudescope, Speed, Space);
  - the config pages, `NtfyAlerter.h`, `NtfyTopic.h` and every `QueueNtfyPost` caller;
  - CI's ship-alert job.
- **What stays:** on-screen alerts. Only the phone push goes.
- **Stored settings:** the topic and every ntfy-only toggle are erased once, on the first boot of the release that
  removes them. Nothing else in NVS is touched.
- **When:** a plan now, and no code. The code comes after #445 merges and before v19 is cut, so v19 and each edition's
  next release ship without it. If it isn't ready when v19 otherwise is, v19 doesn't wait.

## 1. What goes, as read from main at `ee38026`

**Shared:**
- **`include/NtfyAlerter.h`:** the editions' sender, which POSTs to `https://ntfy.sh/<topic>`.
- **`include/NtfyTopic.h`:** the generated private topic. `main.cpp` calls `ntfytopic::Ensure()` at boot, so **every
  device holds a `ntfy-topic` in NVS** whether or not its owner ever used it.
- **The config pages:** the `ntfy-topic` field and its hints in every edition's branch of `ConfigurationWebServer.cpp`,
  each branch's `TrySaveParam("ntfy-topic")`, the radar's `ntfy-regen` checkbox, and the save-time regeneration.

**The radar (`AircraftManager`):**
- `ProcessAlerts`, `QueueNtfyPost`, and `SendFlyoverNotification`, `SendOverheadNotification`,
  `SendEmergencyNotification` and `SendFollowAlert`.
- The `ntfyTopic` member, the throttle, and the per-contact "notified" flags in `TrackedAircraft` that nothing else
  reads.
- `ProcessAlerts` returns at once when the topic is empty and does nothing but queue posts, so removing it changes
  nothing on the screen.

**Each edition:** its `SendNtfy` (each only queues to `NtfyAlerter`; none chimes) and every call to it. The table
below says, for each alert toggle, what it gates **besides** the post.

| edition | setting | what it gates besides ntfy | verdict |
|---|---|---|---|
| radar | `emg-alert` | nothing (`emg-visual` is the on-screen ring) | **erase** |
| radar | `follow-up`, `follow-down`, `follow-lost` | nothing: they gate only `SendFollowAlert` | **erase** |
| radar | `mil-alert` ("Alert on military (ntfy)") | **the MQTT "military" event** (`PublishMqttEvents`) and **the daily highlight's "Military" reason** | **mixed: section 2** |
| radar | `lookup-alert` ("also ntfy") | **the overhead tone on speaker boards, and the Stats overhead pick, when `lookup` is off** | **mixed: section 2** |
| radar | `watchlist` | the on-screen watch flag | **stays**; only its flyover post goes |
| Missileer | `eam-alert-new`, `-tempo`, `-abncp`, `-space` | nothing: each block only calls `SendNtfy` | **erase** |
| Space | `sp-alert-launch`, `-aurora`, `-flare`, `-iss`, `-dsn`, `-neo` | nothing. The chime fires before each check, gated by `sp-chime` alone | **erase**; `sp-chime` stays |
| Seismic | `se-alert-big`, `-near`, `-tsnmi` | nothing | **erase** |
| Birding | `bd-alert-rare`, `bd-alert-target` | nothing | **erase** |
| Fishing | `fi-a-solunar`, `-flow`, `-temp`, `-tide`, `-baro` | **the on-device chime**, inside the same block (with `fi-chime`) | **stay**: they still gate the chime. Only the post goes |
| Fishing | `fi-chime`, `fi-flow-cfs`, `fi-temp-lo`, `fi-temp-hi` | the chime and its triggers | **stay** |
| Claudescope | `cl-alert-sess`, `cl-alert-week` | nothing | **erase** |
| Claudescope | `cl-session-pct`, `cl-week-pct` | nothing: thresholds read only by the alert | **ntfy-only, but not toggles: section 2** |
| Speed | `sc-a-speeder`, `sc-a-record`, `sc-a-offline` | nothing | **erase** |
| Speed | `sc-alert-speed` | nothing: a threshold read only by the speeder alert | **ntfy-only, but not a toggle: section 2** |
| Speed | `sc-limit` | the on-screen over/under | **stays** |

**CI:** the ship-alert job, which posts to ntfy when a shipping leg goes red on main. Afterwards, the session that merges
checks main's run after every merge and reports any red shipping leg. The `NTFY_TOPIC` repository secret is then unused;
deleting it is Daniel's call.

**`scripts/check_follow_privacy.py` tightens.** Today it allows the follow target exactly one way off the device, the
ntfy body ("must not leave the device, except in an ntfy body"). Afterwards the target leaves the device nowhere: the
allowance is deleted, and the selftest's planted leak through the old sink must turn red.

**Docs, when the release ships:**
- `README.md`: the phone-alert bullets for each edition, and the radar's watchlist and alerts line;
- `CLAUDE.md`: each edition's "ntfy on N toggles" line;
- `ROADMAP.md`: marked **removed, 2026-10-28**;
- the proxy's support page (`proxy/pages/support.html`, regenerated with `embed-pages.mjs`).

Release notes and bench logs stay as history.

**Not touched:** MQTT, the visual rings, tones and chimes, every on-screen alert, and every other setting.

## 2. Four decisions for the reviewer, each with a recommendation

1. **`mil-alert`, recommended (a): keep the key, and relabel what it still does.**
   - (a): the key stays and is not erased. Its label becomes "Military: MQTT event and today's highlight". MQTT owners
     see no change.
   - (b): erase it, and move both uses to `mil-show`. Then an owner with `mil-show` on and `mil-alert` off starts
     getting military MQTT events they never chose.
2. **`lookup-alert`, recommended: erase it, so the tone and the Stats pick follow `lookup` alone.**
   - That changes behaviour only on a device with `lookup-alert` on and `lookup` off. Such a device stops getting the
     overhead tone until its owner ticks "Look up!".
   - The alternative is to keep the key as a "look-up tone" setting.
3. **The ntfy-only thresholds (`cl-session-pct`, `cl-week-pct`, `sc-alert-speed`), recommended: leave them.** The
   ruling erases toggles and touches nothing else, so as ruled they stay, unread. Erasing them too is the alternative,
   if the ruling extends to every ntfy-only setting.
4. **The order inside the release:** the erase ships in the same image that stops reading the keys, so no image ever
   reads an erased key and treats it as never set.

## 3. The erase

**The mechanism:** `include/ConfigMigration.h`, rev 6 → **7**, "ntfy removed".
- `configmigration::Apply()` runs unconditionally in `setup()` in every edition, before the app reads its settings. It
  stamps `cfg-rev` when it is done, so the erase runs **once**, on the first boot of the release that carries it.
- It deletes exactly the listed keys from the `config` namespace. A key an edition never had is a no-op.

**The list, as ruled (section 6): 29 keys.**
- **The topic:** `ntfy-topic`.
- **Radar:** `emg-alert`, `lookup-alert`, `follow-up`, `follow-down`, `follow-lost`.
- **Missileer:** `eam-alert-new`, `eam-alert-tempo`, `eam-alert-abncp`, `eam-alert-space`.
- **Space:** `sp-alert-launch`, `sp-alert-aurora`, `sp-alert-flare`, `sp-alert-iss`, `sp-alert-dsn`, `sp-alert-neo`.
- **Seismic:** `se-alert-big`, `se-alert-near`, `se-alert-tsnmi`.
- **Birding:** `bd-alert-rare`, `bd-alert-target`.
- **Claudescope:** `cl-alert-sess`, `cl-alert-week`, `cl-session-pct`, `cl-week-pct`.
- **Speed:** `sc-a-speeder`, `sc-a-record`, `sc-a-offline`, `sc-alert-speed`.

**Not on the list:** `mil-alert` (kept and relabelled, 2.1 (a)), every `fi-*` key, `sp-chime`, `sc-limit`,
`watchlist`, `lookup`, `mil-show`, the visuals, `tones`, and everything else.

## 4. Predictions, to freeze when the code PR opens

| | check | predicted |
|---|---|---|
| **N1** | **every ELF of every edition** (all envs in `platformio.ini`), scanned for `ntfy.sh`, for the sender names (`NtfyAlerter`, `QueueNtfyPost`, `SendNtfy`, `SendFollowAlert`), and for every string containing `ntfy` | `ntfy.sh` and the sender names: **0 in every ELF**. The only `ntfy` strings left are the erase list's key name(s), and nothing else. **The control:** the same scan of main's ELFs finds `ntfy.sh` and the senders in every edition that has them, so the scan can see what it is looking for, and its selftest finds a string planted in a synthetic file |
| **N2** | **every other config field**: each edition's served form, before and after, compared as a set of (name, value, default) | identical **except exactly the removed fields**, with no other field renamed, revalued or re-defaulted. Section 2's relabel (if 2.1 is (a)) changes a label, not a name, value or default |
| **N3** | **a whole-form save round trip**: GET the page, POST every field back unchanged, then compare an NVS dump before and after | **every stored setting byte-identical** except the erase list, which is already absent after the migration |
| **N4** | **the erase, on the host** (`test_config_migration.cpp`): a fake NVS holding every listed key and its neighbours (`fi-chime`, `sp-chime`, `mil-alert`, `watchlist`, `sc-limit`, the visuals). As ruled, `cl-session-pct` is on the erase list, not a neighbour | after `Apply()`, **exactly the listed keys are gone** and every other key is byte-identical; a second `Apply()` is a no-op. **Sabotage:** the erase also deletes one neighbour (`fi-chime`), and the test must turn red |
| **N5** | **the privacy check** | the allowance is gone, and the target reaches **no** outbound sink. The selftest's plant through the old sink turns it red |
| **N6** | **CI** | green, with the ship-alert job gone |

**What N2 and N3 can and cannot see.**
- **On the glass:** the radar (E2:08) and the Missileer (the Missileer bench board).
- **Not on the glass:** the six other editions ship only as `-s3-146` envs, and no bench board runs that SKU. For them,
  N2 compares the page template's field set from the source (each edition's branch), and N3 rests on N4's host test.
  That is the weaker form, and it is named here so it is not mistaken for the strong one.

**Outside the set:** stop and report.

## 5. When, and in what order

1. **Now:** this plan, for a ruling. Its predictions freeze when the code PR opens.
2. **After #445 merges:** one code PR off main, covering:
   - the senders and settings;
   - the rev-7 erase;
   - the privacy check;
   - the CI job;
   - each edition's page.
3. **Before v19 is cut,** so v19 and each edition's next release ship without ntfy. If it isn't ready when v19
   otherwise is, **v19 doesn't wait**.
4. **When the release ships:** the docs in section 1.

## 6. Rulings, 2026-10-28 (reviewer chat, pasted by Daniel), recorded with their source

**The plan (at `4f27511`) is accepted, with these answers to section 2:**
- **2.1, `mil-alert`: (a).** Keep the key, unerased, relabelled **"Military: MQTT event and today's highlight"**.
- **2.2, `lookup-alert`: erase it,** as recommended. The overhead tone and the Stats pick follow "Look up!" alone.
- **2.3, the three ntfy-only thresholds (`cl-session-pct`, `cl-week-pct`, `sc-alert-speed`): erase them too.**
  "Completely gone" covers every ntfy-only setting, toggle or not, so **the list is 29 keys**. In N4,
  `cl-session-pct` moves from the neighbours to the erase list, and `sc-limit` stays a neighbour.
- **2.4, the erase ships in the image that stops reading the keys:** accepted.

**The predictions:**
- **N1-N6 as planned,** frozen and pushed before the first code commit.
- The weaker N2/N3 form for the six `-s3-146`-only editions is accepted, as named.

**The timing stands:** the code comes after #445 merges and before v19 is cut, and v19 doesn't wait. **In that PR's
merge report, ask Daniel whether to delete the unused `NTFY_TOPIC` secret.**

The list in section 3 now reads as ruled. Counted: 1 topic, 5 radar, 4 Missileer, 6 Space, 3 Seismic, 2 Birding,
4 Claudescope, 4 Speed = **29**.
