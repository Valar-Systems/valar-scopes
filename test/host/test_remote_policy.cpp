// The Remote's motion policy, the pure half: src/remote/CoverPolicy.h (docs/remote-edition.md section 4;
// the 34 named checks and the 9 sabotages were frozen there, in 76ddfbd, before the header existed).
//
// One check() per named case, so a sabotage's failing set reads straight off the output:
// test/host/sabotage_remote_policy.py plants each SBn in a copy of the header and compares the FAIL
// lines against the frozen set. The header is included by bare name so that a copy can be searched first.
#include <cstdio>
#include <utility>
#include <vector>

#include "CoverPolicy.h"

using namespace coverpolicy;

static const int SCREEN = 240;
static int checks = 0, failures = 0;

static void check(const char* id, bool ok, const char* what)
{
    ++checks;
    std::printf("  %s  %-3s %s\n", ok ? "ok  " : "FAIL", id, what);
    if (!ok) ++failures;
}

// One device on screen, the policy's state for it, its settings, and every non-None intent it produced.
struct Run {
    State    s;
    Device   d;
    Settings g;
    std::vector<std::pair<uint32_t, Out>> outs;

    Out rec(uint32_t t, Out o)
    {
        if (o.intent != Intent::None) outs.push_back(std::make_pair(t, o));
        return o;
    }
    Out Press(uint32_t t, int x, int y)   { return rec(t, OnPress(s, d, g, t, x, y, SCREEN)); }
    Out Held(uint32_t t, int x, int y)    { return rec(t, OnHeld(s, d, g, t, x, y)); }
    Out Release(uint32_t t, int x, int y) { return rec(t, OnRelease(s, d, g, t, x, y)); }
    Out Idle(uint32_t t)                  { return rec(t, OnIdle(s, d, g, t)); }

    // Held frames strictly after `from` up to and including `to`, every `step` ms, at one point.
    void HoldTo(uint32_t from, uint32_t to, int x, int y, uint32_t step = 52)
    {
        for (uint32_t t = from + step; t <= to; t += step) Held(t, x, y);
    }
    void IdleTo(uint32_t from, uint32_t to, uint32_t step = 52)
    {
        for (uint32_t t = from + step; t <= to; t += step) Idle(t);
    }
    int Count(Intent i) const
    {
        int n = 0;
        for (size_t k = 0; k < outs.size(); ++k) if (outs[k].second.intent == i) ++n;
        return n;
    }
    bool Moved() const { return Count(Intent::Open) + Count(Intent::Close) > 0; }
};

static Device LiveIdle(uint16_t pos = 500)
{
    Device d;
    d.conn = Conn::Live;
    d.op = Op::Idle;
    d.positionPermille = pos;
    return d;
}

static Settings Sett(Gesture ge = Gesture::Hold, uint32_t hold = HOLD_WINDOW_MS, Layout l = Layout::Split)
{
    Settings g;
    g.gesture = ge;
    g.holdMs = hold;
    g.layout = l;
    return g;
}

static Run NewRun(Device d, Settings g)
{
    Run r;
    r.d = d;
    r.g = g;
    return r;
}

static void MakeMoving(Device& d, uint32_t now)
{
    d.op = Op::Opening;
    d.lastDirection = Op::Opening;
    d.motionSeen = true;
    d.lastMotionMs = now;
}

// ------------------------------------------------------------------------------------------ A

static void CaseA()
{
    {   // A1: a 60 s timeline of device states, OnIdle every 52 ms, no stroke.
        Run r = NewRun(Device(), Sett());
        int calls = 0;
        for (uint32_t t = 0; t <= 60000; t += 52) {
            Device d;
            if (t < 5000)       d.conn = Conn::Unknown;
            else if (t < 10000) d = LiveIdle(0);
            else if (t < 20000) { d = LiveIdle((uint16_t)((t - 10000) / 10)); MakeMoving(d, t); }
            else if (t < 25000) { d = LiveIdle(1000); d.lastDirection = Op::Opening; d.motionSeen = true; d.lastMotionMs = 20000; }
            else if (t < 30000) { d = LiveIdle(1000); d.conn = Conn::Offline; }
            else if (t < 35000) d.conn = Conn::Unknown;
            else if (t < 45000) d = LiveIdle(1000);
            else                { d = LiveIdle(1000); d.alarmPending = true; }   // an obstruction, then a tamper
            r.d = d;
            r.Idle(t);
            ++calls;
        }
        check("A1", calls >= 1000 && r.outs.empty(), "no stroke: a 60 s timeline of device states yields nothing");
    }
    {   // A2: begun Live; Offline 400..600; Live again; held to 2000.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        for (uint32_t t = 52; t <= 2000; t += 52) {
            r.d.conn = (t >= 400 && t < 600) ? Conn::Offline : Conn::Live;
            r.Held(t, 120, 60);
        }
        r.Release(2000, 120, 60);
        r.IdleTo(2000, 2500);
        check("A2", !r.Moved(), "a hold that spans a reconnect never fires");
    }
    {   // A3: begun Unknown; Live from 300; held to 2000.
        Device d = LiveIdle();
        d.conn = Conn::Unknown;
        Run r = NewRun(d, Sett());
        r.Press(0, 120, 60);
        for (uint32_t t = 52; t <= 2000; t += 52) {
            r.d.conn = t >= 300 ? Conn::Live : Conn::Unknown;
            r.Held(t, 120, 60);
        }
        r.Release(2000, 120, 60);
        r.IdleTo(2000, 2500);
        check("A3", !r.Moved(), "a hold begun before the device was Live never fires");
    }
}

// ------------------------------------------------------------------------------------------ B

static void CaseB()
{
    {   // B1 + B2: 48 presses on a moving device.
        const int pts[4][2] = { {120, 60}, {120, 180}, {120, 120}, {40, 90} };
        const Layout lays[2] = { Layout::Split, Layout::Face };
        const Gesture ges[3] = { Gesture::Hold, Gesture::DoubleTap, Gesture::SingleTap };
        int stops = 0, total = 0, presses = 0, after = 0;
        for (int p = 0; p < 4; ++p)
            for (int l = 0; l < 2; ++l)
                for (int gi = 0; gi < 3; ++gi)
                    for (int al = 0; al < 2; ++al) {
                        Device d = LiveIdle();
                        MakeMoving(d, 1000);
                        d.alarmPending = (al == 1);
                        Run r = NewRun(d, Sett(ges[gi], HOLD_WINDOW_MS, lays[l]));
                        const Out o = r.Press(1000, pts[p][0], pts[p][1]);
                        ++presses;
                        if (o.intent == Intent::Stop && o.cause == Cause::TouchDownWhileMoving) ++stops;
                        const size_t n0 = r.outs.size();
                        for (uint32_t t = 1052; t <= 4000; t += 52) { r.d.lastMotionMs = t; r.Held(t, pts[p][0], pts[p][1]); }
                        r.Release(4000, pts[p][0], pts[p][1]);
                        r.IdleTo(4000, 4600);
                        after += (int)(r.outs.size() - n0);
                        total += (int)r.outs.size();
                    }
        check("B1", presses == 48 && stops == 48, "48 presses on a moving device: each is STOP on the press edge");
        check("B2", after == 0 && total == 48, "each of those strokes yields nothing after its STOP (48 intents, all STOP)");
    }
    {   // B3: IDLE, but the position changed 1000 ms (STOP) / 1600 ms (not) before the press.
        Device d = LiveIdle();
        d.motionSeen = true;
        d.lastMotionMs = 5000;
        Run a = NewRun(d, Sett());
        const Out oa = a.Press(6000, 120, 120);
        Run b = NewRun(d, Sett());
        const Out ob = b.Press(6600, 120, 120);
        check("B3", oa.intent == Intent::Stop && ob.intent != Intent::Stop,
              "position changed 1000 ms ago is still moving; 1600 ms ago is not");
    }
    {   // B4: the policy emitted OPEN; the device still reports IDLE.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 1200, 120, 60);
        uint32_t tf = 0;
        bool fired = false;
        for (size_t k = 0; k < r.outs.size(); ++k)
            if (r.outs[k].second.intent == Intent::Open) { tf = r.outs[k].first; fired = true; }
        r.Release(1200, 120, 60);
        Run r2 = r;
        const Out o1 = r.Press(tf + 1900, 120, 180);
        const Out o2 = r2.Press(tf + 2100, 120, 180);
        check("B4", fired && o1.intent == Intent::Stop && o2.intent != Intent::Stop,
              "a press 1900 ms after the policy's OPEN is STOP; 2100 ms after is not");
    }
    {   // B5: offline, last seen OPENING.
        Device d = LiveIdle();
        MakeMoving(d, 0);
        d.conn = Conn::Offline;
        Run r = NewRun(d, Sett());
        check("B5", r.Press(10000, 120, 60).intent == Intent::Stop, "offline but last seen OPENING: a press is STOP");
    }
    {   // B6: motion begins under a held finger that has not fired.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 500, 120, 60);
        MakeMoving(r.d, 520);
        const Out o = r.Held(520, 120, 60);
        for (uint32_t t = 572; t <= 2000; t += 52) { r.d.lastMotionMs = t; r.Held(t, 120, 60); }
        r.Release(2000, 120, 60);
        r.IdleTo(2000, 2500);
        check("B6", o.intent == Intent::Stop && o.cause == Cause::MotionBeganUnderFinger && r.outs.size() == 1,
              "motion beginning under an unfired hold is STOP, and nothing follows");
    }
    {   // B7: the hold's own OPEN, then the device moves while the finger is still down.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 1000, 120, 60, 50);
        for (uint32_t t = 1050; t <= 1500; t += 50) {
            if (t >= 1100) MakeMoving(r.d, t);
            r.Held(t, 120, 60);
        }
        r.Release(1500, 120, 60);
        check("B7", r.Count(Intent::Open) == 1 && r.Count(Intent::Stop) == 0 && r.outs.size() == 1,
              "a stroke that fired never stops the motion it started");
    }
}

// ------------------------------------------------------------------------------------------ C

static void CaseC()
{
    {   // C1: window 1000 ms, both halves, the boundary.
        bool ok = true;
        const int ys[2] = { 60, 180 };
        const Intent want[2] = { Intent::Open, Intent::Close };
        for (int k = 0; k < 2; ++k) {
            Run r = NewRun(LiveIdle(), Sett());
            r.Press(0, 120, ys[k]);
            r.HoldTo(0, 998, 120, ys[k]);
            const Out o999 = r.Held(999, 120, ys[k]);
            const Out o1000 = r.Held(1000, 120, ys[k]);
            r.HoldTo(1000, 1500, 120, ys[k]);
            r.Release(1500, 120, ys[k]);
            r.IdleTo(1500, 2000);
            ok = ok && o999.intent == Intent::None && o1000.intent == want[k] && o1000.cause == Cause::Hold
                 && r.outs.size() == 1;
        }
        check("C1", ok, "window hold: nothing at 999 ms, the half's intent at exactly 1000 ms, once");
    }
    {   // C2: curtain 600 ms.
        Run r = NewRun(LiveIdle(), Sett(Gesture::Hold, HOLD_CURTAIN_MS));
        r.Press(0, 120, 60);
        r.HoldTo(0, 598, 120, 60);
        const Out o599 = r.Held(599, 120, 60);
        const Out o600 = r.Held(600, 120, 60);
        check("C2", o599.intent == Intent::None && o600.intent == Intent::Open, "curtain hold: nothing at 599 ms, OPEN at 600 ms");
    }
    {   // C3: an early release with no rejoin.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 988, 120, 60);
        r.Release(990, 120, 60);
        r.IdleTo(990, 2000);
        check("C3", r.outs.empty(), "released at 990 ms and not rejoined: nothing");
    }
    {   // C4: a dropout rejoins; progress is kept from the original press.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 500, 120, 60, 50);
        r.Release(500, 120, 60);
        r.Idle(550);
        r.Press(600, 125, 62);
        r.HoldTo(600, 950, 125, 62, 50);
        const Out o = r.Held(1000, 125, 62);
        check("C4", o.intent == Intent::Open && r.outs.size() == 1, "a rejoin 100 ms later within 20 px: OPEN at exactly 1000 ms from the original press");
    }
    {   // C5: too late to rejoin; the new press is its own hold.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 500, 120, 60, 50);
        r.Release(500, 120, 60);
        r.Press(660, 120, 60);
        r.HoldTo(660, 660 + 25 * 52, 120, 60);
        bool early = false;
        uint32_t at = 0;
        for (size_t k = 0; k < r.outs.size(); ++k) {
            if (r.outs[k].first < 660 + 1000) early = true;
            if (r.outs[k].second.intent == Intent::Open) at = r.outs[k].first;
        }
        check("C5", !early && at >= 1660 && at <= 1712 && r.outs.size() == 1,
              "re-pressed 160 ms after the release: nothing from the original; the new press fires 1000 ms after itself");
    }
    {   // C6: drift 79 px after the ring start still fires; 80 px does not, and its release is a swipe.
        Run a = NewRun(LiveIdle(), Sett());
        a.Press(0, 150, 60);
        a.HoldTo(0, 300, 150, 60);
        for (uint32_t t = 352, x = 150; t <= 1300; t += 52) { if (x > 71) x = (x - 10 > 71 ? x - 10 : 71); a.Held(t, (int)x, 60); }
        Run b = NewRun(LiveIdle(), Sett());
        b.Press(0, 150, 60);
        b.HoldTo(0, 300, 150, 60);
        for (uint32_t t = 352, x = 150; t <= 1300; t += 52) { if (x > 70) x = (x - 10 > 70 ? x - 10 : 70); b.Held(t, (int)x, 60); }
        const Out rel = b.Release(1300, 70, 60);
        check("C6", a.Count(Intent::Open) == 1 && !b.Moved() && rel.intent == Intent::Next,
              "after 250 ms, 79 px of drift is still the hold; 80 px is not, and its release is Next");
    }
    {   // C7: the centre band.
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 120);
        r.HoldTo(0, 3000, 120, 120);
        r.Release(3000, 120, 120);
        r.IdleTo(3000, 3500);
        check("C7", r.outs.empty(), "a press in the centre band, held 3 s: nothing");
    }
    {   // C8: eligibility lost mid-hold.
        Run a = NewRun(LiveIdle(), Sett());
        a.Press(0, 120, 60);
        for (uint32_t t = 52; t <= 2000; t += 52) { if (t >= 600) a.d.conn = Conn::Offline; a.Held(t, 120, 60); }
        Run b = NewRun(LiveIdle(), Sett());
        b.Press(0, 120, 60);
        for (uint32_t t = 52; t <= 2000; t += 52) { if (t >= 600) b.d.alarmPending = true; b.Held(t, 120, 60); }
        check("C8", !a.Moved() && !b.Moved(), "offline, or an alarm pending, from 600 ms: the hold never fires");
    }
    check("C9", ClampHoldMs(Kind::Window, 0) == 1000 && ClampHoldMs(Kind::Curtain, 0) == 600
                && ClampHoldMs(Kind::Window, 100) == 400 && ClampHoldMs(Kind::Curtain, 5000) == 3000
                && ClampHoldMs(Kind::Window, 1200) == 1200,
          "hold time: unset is the kind's default; clamped to 400..3000");
}

// ------------------------------------------------------------------------------------------ D

static Run Tap(Device d, Settings g, uint32_t t, int x, int y)
{
    Run r = NewRun(d, g);
    r.Press(t, x, y);
    r.Release(t + 80, x, y);
    return r;
}

static void CaseD()
{
    {
        Run r = Tap(LiveIdle(), Sett(), 0, 120, 60);
        r.IdleTo(80, 1080);
        check("D1", r.outs.empty(), "Hold gesture: a tap does nothing");
    }
    {
        const Settings g = Sett(Gesture::SingleTap);
        Run up = NewRun(LiveIdle(), g);
        up.Press(0, 120, 60);
        const Out ou = up.Release(80, 120, 60);
        Run lo = Tap(LiveIdle(), g, 0, 120, 180);
        Run mid = Tap(LiveIdle(), g, 0, 120, 120);
        mid.IdleTo(80, 1000);
        check("D2", ou.intent == Intent::Open && ou.cause == Cause::Tap && up.outs.size() == 1
                    && lo.Count(Intent::Close) == 1 && lo.outs.size() == 1 && mid.outs.empty(),
              "SingleTap: OPEN on the release edge upper, CLOSE lower, nothing in the centre band");
    }
    {
        const Settings g = Sett(Gesture::DoubleTap);
        Run one = Tap(LiveIdle(), g, 0, 120, 60);
        one.IdleTo(80, 1080);
        Run two = Tap(LiveIdle(), g, 0, 120, 60);
        two.IdleTo(80, 370);
        two.Press(380, 122, 64);
        const Out o2 = two.Release(460, 122, 64);
        Run other = Tap(LiveIdle(), g, 0, 120, 60);
        other.IdleTo(80, 370);
        other.Press(380, 120, 180);
        other.Release(460, 120, 180);
        other.IdleTo(460, 1460);
        Run late = Tap(LiveIdle(), g, 0, 120, 60);
        late.IdleTo(80, 520);
        late.Press(530, 120, 60);
        late.Release(610, 120, 60);
        late.IdleTo(610, 1610);
        check("D3", one.outs.empty() && o2.intent == Intent::Open && o2.cause == Cause::DoubleTap && two.outs.size() == 1
                    && other.outs.empty() && late.outs.empty(),
              "DoubleTap: one tap nothing; a pair 300 ms apart OPEN on the second release; other half or 450 ms nothing");
    }
    {
        Device off = LiveIdle();
        off.conn = Conn::Offline;
        Device unk = LiveIdle();
        unk.conn = Conn::Unknown;
        Run a = Tap(off, Sett(Gesture::SingleTap), 0, 120, 60);
        Run b = Tap(unk, Sett(Gesture::SingleTap), 0, 120, 60);
        Run c = Tap(off, Sett(Gesture::DoubleTap), 0, 120, 60);
        c.Press(300, 120, 60);
        c.Release(380, 120, 60);
        Run e = Tap(unk, Sett(Gesture::DoubleTap), 0, 120, 60);
        e.Press(300, 120, 60);
        e.Release(380, 120, 60);
        check("D4", a.outs.empty() && b.outs.empty() && c.outs.empty() && e.outs.empty(),
              "SingleTap and DoubleTap do nothing while Offline or Unknown");
    }
}

// ------------------------------------------------------------------------------------------ E

static Device At(uint16_t pos, Op last)
{
    Device d = LiveIdle(pos);
    d.lastDirection = last;
    return d;
}

static Intent FaceHold(Device d, int x, int y)
{
    Run r = NewRun(d, Sett(Gesture::Hold, HOLD_WINDOW_MS, Layout::Face));
    r.Press(0, x, y);
    r.HoldTo(0, 1100, x, y);
    return r.outs.size() == 1 ? r.outs[0].second.intent : Intent::None;
}

static void CaseE()
{
    check("E1", FaceHold(At(0, Op::Idle), 120, 120) == Intent::Open
                && FaceHold(At(1000, Op::Idle), 60, 200) == Intent::Close
                && FaceHold(At(400, Op::Opening), 120, 120) == Intent::Close
                && FaceHold(At(400, Op::Closing), 200, 60) == Intent::Open
                && FaceHold(At(300, Op::Idle), 120, 120) == Intent::Open
                && FaceHold(At(700, Op::Idle), 120, 120) == Intent::Close,
          "face button: closed opens, open closes, part-way reverses, no direction heads for the farther end");
    Run t0 = Tap(At(0, Op::Idle), Sett(Gesture::SingleTap, HOLD_WINDOW_MS, Layout::Face), 0, 120, 120);
    Run t1 = Tap(At(1000, Op::Idle), Sett(Gesture::SingleTap, HOLD_WINDOW_MS, Layout::Face), 0, 120, 120);
    Run th = Tap(At(0, Op::Idle), Sett(Gesture::Hold, HOLD_WINDOW_MS, Layout::Face), 0, 120, 120);
    th.IdleTo(80, 1080);
    check("E2", t0.Count(Intent::Open) == 1 && t1.Count(Intent::Close) == 1 && th.outs.empty(),
          "face button: SingleTap's tap follows the cycle; Hold's tap does nothing");
}

// ------------------------------------------------------------------------------------------ F, G

static Out SwipeOut(Device d, int dx, int dy)
{
    Run r = NewRun(d, Sett());
    r.Press(0, 150, 60);
    r.Held(50, 150 + dx / 3, 60 + dy / 3);
    r.Held(100, 150 + 2 * dx / 3, 60 + 2 * dy / 3);
    return r.Release(150, 150 + dx, 60 + dy);
}

static void CaseF()
{
    Device off = LiveIdle();
    off.conn = Conn::Offline;
    Device unk;
    check("F1", SwipeOut(LiveIdle(), -60, 0).intent == Intent::Next && SwipeOut(LiveIdle(), 60, 0).intent == Intent::Prev
                && SwipeOut(off, -60, 0).intent == Intent::Next && SwipeOut(unk, 60, 0).intent == Intent::Prev,
          "a horizontal swipe moves between devices, Live, Offline or Unknown");
    check("F2", SwipeOut(LiveIdle(), 0, 60).intent == Intent::None, "a vertical swipe does nothing");
}

static void CaseG()
{
    {
        Device d = LiveIdle();
        d.alarmPending = true;
        Run tap = Tap(d, Sett(), 0, 120, 60);
        Run hold = NewRun(d, Sett());
        hold.Press(0, 120, 60);
        hold.HoldTo(0, 2000, 120, 60);
        const Out rel = hold.Release(2000, 120, 60);
        const Out sw = SwipeOut(d, -60, 0);
        check("G1", tap.Count(Intent::Ack) == 1 && tap.outs.size() == 1 && rel.intent == Intent::Ack && hold.outs.size() == 1
                    && sw.intent == Intent::None,
              "alarm pending: a tap acks, a 2 s press acks on release and never moves anything, a swipe is nothing");
    }
    {
        Device d = LiveIdle();
        MakeMoving(d, 1000);
        d.alarmPending = true;
        Run r = NewRun(d, Sett());
        const Out p = r.Press(1000, 120, 60);
        const Out rel = r.Release(1080, 120, 60);
        r.d.op = Op::Idle;
        r.d.lastMotionMs = 1100;
        r.Press(4000, 120, 60);
        const Out ack = r.Release(4080, 120, 60);
        check("G2", p.intent == Intent::Stop && rel.intent == Intent::None && ack.intent == Intent::Ack,
              "alarm pending and moving: the press is STOP, its release nothing; a new tap acks");
    }
}

// ------------------------------------------------------------------------------------------ H, I, J

static std::vector<uint32_t> Resends(uint32_t restartAt, uint32_t movingUntil, uint32_t to)
{
    Resend r;
    StartStop(r, 0);
    std::vector<uint32_t> at;
    for (uint32_t t = 50; t <= to; t += 50) {
        if (restartAt && t == restartAt) StartStop(r, t);
        if (ResendDue(r, t, t < movingUntil)) at.push_back(t);
    }
    return at;
}

static void CaseH()
{
    const std::vector<uint32_t> a = Resends(0, 100000, 5000);
    check("H1", a.size() == 3 && a[0] == 500 && a[1] == 1000 && a[2] == 1500, "STOP resent at +500, +1000, +1500 and never again");
    check("H2", Resends(0, 450, 5000).empty(), "not moving at +500: no resend");
    const std::vector<uint32_t> c = Resends(700, 100000, 5000);
    int afterRestart = 0;
    for (size_t k = 0; k < c.size(); ++k) if (c[k] > 700) ++afterRestart;
    check("H3", afterRestart == 3, "a new STOP restarts the count: 3 more resends");
}

static void CaseIJ()
{
    {
        Run r = NewRun(LiveIdle(), Sett());
        r.Press(0, 120, 60);
        r.HoldTo(0, 1100, 120, 60);
        r.Release(1100, 120, 60);
        r.IdleTo(1100, 11100);
        check("I1", r.Count(Intent::Open) == 1 && r.outs.size() == 1, "OPEN is never retried: one OPEN, then 10 s of nothing");
    }
    State s;
    Device unk;
    Device off = LiveIdle();
    off.conn = Conn::Offline;
    Device live = LiveIdle();
    Device moving = LiveIdle();
    MakeMoving(moving, 5000);
    Device alarm = LiveIdle();
    alarm.alarmPending = true;
    Device settling = LiveIdle();
    settling.motionSeen = true;
    settling.lastMotionMs = 4000;
    check("J1", !ControlsEnabled(unk, s, 5000) && !ControlsEnabled(off, s, 5000) && ControlsEnabled(live, s, 5000)
                && !ControlsEnabled(moving, s, 5000) && !ControlsEnabled(alarm, s, 5000) && !ControlsEnabled(settling, s, 5000),
          "the screen offers OPEN/CLOSE only when Live, still, and with no alarm pending");
}

int main()
{
    std::printf("Remote motion policy (src/remote/CoverPolicy.h)\n");
    CaseA();
    CaseB();
    CaseC();
    CaseD();
    CaseE();
    CaseF();
    CaseG();
    CaseH();
    CaseIJ();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
