// DrillMachine — see the header for why this file includes nothing.
//
// The only include is its own header. If a future edit reaches for Arduino.h,
// millis(), Serial or String, test/host/no-arduino.cpp is what fails.

#include "DrillMachine.h"

namespace game {

namespace {

/// Progress through a fixed-duration phase, clamped, in per mille.
uint16_t Permille(uint64_t elapsed_us, uint32_t span_us) {
  if (span_us == 0) return 1000;
  if (elapsed_us >= span_us) return 1000;
  // 64-bit before the multiply: elapsed * 1000 overflows 32 bits at ~4.3 s.
  return static_cast<uint16_t>((elapsed_us * 1000u) / span_us);
}

/// Saturating subtraction. A countdown that wraps to 584,000 years is worse
/// than one that reads zero, and unsigned underflow does exactly that.
uint64_t Remaining(uint64_t deadline_us, uint64_t now_us) {
  return deadline_us > now_us ? deadline_us - now_us : 0;
}

}  // namespace

void DrillMachine::SetNote(const char* s) {
  size_t i = 0;
  const size_t cap = sizeof(st_.note) - 1;
  while (s[i] != '\0' && i < cap) {
    st_.note[i] = s[i];
    i += 1;
  }
  st_.note[i] = '\0';
}

void DrillMachine::Enter(Phase p, uint64_t now_us) {
  st_.phase = p;
  phase_since_us_ = now_us;
  st_.progress_permille = 0;
}

void DrillMachine::Step(Event ev, uint64_t now_us) {
  // -----------------------------------------------------------------------
  // RAIL 1, ENFORCED HERE RATHER THAN REMEMBERED.
  //
  // A phase that animates is only ever entered from a branch below that a
  // Player* event reached. Tick never advances INTO Printing or Terminal; it
  // only advances OUT of them and runs the clock. A burst of feed traffic is
  // MessageArrived and OtherMessageArrived, and neither can reach an animating
  // phase from anywhere.
  // -----------------------------------------------------------------------

  // Abort is available from any live phase, and from nowhere else. Placed
  // first so no phase can accidentally swallow it.
  if (ev == Event::PlayerAbort) {
    if (st_.phase != Phase::Idle && st_.phase != Phase::Complete
        && st_.phase != Phase::Aborted) {
      SetNote("aborted by operator");
      Enter(Phase::Aborted, now_us);
    }
    return;
  }

  // A DIFFERENT MESSAGE ARRIVING MUST NOT DISTURB THIS DRILL.
  //
  // §3's loop is per-message and the fleet computes the same T, so a busy night
  // delivers messages while a drill is live. Dropping the drill to show the new
  // one would make a traffic spike destroy the player's committed sortie —
  // which §4 logs as a failed execution. It is silently ignored; the caller
  // decides whether to queue it.
  if (ev == Event::OtherMessageArrived) return;

  // A FEED RECONNECT IS NOT A GAME EVENT. The drill runs on the device's own
  // monotonic clock against a T that was already derived; losing and regaining
  // the feed changes neither. Explicitly a no-op rather than falling through,
  // so a future edit has to argue with this comment.
  if (ev == Event::FeedReconnected) return;

  switch (st_.phase) {
    case Phase::Idle:
      if (ev == Event::MessageArrived) {
        // STATIC. This is the state a traffic burst produces.
        Enter(Phase::Offered, now_us);
      }
      return;

    case Phase::Offered:
      // The only way forward is a human opening it.
      if (ev == Event::PlayerOpen) Enter(Phase::Printing, now_us);
      return;

    case Phase::Printing:
      if (ev == Event::Tick) {
        st_.progress_permille = Permille(now_us - phase_since_us_, cfg_.print_us);
        // The print finishing on its own is fine: a human started it.
        if (st_.progress_permille >= 1000) Enter(Phase::Authenticate, now_us);
      } else if (ev == Event::PrintFinished) {
        Enter(Phase::Authenticate, now_us);
      }
      return;

    case Phase::Authenticate:
      // §4: "Acking commits you. Missing T after commitment is a logged failed
      // execution." The commitment is recorded HERE, at the ack, not at the
      // window — that is the whole of the ratio the score is built on.
      if (ev == Event::PlayerAck) {
        st_.committed = true;
        Enter(Phase::WarPlan, now_us);
      }
      return;

    case Phase::WarPlan:
      if (ev == Event::PlayerConfirmWarPlan) Enter(Phase::Enable, now_us);
      return;

    case Phase::Enable:
      // Solo path (rail 2). The two-person split-knowledge minigame is not
      // here and must not be added before the arm runs measure whether the
      // panel can hold a finger for 10 s.
      if (ev == Event::PlayerEnable) Enter(Phase::Armed, now_us);
      return;

    case Phase::Armed: {
      if (ev == Event::PlayerKeyTurn) {
        // EARLY. The window has not opened, so this is not an execution.
        // Recorded rather than ignored: a key turned before the window is a
        // real thing the player did, and silently discarding it would let the
        // device show nothing happening while the player is certain they acted.
        st_.deviation_us = static_cast<int64_t>(now_us) - static_cast<int64_t>(st_.t_at_us);
        st_.executed = true;
        SetNote("keyed before the window");
        Enter(Phase::Aborted, now_us);
        return;
      }
      if (ev != Event::Tick) return;
      st_.until_window_us = Remaining(st_.t_at_us, now_us);
      // T ALREADY PAST AT ARM TIME is a real case: a snap-execution message
      // (§6's ~2-minute tier) can be opened late. `>= t_at_us` catches both the
      // ordinary arrival at T and the case where the drill armed after it, and
      // the window's own expiry below then closes it honestly.
      if (now_us > st_.t_at_us + cfg_.window_us) {
        // T IS NOT MERELY PAST, THE WINDOW HAS ALREADY CLOSED. Falling into
        // Window here would offer a key turn that could never be on time and
        // would then abort a tick later — the device showing an open window
        // that is not open, which is the product stating something false.
        //
        // Found by the host test rather than by reading: the first version
        // checked only `now >= t_at_us`, which is true forever after T.
        SetNote("window closed, no key");
        Enter(Phase::Aborted, now_us);
        return;
      }
      if (now_us >= st_.t_at_us) Enter(Phase::Window, now_us);
      return;
    }

    case Phase::Window: {
      if (ev == Event::PlayerKeyTurn) {
        // THE MEASUREMENT. Signed, against T, in microseconds, and that is all
        // this file does with it (rail 3). Negative would mean early, which is
        // unreachable from here by construction — the window opens at T — but
        // the arithmetic is signed anyway so the field means one thing
        // everywhere it appears, including from Armed above.
        st_.deviation_us = static_cast<int64_t>(now_us) - static_cast<int64_t>(st_.t_at_us);
        st_.executed = true;
        st_.until_window_us = 0;
        Enter(Phase::Committed, now_us);
        return;
      }
      if (ev != Event::Tick) return;
      st_.until_window_us = 0;
      const uint64_t closes_at = st_.t_at_us + cfg_.window_us;
      // STRICTLY PAST THE CLOSE. At exactly closes_at the window is still open:
      // a 2-second window means [T, T+2s], and treating the final instant as
      // shut would make the published constant 2 s minus one tick.
      if (now_us > closes_at) {
        SetNote("window closed, no key");
        Enter(Phase::Aborted, now_us);
      }
      return;
    }

    case Phase::Committed:
      // §3 step 5 — the fleet may second or inhibit. The device waits; nothing
      // here animates, because nothing has been decided.
      //
      // ---------------------------------------------------------------------
      // THIS PARK IS A RULING, NOT AN UNFINISHED BRANCH.
      //
      // Recorded in valar-eam-feed/docs/standing-rulings.md under Device,
      // because a park reads as a gap and the obvious next edit is to fill it
      // in with a timer.
      //
      // §3 step 6's published 30-second terminal countdown begins when the
      // launch vote RESOLVES — seconded, inhibited, or the dead-man timer
      // expiring. That resolution is the SERVER's to report. A device that
      // starts its own countdown is manufacturing a detonation result it does
      // not have, on a product whose entire tone posture rests on never doing
      // that. It would also be a second implementation of an outcome the
      // server already owns, which is how the deviation curve would drift if
      // it were duplicated here.
      //
      // So the honest state is: committed, and waiting. D7 wires the
      // resolution in and Phase::Terminal is entered from it.
      // ---------------------------------------------------------------------
      if (ev == Event::Tick) {
        st_.until_impact_us = 0;
      }
      return;

    case Phase::Terminal:
      if (ev == Event::Tick) {
        const uint64_t ends_at = phase_since_us_ + cfg_.terminal_us;
        st_.until_impact_us = Remaining(ends_at, now_us);
        st_.progress_permille = Permille(now_us - phase_since_us_, cfg_.terminal_us);
        if (st_.until_impact_us == 0) Enter(Phase::Complete, now_us);
      }
      return;

    case Phase::Complete:
    case Phase::Aborted:
      // Terminal states. A drill that has ended stays ended until Reset().
      return;
  }
}

}  // namespace game
