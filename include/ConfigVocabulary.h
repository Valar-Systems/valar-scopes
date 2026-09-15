#pragma once

/**
 * DOES THE SUBMITTED FORM KNOW ABOUT THIS SETTING?
 *
 * THE BUG THIS EXISTS FOR. The config page posts the WHOLE form, and an unchecked
 * box is simply absent from the body -- so SaveToggle wrote an explicit "false"
 * for every toggle it did not see. That is correct for a form that was rendered a
 * moment ago and wrong for one that was not:
 *
 *   1. customer opens the config page and leaves the tab open (phones keep tabs
 *      open for weeks; this is normal, not careless);
 *   2. firmware updates over OTA and adds a toggle;
 *   3. customer returns to that tab, changes one unrelated setting, saves;
 *   4. every toggle their stale tab never knew about is written "false".
 *
 * Silently. No error, no banner, no log line -- invisible until the owner notices
 * something they expected has stopped, at which point it reads as "the product is
 * flaky" rather than as a config wipe with a cause. That is the worst detection
 * profile available, and it is why this is a launch item.
 *
 * The CSRF guard does not help and correctly does not: a stale tab is SAME-ORIGIN.
 *
 * THE FIX IS THAT THE FORM DECLARES ITS OWN VOCABULARY. The submit handler sends
 * `cfg-toggles`, built from the DOM as rendered -- so it cannot drift from the
 * form the way a hand-maintained list per page would -- and a toggle absent from
 * that list means "this form does not know about that setting, do not touch it".
 * A stale tab then silently UNDER-writes instead of silently wiping, which is the
 * failure direction you want.
 *
 * WHY AN ABSENT LIST MUST MEAN "WRITE NOTHING FALSE". A stale tab is precisely the
 * client that will not send the new field, so a compatibility fallback to the old
 * whole-form behaviour would exempt the only case this exists for. It also gives
 * the right failure mode if the JS ever breaks: no list, no destructive writes.
 */

#include <stddef.h>

namespace cfgvocab {

/**
 * Is `name` one of the comma-separated entries in `vocab`?
 *
 * DELIMITER-AWARE, NOT A SUBSTRING TEST. "info-type" must not match inside
 * "info-type2", and "alt" must not match inside "altcolor" -- the config page has
 * both shapes, so a naive strstr would silently authorise writing "false" to a
 * setting the form never mentioned, which is the exact bug being fixed.
 *
 * A null or empty vocabulary matches NOTHING, which is what makes an absent
 * `cfg-toggles` safe by construction rather than by a caller remembering to check.
 */
inline bool Declares(const char* vocab, const char* name)
{
    if (vocab == nullptr || name == nullptr || *name == '\0') return false;

    size_t nlen = 0;
    while (name[nlen] != '\0') ++nlen;

    const char* p = vocab;
    while (*p != '\0') {
        while (*p == ',' || *p == ' ') ++p;      // skip separators
        const char* start = p;
        while (*p != '\0' && *p != ',') ++p;     // to the end of this token
        size_t len = (size_t)(p - start);
        while (len > 0 && start[len - 1] == ' ') --len;   // trailing space
        if (len == nlen) {
            size_t i = 0;
            while (i < len && start[i] == name[i]) ++i;
            if (i == len) return true;
        }
    }
    return false;
}

} // namespace cfgvocab
