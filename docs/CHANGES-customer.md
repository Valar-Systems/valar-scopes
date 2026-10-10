<!--
What's new, for customers. The single source for:
  - the changes page (its address is the url: line below);
  - the 2-4 line summary a notify: yes image shows after an update (compiled in).
Spec: docs/v17-whats-new.md.

One section per firmware version, newest first:
  ## v<N>
  notify: yes|no     yes = updating to this version shows the tag and the config banner.
                     no  = nothing customer-noticeable: a changes-page entry only.
  summary:           1-4 lines, each at most 24 characters (refs not counted), ending at the
  - ... (refs: ...)  first blank line. ONLY changes customers will notice; quiet fixes go in the
                     page text only.
  (then the page text: plain language, no jargon, no internal names)

refs: #NNN (a PR merged on main) or spec:<doc> (a spec whose status names its merged PR).
"pending" is allowed only for versions other than the one being cut. The release gate refuses a cut
whose lines do not all map to merged work, so a slipped item's line is removed before the cut.

Who decides: Code proposes the entry, the notify flag and the lines; Daniel approves them in the
FW_VERSION bump PR, before the cut (the summary is compiled into the image). The page text may still
change at promote.
-->
url: https://scopes.valarsystems.com/blipscope/changes

## v17
notify: yes
summary:
- Hold to zoom in  (refs: #388)
- Overhead plane card  (refs: spec:v17-overhead-card)
- Clock follows DST  (refs: pending)

### Press and hold to zoom
Press and hold anywhere on the radar to jump in close. Hold again to go back to your usual range. A
ring fills around your finger while you hold; let go early and nothing changes. Swiping up and down
to zoom works exactly as before.

### See what's flying right over you
With "Look up!" turned on, a small card appears for a few seconds when a plane passes overhead:
who it is, what it is, where it's going and how high. Tap it for the full details, or swipe it
away. It never covers an emergency alert.

### The clock follows daylight saving time
The clock now changes for daylight saving time on its own. It uses your time zone, which Blipscope
picks up from your browser the next time you open its settings page.

### Smaller fixes
- A plane you've just claimed now appears in your Collection right away. Before, it could take a
  refresh or two.
- During setup, "Use my location" now ticks off the location step straight away.

## v16
notify: yes
summary:
- Swipe up/down to zoom  (refs: #374)
- Touch problem notice  (refs: #376)

### Zoom in on the radar
Swipe up to zoom in and down to zoom out. A small ZOOM tag shows while you're zoomed in, and the
radar goes back to your usual range by itself after 10 minutes.

### If the touchscreen stops responding
Blipscope now notices and tries to fix it by restarting, at most three times and only when nobody
is using it. If that doesn't work, it shows a "Touch unavailable" note at the top of the screen
with our support address and your device ID. The radar keeps working the whole time.
