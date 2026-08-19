// DrawDrill — see the header. Stateless by construction.

#include "DrawDrill.h"

#include <Arduino.h>

#include "../eam/EamTheme.h"
#include "Layout.h"

// ---------------------------------------------------------------------------
// The screen-state hooks. Compiled to nothing without the flag, so the shipping
// image carries none of it.
//
// They sit BESIDE the draw call they describe, on the same line, taking the
// same arguments. A divergence is then a visible edit rather than a silent
// drift — which is the one property that makes the log evidence about the
// renderer rather than a second description of it.
#ifdef SCREEN_STATE_PROBE
#define SS_BEGIN(frame, phase) Serial.printf("%u,%s", (unsigned)(frame), (phase))
#define SS_TEXT(id, v)         Serial.printf(",text:%s=%s", (id), (v))
#define SS_NUM(id, v)          Serial.printf(",num:%s=%ld", (id), (long)(v))
#define SS_RECT(id, x, y, w, h) Serial.printf(",rect:%s=%d,%d,%d,%d", (id), (int)(x), (int)(y), (int)(w), (int)(h))
#define SS_ARC(id, d)          Serial.printf(",arc:%s=%d", (id), (int)(d))
#define SS_FLAG(id, b)         Serial.printf(",flag:%s=%d", (id), (b) ? 1 : 0)
#define SS_END()               Serial.println()
#else
#define SS_BEGIN(frame, phase) ((void)0)
#define SS_TEXT(id, v)         ((void)0)
#define SS_NUM(id, v)          ((void)0)
#define SS_RECT(id, x, y, w, h) ((void)0)
#define SS_ARC(id, d)          ((void)0)
#define SS_FLAG(id, b)         ((void)0)
#define SS_END()               ((void)0)
#endif

namespace game {
namespace {

constexpr int kC = SCREEN_SIZE_DIV_2;

const char* PhaseName(Phase p) {
  switch (p) {
    case Phase::Idle:         return "idle";
    case Phase::Offered:      return "offered";
    case Phase::Printing:     return "printing";
    case Phase::Authenticate: return "authenticate";
    case Phase::WarPlan:      return "warplan";
    case Phase::Enable:       return "enable";
    case Phase::Armed:        return "armed";
    case Phase::Window:       return "window";
    case Phase::Committed:    return "committed";
    case Phase::Terminal:     return "terminal";
    case Phase::Complete:     return "complete";
    case Phase::Aborted:      return "aborted";
  }
  return "?";
}

/// Centred text. BandCanvas exposes textWidth but not setTextDatum, so the
/// centring is arithmetic here -- the same shape as EamManager::CenterText,
/// which is the house helper this cannot reach from outside the class.
void Centre(BandCanvas& c, const char* s, int y, uint32_t colour) {
  c.setTextColor(colour);
  c.drawString(s, kC - c.textWidth(s) / 2, y);
}

/// §3 step 1 — the paper strip printing.
///
/// The strip advances on `progress_permille`, which the MACHINE owns. The scan
/// line is the one thing driven by `nowUs`, because it is decoration with no
/// state behind it: if this function derived the strip's height from a local
/// counter, two calls with the same State would draw different frames and the
/// screen-state log would stop being a function of the drill.
void DrawStrip(BandCanvas& c, const eam::Palette& palette, const State& st, uint64_t nowUs) {
  const int w = SCREEN_SIZE - 72;
  const int fullH = 96;
  const int h = (fullH * st.progress_permille) / 1000;
  const int x = kC - w / 2;
  const int y = 44;

  c.fillRoundRect(x, y, w, h, 3, palette.faint);                SS_RECT("strip", x, y, w, h);
  c.drawRoundRect(x, y, w, h, 3, palette.dim);
  // Text lines appear as the strip emerges, so the message reveals rather than
  // popping. Line count comes from the strip's own height -- one derivation.
  const int lines = h / 14;
  for (int i = 0; i < lines; i += 1) {
    c.drawFastHLine(x + 8, y + 10 + i * 14, w - 16, palette.dim);
  }
  SS_NUM("strip_lines", lines);
  if (h > 4) {
    const int scan = y + h - 2 + (int)((nowUs / 60000ull) % 3);
    c.drawFastHLine(x, scan, w, palette.accent);
  }
}

/// §3 step 1 — the padlocked SAS safe, two crew locks, crack the seal.
///
/// TWO LOCKS ARE DRAWN AND ONLY ONE IS LIVE. That is deliberate and is art
/// rather than mechanic: §8's two-person rule is the fiction the safe depicts,
/// and rail 2 keeps the SOLO path the only one built. The second lock renders
/// as already-open so the picture is honest about what this device is doing —
/// showing two closed locks and accepting one ack would be the panel implying a
/// second crew member exists.
void DrawSafe(BandCanvas& c, const eam::Palette& palette, const State& st) {
  const int r = 34;
  c.drawCircle(kC, kC, r, palette.dim);                          SS_ARC("safe", 360);
  c.drawCircle(kC, kC, r - 4, palette.faint);
  // Left lock: this crew's, closed until the ack.
  c.fillRoundRect(kC - 22, kC - 6, 14, 12, 2, palette.accent);   SS_FLAG("lock_self", true);
  c.drawFastHLine(kC - 20, kC - 10, 10, palette.accent);
  // Right lock: the absent deputy's, drawn OPEN. Solo path.
  c.drawRoundRect(kC + 8, kC - 6, 14, 12, 2, palette.faint);     SS_FLAG("lock_deputy", false);
  Centre(c, "ACK TO AUTHENTICATE", kC + r + 14, palette.dim);    SS_TEXT("prompt", "ACK TO AUTHENTICATE");
  (void)st;
}

/// The bezel arc — §3 step 4's key turn lives on it, and the window is shown on
/// it so the two are the same object rather than two things to correlate.
void DrawBezel(BandCanvas& c, const eam::Palette& palette, const State& st) {
  const int r1 = kC - 3;
  const int r0 = kC - 12;
  c.drawArc(kC, kC, r0, r1, 0, 360, palette.faint);              SS_ARC("bezel", 360);
  if (st.phase == Phase::Window) {
    // THE WHOLE RING, not a sweep. A sweeping indicator would imply the window
    // is a position to hit; it is an interval, and every instant in it is
    // equally on time until the machine says otherwise.
    c.fillArc(kC, kC, r0, r1, 0, 360, palette.accent);           SS_ARC("window_ring", 360);
  }
}

}  // namespace

void DrawDrill(BandCanvas& c, const eam::Palette& palette, const State& st, uint64_t nowUs) {
  static uint32_t frame = 0;  // LOG SEQUENCE ONLY. Never read by any draw call.
  SS_BEGIN(frame++, PhaseName(st.phase));

  c.fillScreen(palette.bg);                                      SS_RECT("bg", 0, 0, SCREEN_SIZE, SCREEN_SIZE);

  // EXERCISE, on every frame, in every phase, before anything else can fail to
  // draw. Tone rail, absolute -- and placed first so a return below cannot skip
  // it. Amber is EXERCISE and nothing else, anywhere.
  c.setTextSize(1);
  Centre(c, "EXERCISE", 16, palette.alert);                      SS_TEXT("exercise", "EXERCISE");

  switch (st.phase) {
    case Phase::Idle:
      Centre(c, "NO TRAFFIC", kC, palette.faint);                SS_TEXT("body", "NO TRAFFIC");
      break;

    case Phase::Offered:
      // STATIC, and that is the rail-1 requirement made visible: a burst of
      // real traffic parks here and nothing moves until a person acts.
      c.drawRoundRect(kC - 60, kC - 18, 120, 36, 4, palette.accent);
      Centre(c, "MESSAGE WAITING", kC - 4, palette.accent);      SS_TEXT("body", "MESSAGE WAITING");
      Centre(c, "TAP TO OPEN", kC + 12, palette.dim);            SS_TEXT("prompt", "TAP TO OPEN");
      break;

    case Phase::Printing:
      DrawStrip(c, palette, st, nowUs);                                   SS_NUM("print_permille", st.progress_permille);
      break;

    case Phase::Authenticate:
      DrawSafe(c, palette, st);
      break;

    case Phase::WarPlan:
      Centre(c, "WAR PLAN", kC - 12, palette.dim);               SS_TEXT("body", "WAR PLAN");
      Centre(c, "FDM ASSIGNED", kC + 4, palette.accent);         SS_TEXT("target", "FDM ASSIGNED");
      Centre(c, "CONFIRM", kC + 24, palette.dim);                SS_TEXT("prompt", "CONFIRM");
      break;

    case Phase::Enable:
      Centre(c, "ENABLE", kC - 6, palette.accent);               SS_TEXT("body", "ENABLE");
      Centre(c, "HOLD TO ARM", kC + 12, palette.dim);            SS_TEXT("prompt", "HOLD TO ARM");
      break;

    case Phase::Armed: {
      DrawBezel(c, palette, st);
      // SECONDS, NOT TENTHS. §13 A.3's ruling one layer out: do not display a
      // precision the clock cannot support. The countdown is a countdown, and a
      // tenths digit here would imply the device knows where it is inside a
      // 199 ms floor.
      const long secs = (long)(st.until_window_us / 1000000ull);
      c.setTextSize(2);
      Centre(c, String(secs).c_str(), kC - 4, palette.accent);   SS_NUM("countdown_s", secs);
      c.setTextSize(1);
      Centre(c, "STAND BY", kC + 22, palette.dim);               SS_TEXT("prompt", "STAND BY");
      break;
    }

    case Phase::Window:
      DrawBezel(c, palette, st);
      c.setTextSize(2);
      Centre(c, "TURN", kC - 4, palette.accent);                 SS_TEXT("body", "TURN");
      c.setTextSize(1);
      break;

    case Phase::Committed:
      Centre(c, "EXECUTION LOGGED", kC - 14, palette.accent);    SS_TEXT("body", "EXECUTION LOGGED");
      // THE DEVIATION IS DISPLAYED, NOT SCORED. The server owns the curve and
      // the bucket (rail 3); this shows the raw offset the device measured, in
      // the 0.2 s bucket §13 A.3 ruled -- so a reader is told what was measured
      // rather than what it was worth.
      if (st.executed) {
        const long tenths = (long)(st.deviation_us / 100000);
        char buf[24];
        snprintf(buf, sizeof(buf), "%+ld.%ld s", tenths / 10, (tenths < 0 ? -tenths : tenths) % 10);
        Centre(c, buf, kC + 6, palette.dim);                     SS_NUM("deviation_us", (long)st.deviation_us);
      }
      // §3 step 6 waits on the server's resolution and this screen says so
      // rather than starting a countdown. See the ruling in DrillMachine.cpp.
      Centre(c, "AWAITING FLEET", kC + 26, palette.faint);       SS_TEXT("prompt", "AWAITING FLEET");
      break;

    case Phase::Terminal: {
      const long secs = (long)(st.until_impact_us / 1000000ull);
      c.setTextSize(2);
      Centre(c, String(secs).c_str(), kC - 4, palette.alert);    SS_NUM("terminal_s", secs);
      c.setTextSize(1);
      break;
    }

    case Phase::Complete:
      Centre(c, "COMPLETE", kC, palette.dim);                    SS_TEXT("body", "COMPLETE");
      break;

    case Phase::Aborted:
      Centre(c, "NO EXECUTION", kC - 8, palette.faint);          SS_TEXT("body", "NO EXECUTION");
      // The machine's own reason, rendered verbatim. A screen that invented its
      // own wording here would be a second description of the same fact.
      if (st.note[0] != '\0') {
        Centre(c, st.note, kC + 10, palette.faint);              SS_TEXT("note", st.note);
      }
      break;
  }

  SS_FLAG("committed", st.committed);
  SS_FLAG("executed", st.executed);
  SS_END();
}

}  // namespace game
