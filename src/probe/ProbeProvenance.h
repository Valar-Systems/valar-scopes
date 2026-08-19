// ProbeProvenance -- every bench run states what produced it, on the run.
//
// ===========================================================================
// WHY THIS EXISTS, AND IT IS NOT BOOKKEEPING.
//
// test/fixtures/ntp-corrections-2026-08.json (valar-eam-feed) taught this the
// expensive way: a bare list of numbers is not re-derivable. The 75.7 ms clock
// floor came out of a session contaminated by the harness freeze, was acted on
// as a ruling, and only a LONGER, CLEANER run found 198.5 ms. Nothing in the
// first fixture said which session it came from, what build produced it, or
// what else was running -- so the two could not be told apart until somebody
// re-derived the whole thing.
//
// The gesture corpus and the frame budget are both about to become fixtures the
// same way. A corpus of touch samples with no board id is a corpus that cannot
// be retired when the batch's touch IC changes, and a frame budget with no
// build flags is a number about an unknown machine.
//
// So every probe prints this block FIRST, before any measurement, and the
// capture scripts keep it at the head of the CSV. Cheap, and the alternative is
// a week.
// ===========================================================================
//
// Ambient conditions are the one field a device cannot self-report honestly on
// this board -- there is no temperature sensor wired on the s3-128 -- so the
// banner PROMPTS for them rather than inventing them, and the runbook asks for
// them in the same breath. An invented 22 C is worse than a blank.

#ifndef BLIPSCOPE_PROBE_PROVENANCE_H
#define BLIPSCOPE_PROBE_PROVENANCE_H

#include <Arduino.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_mac.h>

namespace probe {

/// The build's own identity, printed as CSV comment lines (`# ...`).
///
/// `label` names the run so two captures in one sitting cannot be confused;
/// the runbook supplies it. Everything else is read from the running image,
/// which is the point -- a field somebody types is a field somebody mistypes.
inline void PrintProvenance(const char* label) {
  Serial.println();
  Serial.println("# ==== PROVENANCE ============================================");
  Serial.printf("# label        %s\n", label);

  // FIRMWARE HASH, from the image actually running. Not FW_VERSION: a version
  // string is what somebody meant to build, and the ELF sha256 is what booted.
  // Same rule as reading the artifact rather than the config.
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_app_desc_t desc;
  if (run != nullptr && esp_ota_get_partition_description(run, &desc) == ESP_OK) {
    Serial.printf("# app_name     %s\n", desc.project_name);
    Serial.printf("# app_version  %s\n", desc.version);
    Serial.printf("# built        %s %s\n", desc.date, desc.time);
    Serial.print("# elf_sha256   ");
    for (int i = 0; i < 8; i += 1) Serial.printf("%02x", desc.app_elf_sha256[i]);
    Serial.println();
    Serial.printf("# partition    %s @ 0x%06x\n", run->label, (unsigned)run->address);
  } else {
    // SAID OUT LOUD rather than skipped. A provenance block missing its hash
    // is a run whose identity is unknown, and a reader must not have to notice
    // an absent line to learn that.
    Serial.println("# elf_sha256   UNAVAILABLE -- this run cannot be tied to a build");
  }

  // BOARD IDENTITY. The MAC is the only stable per-unit id this hardware has,
  // and the bench has more than one board (COM118 carries long soaks, COM119 is
  // the expendable finger-test board). A corpus that cannot say which one it
  // came from cannot be retired when that unit's touch IC changes.
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  Serial.printf("# board_mac    %02x:%02x:%02x:%02x:%02x:%02x\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("# chip         %s rev%d, %d core(s)\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("# cpu_mhz      %u\n", (unsigned)getCpuFrequencyMhz());
  Serial.printf("# flash_size   %u\n", (unsigned)ESP.getFlashChipSize());
  Serial.printf("# psram_total  %u\n", (unsigned)ESP.getPsramSize());
  Serial.printf("# psram_free   %u\n", (unsigned)ESP.getFreePsram());
  Serial.printf("# heap_free    %u\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("# heap_maxblk  %u\n", (unsigned)ESP.getMaxAllocHeap());

  // BUILD FLAGS THAT CHANGE WHAT IS BEING MEASURED. Not every flag -- the ones
  // whose absence would make the numbers describe a different machine. A frame
  // budget measured without PSRAM, or a gesture corpus captured with the touch
  // watchdog re-arming the chip mid-swipe, are confident numbers about
  // something we do not ship.
  Serial.print("# variant      ");
#if defined(BLIPSCOPE_VARIANT_S3_128)
  Serial.println("S3_128");
#elif defined(BLIPSCOPE_VARIANT_S3_146)
  Serial.println("S3_146");
#elif defined(BLIPSCOPE_VARIANT_PRO_S3_21)
  Serial.println("PRO_S3_21");
#else
  Serial.println("UNSET -- geometry and pins are unknown for this run");
#endif
  Serial.print("# flags        ");
#ifdef GAMETEST_FORCE_ARM
  Serial.print("GAMETEST_FORCE_ARM ");
#endif
#ifdef PROBE_SKETCH
  Serial.print("PROBE_SKETCH ");
#endif
#ifdef FEATURE_EAM
  Serial.print("FEATURE_EAM ");
#endif
#ifdef BOARD_HAS_PSRAM
  Serial.print("BOARD_HAS_PSRAM ");
#endif
  Serial.println();

  // THE CLOCK. There is no RTC and no network in a probe sketch, so the device
  // genuinely does not know the date -- and printing an epoch of 0 dressed as a
  // timestamp is the invented number this whole block exists to prevent. The
  // build date above is the anchor; the operator supplies the wall clock.
  Serial.println("# date_utc     <fill in from the capture script>");
  Serial.println("# ambient      <fill in: room temp, board enclosed/open, USB hub or direct>");
  Serial.println("# operator     <fill in>");
  Serial.println("# ============================================================");
  Serial.println();
}

}  // namespace probe

#endif  // BLIPSCOPE_PROBE_PROVENANCE_H
