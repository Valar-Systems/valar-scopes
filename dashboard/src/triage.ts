import type { BootRow, LedgerRow, OtaRow } from "./analytics";
import { driftFreshness, type DriftStatus } from "./drift";

// The Fleet page's triage list: five questions, each answered by a count and the
// device ids behind it. PURE -- every input is fetched by the caller -- so each
// item can be tested with a fixture that makes it non-empty and a control that
// keeps it empty.
//
// COUNTS AND DEVICE HEALTH ONLY. Nothing here reads what a device was used on.

// ---------------------------------------------------------------- boot reasons

// The reset-reason strings the firmware reports (src/OtaUpdater.cpp
// ResetReasonName, plus the deferred-cause suffixes _NETWD and, from v15,
// _TOUCHWD). A power-on or a software/host reset is what a working device does;
// everything else is a crash, a watchdog, a brownout, or one of OUR watchdogs
// rebooting to recover -- worth a look either way.
export const EXPECTED_BOOT_REASONS = ["POWERON", "SW", "EXT", "USB", "JTAG", "DEEPSLEEP"] as const;
export const FLAGGED_BOOT_REASONS = [
  "PANIC", "INT_WDT", "TASK_WDT", "WDT", "BROWNOUT", "PWR_GLITCH", "CPU_LOCKUP", "EFUSE", "SDIO",
  "SW_NETWD", "SW_TOUCHWD",
] as const;

// Anything not EXPECTED is flagged -- including UNKNOWN_<n> and any string a
// future firmware invents. An unrecognised reason is a reason to look, never a
// reason to stay quiet.
export function isFlaggedBoot(reason: string): boolean {
  return !(EXPECTED_BOOT_REASONS as readonly string[]).includes(reason);
}

// Triage item 3 (OTA failures) looks back a FIXED 7 days, whatever window the
// page is showing -- one constant, used by the query (index.ts) and by the label.
export const OTA_TRIAGE_HOURS = 168;

// ---------------------------------------------------------------- the list

export interface TriageItem {
  key: string;
  title: string;
  window: string; // what "recent" means for this item, stated on the page
  devices: string[]; // the ids behind the count
  detail: string[]; // one line per device, same order (no subjects of use)
  note?: string; // e.g. the drift state when that item is not a device list
  level?: "amber" | "red"; // the drift item: amber = stale, red = failed / not clean
}

export interface TriageInput {
  ledger: LedgerRow[];
  seen7d: Set<string>; // ids with any request in 7 days
  seenRetention: Set<string>; // ids with any request in the retention window
  latestBoots: BootRow[]; // most recent boot per device, 30 days
  otaNotOk: OtaRow[]; // OTA results other than "ok", in the FIXED 7-day window
  drift: DriftStatus;
  nowMs?: number; // for the drift run's age; defaults to Date.now()
}

export function computeTriage(t: TriageInput): TriageItem[] {
  const enrolled = t.ledger.map((l) => l.dev);

  const silent7 = enrolled.filter((d) => !t.seen7d.has(d));
  const neverSeen = enrolled.filter((d) => !t.seenRetention.has(d));
  const crashed = t.latestBoots.filter((b) => isFlaggedBoot(b.reason));
  const drift = t.drift;
  const fresh = driftFreshness(drift, t.nowMs ?? Date.now());
  // Red: the job failed (or could not be read), or the run found drift. Amber: a
  // CLEAN that is over 26 h old. Either way the line says how old the run is.
  const level: "amber" | "red" | undefined =
    fresh.level === "red" || drift.state === "DRIFT" ? "red" : fresh.level === "amber" || drift.state !== "CLEAN" ? "amber" : undefined;

  return [
    {
      key: "silent7",
      title: "Enrolled, no request in 7 days",
      window: "7 days, by last request (never the ledger's lastAt)",
      devices: silent7,
      detail: silent7.map(() => "no request in 7 days"),
    },
    {
      key: "crashBoot",
      title: "Last boot was a crash, watchdog or brownout",
      window: "most recent boot in 30 days",
      devices: crashed.map((b) => b.dev),
      detail: crashed.map((b) => `${b.reason} at ${b.at}`),
    },
    {
      key: "otaFail",
      title: "OTA attempts that did not succeed",
      window: `${OTA_TRIAGE_HOURS / 24} days, fixed -- whatever window the page is showing`,
      devices: t.otaNotOk.map((o) => o.dev),
      detail: t.otaNotOk.map((o) => `${o.result} v${o.fwFrom}->v${o.fwTo} at ${o.when}`),
    },
    {
      key: "neverSeen",
      title: "Enrolled, never made a request (setup not completed)",
      window: "the 90 days Analytics Engine retains",
      devices: neverSeen,
      detail: neverSeen.map(() => "no request of any kind"),
    },
    {
      key: "drift",
      title: "Render drift not CLEAN",
      window: "the last photo-drift run",
      devices: [],
      note: level ? `${drift.state}${drift.detail ? ` -- ${drift.detail}` : ""} · ${fresh.label}${fresh.level === "amber" ? " (stale: over 26 h)" : ""}` : undefined,
      level,
      // shown even when clean, so the age is always on the line
      detail: [`${drift.state} · ${fresh.label}`],
    },
  ];
}

// An item's count: devices for the device items, 1/0 for the drift item.
export const triageCount = (i: TriageItem): number => (i.key === "drift" ? (i.note ? 1 : 0) : i.devices.length);
