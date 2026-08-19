// FactoryReset — see the header.

#include "FactoryReset.h"

#include <Preferences.h>

#include "WiFiManagerHelpers.h"  // ForgetFastAp() and the FAST_NS name

namespace factoryreset
{
    namespace
    {
        /// The one key that survives a factory reset, and the reason it does.
        ///
        /// "cloud-key-fac" is written exactly once, by the enrollment route, and
        /// is never rendered and never writable from the settings form -- see
        /// the ENROLLMENT KEY LANDING block in ConfigurationWebServer.cpp. That
        /// design makes "clear the key box and save" a safe repair instead of an
        /// unrecoverable act, and it has the same consequence here: a customer
        /// who factory-resets to fix a problem must not lose the one value they
        /// cannot type back in.
        ///
        /// RESALE IS A DIFFERENT OPERATION and is deliberately not this one. A
        /// board changing hands needs its cloud identity AND its leaderboard row
        /// released together, server side included; doing half of that here --
        /// clearing the key while the MAC-derived leaderboard id stays the same
        /// -- would leave the device unable to reach the cloud while its old
        /// score still sits on the public board under the new owner's device.
        /// See docs/factory-reset.md.
        constexpr char PRESERVE_KEY[] = "cloud-key-fac";

        /// Wipe one namespace, optionally carrying one key across.
        ///
        /// Returns false when the namespace could not be opened, which on ESP32
        /// means it does not exist -- an edition that never wrote it, or a
        /// device that never enrolled. That is not an error and it is not
        /// silence either: the caller leaves it out of the `cleared=` list, so
        /// the audit line reports what was actually cleared rather than what was
        /// attempted.
        bool ClearNamespace(const char* ns, const char* preserve = nullptr)
        {
            Preferences p;
            if (!p.begin(ns, false))
                return false;

            String kept;
            bool hadKept = false;
            if (preserve != nullptr && p.isKey(preserve)) {
                kept = p.getString(preserve, "");
                hadKept = kept.length() > 0;
            }

            const bool ok = p.clear();

            // Put it back BEFORE end(), inside the same open handle. Reopening
            // to restore would leave a window in which a reboot mid-reset loses
            // the factory key permanently.
            if (ok && hadKept)
                p.putString(preserve, kept);

            p.end();

            if (!ok)
                Serial.printf("[reset] FAILED to clear namespace=%s -- it still holds data\n", ns);
            return ok;
        }

    }  // namespace

    void Perform(Tier tier, const std::function<void()>& forgetWifiCredentials)
    {
        if (tier == Tier::None)
            return;

        // Built as a list rather than printed as it goes, so the audit line is
        // one line naming exactly what was cleared. A destructive operation that
        // reports itself across five interleaved prints is one nobody reads --
        // and an unread report is the same as a silent wipe.
        String cleared;
        const auto note = [&cleared](const char* ns) {
            if (cleared.length()) cleared += ',';
            cleared += ns;
        };

        // ---- both tiers: the network -------------------------------------
        if (forgetWifiCredentials) {
            forgetWifiCredentials();
            note("wifi");  // WiFiManager's own store, not a Preferences namespace
        }
        // The pinned BSSID belongs to the network just forgotten. Cleared for
        // BOTH tiers, because leaving it would have the next boot chase a node
        // on a network the device no longer has credentials for.
        WiFiManagerHelpers::ForgetFastAp();
        note(WiFiManagerHelpers::detail::FAST_NS);

        // ---- factory only: everything the owner put here ------------------
        String preserved;
        if (tier == Tier::Factory) {
            if (ClearNamespace("config", PRESERVE_KEY)) {
                note("config");
                preserved = PRESERVE_KEY;
            }
            if (const char* lb = LogbookNamespace()) {
                if (ClearNamespace(lb))
                    note(lb);
            }
        }

        Serial.printf("[reset] tier=%s cleared=%s", TierName(tier), cleared.c_str());
        if (preserved.length())
            Serial.printf(" preserved=%s", preserved.c_str());
        Serial.println();

        // THE LEADERBOARD IDENTITY IS NOT CLEARED HERE, AND CANNOT BE.
        //
        // DeviceIdentity::LeaderboardId() is SHA-256(MAC || salt) computed at
        // boot and cached in RAM -- it is not stored, so there is nothing in NVS
        // to erase. A factory-reset device that opts back in re-claims its own
        // server row, inheriting the previous score and callsign. Stated out
        // loud on every factory reset because the alternative is a customer
        // believing the board was blanked when its public record was not.
        if (tier == Tier::Factory)
            Serial.println("[reset] NOTE leaderboard id is MAC-derived and unchanged; "
                           "the server row is not released by this reset");
    }

}  // namespace factoryreset
