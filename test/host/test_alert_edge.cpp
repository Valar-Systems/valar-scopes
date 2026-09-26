// Host test for include/AlertEdge.h: an alert is never consumed unseen.
//
// The sequences are the contact's visibility, one entry per UpdateVisualAlerts
// pass. E1 and E2 are the frozen bench predictions, run here with no board:
//   E1  first seen OUTSIDE the circle, then moves in  -> one edge, on entry
//   E2  first seen INSIDE                             -> one edge, immediately
// The CONTROL runs the OLD emergency rule (spend the edge on first sight) over E1
// and requires it to produce NO edge -- so this test can tell the two rules apart,
// and would go red if the new rule ever regressed into the old one.
#include <cstdio>
#include <vector>
#include "../../include/AlertEdge.h"

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); ++failures; }
}

// The edges a rule produces over a visibility sequence, as indices.
template <typename Rule>
static std::vector<int> Edges(const std::vector<bool>& vis, Rule rule)
{
    bool fired = false;
    std::vector<int> at;
    for (int i = 0; i < (int)vis.size(); ++i)
        if (rule(fired, (bool)vis[i])) at.push_back(i);
    return at;
}

// What emergency did before: take the edge on first sight, flash only if visible then.
static bool OldEmergencyRule(bool& fired, bool visible)
{
    if (fired) return false;
    fired = true;
    return visible;          // the edge is spent either way; it only SHOWS if visible
}

int main()
{
    std::printf("alert edge: never consumed unseen\n");
    auto rule = [](bool& f, bool v) { return alertedge::TakeVisibleEdge(f, v); };

    const std::vector<bool> e1 = { false, false, true, true };     // outside, outside, in, in
    const std::vector<bool> e2 = { true, true, true };             // inside from the start
    const std::vector<bool> never = { false, false, false };       // never visible
    const std::vector<bool> flap = { false, true, false, true };   // in, out, in again

    const auto a = Edges(e1, rule);
    check(a.size() == 1 && a[0] == 2, "E1: exactly one edge, on entry (index 2)");
    const auto b = Edges(e2, rule);
    check(b.size() == 1 && b[0] == 0, "E2: exactly one edge, immediately (index 0)");
    const auto c = Edges(never, rule);
    check(c.empty(), "never visible: no edge");
    bool fired = false;
    for (bool v : never) alertedge::TakeVisibleEdge(fired, v);
    check(!fired, "never visible: the edge is still waiting (fired stays false)");
    const auto d = Edges(flap, rule);
    check(d.size() == 1 && d[0] == 1, "leaving and re-entering does not re-fire");

    // CONTROL: the old rule over E1 spends the edge unseen -- zero visible edges.
    const auto old = Edges(e1, OldEmergencyRule);
    check(old.empty(), "CONTROL: the old consume-on-first-sight rule shows NOTHING for E1");

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
