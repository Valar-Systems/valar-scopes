// Host test for the join-failure classification.
//
// The mapping is a switch, so the point is not the arithmetic. It is that the
// two ways of being wrong here have very different costs, and the expensive one
// is the one that reads as helpful:
//
//   * saying WRONG PASSWORD when the password was right sends the owner to
//     re-type a correct password while an antenna or a weak link is the real
//     fault. That is precisely the two-day hole this feature exists to close,
//     and it would close it in the wrong direction.
//   * saying WEAK SIGNAL when the key was rejected leaves them adjusting their
//     router forever.
//
// So every case asserts which CAUSE it maps to, and the 15-vs-204 pair gets its
// own named assertions, because merging them is the single most likely future
// "simplification".
#include <cstdio>
#include <cstring>
#include "../../include/JoinFailure.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

using namespace joinfail;

int main()
{
    std::printf("join failure classification\n");

    // ---- key rejected -------------------------------------------------------
    check(Classify(R_AUTH_FAIL)   == Cause::WrongPassword, "AUTH_FAIL is a wrong password");
    check(Classify(R_AUTH_EXPIRE) == Cause::WrongPassword, "AUTH_EXPIRE is a wrong password");
    // THE ONE THAT ACTUALLY HAPPENS. A wrong WPA2 passphrase fails during the
    // 4-way exchange, not at auth -- so if this row is ever dropped, the real
    // world case stops being recognised while the tidy-looking ones still pass.
    check(Classify(R_4WAY_HANDSHAKE_TIMEOUT) == Cause::WrongPassword,
          "4WAY_HANDSHAKE_TIMEOUT is what a wrong WPA2 key really produces");

    // ---- not a password problem --------------------------------------------
    check(Classify(R_NO_AP_FOUND)       == Cause::NetworkNotFound, "NO_AP_FOUND is a missing network");
    check(Classify(R_HANDSHAKE_TIMEOUT) == Cause::WeakSignal,      "HANDSHAKE_TIMEOUT is RF, not the key");

    // ---- THE DISTINCTION, ASSERTED DIRECTLY --------------------------------
    // 15 and 204 are both "handshake timeout" by name and mean opposite things.
    // Anyone tidying this switch will be tempted to fold them together.
    check(Classify(R_4WAY_HANDSHAKE_TIMEOUT) != Classify(R_HANDSHAKE_TIMEOUT),
          "CONTROL: the two handshake timeouts must NOT classify the same");

    // ---- unknown stays unknown ---------------------------------------------
    check(Classify(0)   == Cause::Unknown, "0 is unknown");
    check(Classify(99)  == Cause::Unknown, "an unmapped code is unknown");
    check(Classify(255) == Cause::Unknown, "and so is the top of the range");
    // Guessing the commonest cause would be right most of the time, and wrong
    // in the one direction that costs days.
    check(Classify(99) != Cause::WrongPassword,
          "an unmapped code must NOT be reported as a wrong password");

    // ---- every cause says something, and says what to DO --------------------
    const Cause all[] = { Cause::Unknown, Cause::WrongPassword,
                          Cause::NetworkNotFound, Cause::WeakSignal };
    for (Cause c : all) {
        const Advice a = AdviceFor(c);
        check(a.l0 && a.l0[0], "every cause states what happened");
        check(a.l1 && a.l1[0], "every cause states an action");
        check(a.l2 && a.l2[0], "...including its second line");
        // A round 240x240 disc. An overrunning line is unreadable exactly when
        // it matters, so the width is asserted rather than eyeballed.
        check(std::strlen(a.l0) <= 20, "line 0 fits the disc");
        check(std::strlen(a.l1) <= 20, "line 1 fits the disc");
        check(std::strlen(a.l2) <= 20, "line 2 fits the disc");
    }

    // ---- CONTROL: the advice is not one constant ---------------------------
    // Without this, an AdviceFor() that returned the same three lines for
    // everything would satisfy every assertion above.
    check(std::strcmp(AdviceFor(Cause::WrongPassword).l0,
                      AdviceFor(Cause::WeakSignal).l0) != 0,
          "CONTROL: different causes produce different screens");
    check(std::strcmp(AdviceFor(Cause::NetworkNotFound).l0,
                      AdviceFor(Cause::Unknown).l0) != 0,
          "CONTROL: ...and not-found differs from unknown too");

    // ---- the cross-task store ----------------------------------------------
    check(LastCause() == Cause::Unknown, "nothing recorded yet reads as Unknown");
    RecordReason(R_4WAY_HANDSHAKE_TIMEOUT);
    check(LastCause() == Cause::WrongPassword, "a recorded reason is classified");
    check(LastReason() == R_4WAY_HANDSHAKE_TIMEOUT, "and the raw code is retained");
    RecordReason(R_NO_AP_FOUND);
    check(LastCause() == Cause::NetworkNotFound, "a later reason replaces it");

    if (failures == 0) std::printf("  ok\n");
    else               std::printf("  %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
