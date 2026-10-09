<!--
What's new, for customers. The single source for:
  - the changes page, served by the Worker at /blipscope/changes;
  - the 2-4 line summary each image shows after an update (compiled in).
Spec: docs/v17-whats-new.md.

One section per firmware version, newest first:
  ## v<N>
  notify: yes|no          yes = devices updating to this version show "What's new"
  summary:                1-4 lines, each at most 24 characters, ending at the first blank line
  - ...
  (then the page text: plain language, no jargon, no internal names)

Code drafts each entry in the FW-bump PR. The summary is compiled into the image at the cut, so
Daniel approves it BEFORE the cut; the page text can still change at promote.
-->

## v17
notify: yes
summary:
- Double-tap to zoom in
- Overhead plane card
- Clock follows DST
- Claims show at once

### Double-tap to zoom
Double-tap an empty spot on the radar to jump in close. Double-tap again to go back to your usual
range.

### See what's flying right over you
With "Look up!" turned on, a small card appears for a few seconds when a plane passes overhead:
who it is, what it is, where it's going and how high. Tap it for the full details, or swipe it
away. It never covers an emergency alert.

### The clock follows daylight saving time
The clock now changes for daylight saving time on its own. It uses your time zone, which Blipscope
picks up from your browser the next time you open its settings page.

### Fixed: new claims show up straight away
A plane you've just claimed now appears in your Collection right away. Before, it could take a
refresh or two.

## v16
notify: yes
summary:
- Swipe up/down to zoom
- Touch problem notice

### Zoom in on the radar
Swipe up to zoom in and down to zoom out. A small ZOOM tag shows while you're zoomed in, and the
radar goes back to your usual range by itself after 10 minutes.

### If the touchscreen stops responding
Blipscope now notices and tries to fix it by restarting, at most three times and only when nobody
is using it. If that doesn't work, it shows a "Touch unavailable" note at the top of the screen
with our support address and your device ID. The radar keeps working the whole time.
