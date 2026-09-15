#pragma once

/* ============================================================================
 * WHAT THE STATS FACE DROPS WHEN IT RUNS OUT OF ROOM -- decided here, once.
 *
 * THE DEFECT THIS EXISTS FOR was never a wrong row order. It was that there WAS
 * no order: whatever happened to be written before the budget ran out survived,
 * and everything after it was silently deleted. Nothing stated a priority, so
 * nobody had ever decided one, and a row earned its place by being EARLY rather
 * than by being important.
 *
 * That mechanism has now deleted three different things:
 *
 *   the Reset-WiFi control   -- fixed locally, by reserving it a row
 *   the host name            -- fixed locally, by reserving it a row
 *   the device's IP address  -- found 2026-09-15, still space-guarded
 *
 * Three local fixes to one mechanism. The fourth would have been whatever gets
 * added next, so the order is declared here and the renderer consumes it.
 *
 * WHY IT MATTERS MORE THAN IT LOOKS. The budget is ~106 px, about five rows.
 * The IP survives only on a near-empty device, so it disappears in proportion to
 * how much history a device has -- which means it fails FIRST on long-serving
 * units, the ones that survive long enough to meet a credential rotation, go
 * NEEDS VERIFY, and then cannot say where to reach them. See
 * docs/stats-address-priority.md for the measured state table.
 * ========================================================================== */

namespace statsrows {

/**
 * Every row this face can draw, in PRIORITY order, highest first.
 *
 * Adding a row means placing it in this list, which is the point: the question
 * "what should this displace?" has to be answered deliberately instead of being
 * settled by where the code happened to go.
 */
enum class Row {
    // ---- RESERVED: guaranteed a row, whatever else is on screen -----------
    ResetWifi,      ///< how to get the device back onto a network
    Address,        ///< the IP -- how to reach it once it is on one

    // ---- SPACE-GUARDED: drawn in this order, dropped from the bottom ------
    HostName,       ///< the .local name; see WHAT LOSES below
    AircraftCount,
    HighFastNear,
    Today,
    Lifelist,
    Leaderboard,
    Feed,
    WiFiStatus,
};

/**
 * How many rows are held back from the scrolling budget.
 *
 * The renderer derives its ceiling from THIS, so reserving another row is a
 * one-line change here rather than an arithmetic edit in the middle of a
 * 300-line draw function -- which is how the ceiling and the reserved set
 * drifted apart in the first place.
 */
constexpr int RESERVED_ROWS = 2;   // ResetWifi + Address

/**
 * The ceiling every space-guarded row obeys: the first y a row may NOT occupy.
 *
 * Here rather than in the draw function so the reserved COUNT and the ceiling
 * cannot drift apart -- they did before, which is how a "reserved" row and the
 * limit that protects it stopped agreeing. `clockRow` is the clock's own y and
 * the trailing 2 px is the separation above the Reset-WiFi control.
 */
constexpr int ClockTopFor(int clockRow, int lh)
{
    return clockRow - (RESERVED_ROWS * lh) - 2;
}

/**
 * The vertical budget, and the ONE rule about how a gap is spent.
 *
 * THE BUG THIS REPLACES: rows were guarded and would not advance `y` past the
 * ceiling, but the inter-block gaps were bare `y += 6` statements and were not.
 * So a block whose heading did not fit still consumed its gap -- charging the
 * budget for a row that was never drawn, and pushing `y` past the ceiling using
 * whitespace alone.
 *
 * A gap is therefore PENDING until something actually draws. Deferring is the
 * fix rather than guarding, because a guarded gap still has to guess whether the
 * next row will fit; a deferred one simply never applies unless it does.
 *
 * Pure so it can be tested on a host -- the defect was arithmetic, and
 * arithmetic trapped inside a 300-line draw function is arithmetic nobody can
 * check. See test/host/test_stats_row_budget.cpp.
 */
struct Budget {
    // No default member initialisers: those make this non-aggregate for the
    // host toolchain, and it is always brace-initialised at both call sites.
    int y;           ///< next row's top edge
    int lh;          ///< row height
    int ceiling;     ///< first y a row may NOT occupy
    int pendingGap;  ///< charged only when a row actually draws

    /// Queue a gap before the next row. Costs nothing unless that row draws.
    void Gap(int px) { pendingGap = px; }

    /// Would a row fit, gap included?
    bool Fits() const { return y + pendingGap + lh <= ceiling; }

    /**
     * Claim a row. Returns the y to draw at, or -1 when there is no room.
     * On refusal NOTHING is consumed -- not the row, and not the pending gap,
     * which stays queued for a later row that does fit.
     */
    int Take()
    {
        if (!Fits()) return -1;
        y += pendingGap;
        pendingGap = 0;
        const int at = y;
        y += lh;
        return at;
    }
};

/* ---------------------------------------------------------------------------
 * WHAT LOSES, AND WHY -- stated because reserving something always costs
 * something, and an unstated cost is one nobody agreed to.
 *
 * THE .LOCAL NAME LOSES ITS RESERVED ROW. It is not dropped from the face: it
 * still draws, immediately above the IP, whenever there is room -- which on a
 * quiet device is most of the time. It simply stops being the row that survives
 * when only one can.
 *
 * Nothing else gives anything up. This is a SWAP, not an addition: the face
 * reserved two rows before and reserves two now. The only change is WHICH of
 * the two addresses is guaranteed.
 *
 * The reasoning, so it is not "tidied" back later:
 *
 *   - A customer reading this screen is HOLDING the device. The IP is
 *     unambiguous in that moment and needs no disambiguation.
 *   - The name is recoverable from elsewhere -- it is printed on the quick-start
 *     card and it is the setup hotspot's own SSID.
 *   - The IP is recoverable from NOWHERE except the router.
 *   - `.local` is the address that FAILS on Android and on many Windows setups.
 *     The old code said so in a comment and then gave that row the guarantee,
 *     handing the reserved slot to the address that does not work for a large
 *     share of households.
 *
 * Reserve the thing with no other source.
 * ------------------------------------------------------------------------- */

} // namespace statsrows
