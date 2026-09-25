import type { LedgerRow, UpstreamRow } from "./analytics";

// The setup funnel: a device's START -> first /blips request -> first card open.
// PURE -- the caller fetches the ledger and the first-request times.
//
// TWO WAYS IN, and the start stage says which:
//   - "enrolled":   web enrolment wrote an enr:dev: ledger row (the customer path);
//   - "first seen": a FACTORY-PROVISIONED unit has no ledger row -- provisioning
//     writes none -- so it starts at its first authenticated request, which is the
//     provisioner's verify at the bench. Without this it was invisible here.
// Their gaps are not the same quantity (first seen -> /blips includes shipping and
// the customer's setup), so each has its own median; they are never pooled.
// Allowlisted FAKE ids (scripts/device-id-allowlist.txt) never enter as "first
// seen": beefbeefbeefbeef made 27 requests before production started refusing it.
//
// "FIRST CARD OPEN" IS THE FIRST PHOTO FETCH. The device fetches a photo when a
// detail card is opened (on a tap), so the first /photo request is the first
// card opened ON AN AIRCRAFT WITH A STOCK PHOTO. It is measured from request
// points, not from the usage report, because the usage report only exists from
// firmware 9 -- which is why a pre-v9 device can't reach the last stage here.
//
// TWO WAYS A GAP IS NOT A MEASUREMENT, and both are excluded from the medians
// and named on the page rather than silently dropped:
//   - enrolled before the retention window: Analytics Engine keeps ~90 days, so
//     the "first" request we can see is only the first one still retained;
//   - first request BEFORE enrolment: devices from the shared-key era requested
//     before per-device enrolment existed (2026-08-13), so the gap is negative.

export type Stage = "enrolled" | "seen" | "blips" | "card";
export type Origin = "enrolled" | "first seen";

export interface FunnelRow {
  dev: string;
  origin: Origin;
  enrolledAt: string; // "" for a first-seen row
  startedAt: string;  // enrolledAt, or the first authenticated request
  firstBlips: string;
  firstCard: string;
  stage: Stage; // the furthest stage reached
  gapEnrolToBlipsH: number | null; // null = not measurable (see flags)
  gapBlipsToCardH: number | null;
  flag: "" | "enrolled before retention" | "requested before enrolment" | "first seen before retention";
}

export interface Funnel {
  rows: FunnelRow[];
  medianEnrolToBlipsH: number | null; // enrolled rows only
  medianSeenToBlipsH: number | null;  // first-seen rows only -- never pooled with the above
  medianBlipsToCardH: number | null;
  stuck: { enrolled: string[]; seen: string[]; blips: string[] }; // devices stuck AT that stage
}

const ms = (iso: string): number => Date.parse(iso.endsWith("Z") || iso.includes("+") ? iso : `${iso.replace(" ", "T")}Z`);
const hoursBetween = (a: string, b: string): number => Math.round(((ms(b) - ms(a)) / 3600000) * 10) / 10;

export function median(xs: number[]): number | null {
  if (!xs.length) return null;
  const s = [...xs].sort((a, b) => a - b);
  const m = Math.floor(s.length / 2);
  return s.length % 2 ? (s[m] as number) : Math.round((((s[m - 1] as number) + (s[m] as number)) / 2) * 10) / 10;
}

// A first request within a day of the retention edge is almost certainly just the
// oldest one Analytics Engine still keeps, not the device's real first.
const RETENTION_EDGE_MS = 24 * 3600000;

export function computeFunnel(
  ledger: LedgerRow[],
  firstBlips: Map<string, string>,
  firstCard: Map<string, string>,
  retentionStartMs: number,
  firstSeen: Map<string, string> = new Map(),
  fakeIds: ReadonlySet<string> = new Set(),
): Funnel {
  const enrolledRows: FunnelRow[] = ledger.map((l) => {
    const fb = firstBlips.get(l.dev) ?? "";
    const fc = firstCard.get(l.dev) ?? "";
    const stage: Stage = fc ? "card" : fb ? "blips" : "enrolled";
    let flag: FunnelRow["flag"] = "";
    if (l.firstEnrolled && ms(l.firstEnrolled) < retentionStartMs) flag = "enrolled before retention";
    else if (fb && l.firstEnrolled && ms(fb) < ms(l.firstEnrolled)) flag = "requested before enrolment";
    const g1 = !flag && fb && l.firstEnrolled ? hoursBetween(l.firstEnrolled, fb) : null;
    const g2 = !flag && fb && fc ? hoursBetween(fb, fc) : null;
    return { dev: l.dev, origin: "enrolled", enrolledAt: l.firstEnrolled, startedAt: l.firstEnrolled, firstBlips: fb, firstCard: fc, stage, gapEnrolToBlipsH: g1, gapBlipsToCardH: g2, flag };
  });
  const ledgered = new Set(ledger.map((l) => l.dev));
  const seenRows: FunnelRow[] = [...firstSeen.entries()]
    .filter(([dev]) => !ledgered.has(dev) && !fakeIds.has(dev))
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([dev, seenAt]) => {
      const fb = firstBlips.get(dev) ?? "";
      const fc = firstCard.get(dev) ?? "";
      const stage: Stage = fc ? "card" : fb ? "blips" : "seen";
      const flag: FunnelRow["flag"] = ms(seenAt) < retentionStartMs + RETENTION_EDGE_MS ? "first seen before retention" : "";
      const g1 = !flag && fb ? hoursBetween(seenAt, fb) : null;
      const g2 = !flag && fb && fc ? hoursBetween(fb, fc) : null;
      return { dev, origin: "first seen", enrolledAt: "", startedAt: seenAt, firstBlips: fb, firstCard: fc, stage, gapEnrolToBlipsH: g1, gapBlipsToCardH: g2, flag };
    });
  const rows = [...enrolledRows, ...seenRows];
  // Medians are taken from `rows` -- the rows the page shows -- so a median can
  // never describe a device that is not on the page.
  const gapsOf = (o: Origin) => rows.filter((r) => r.origin === o).map((r) => r.gapEnrolToBlipsH).filter((g): g is number => g !== null);
  const g1s = gapsOf("enrolled");
  const gSeen = gapsOf("first seen");
  const g2s = rows.map((r) => r.gapBlipsToCardH).filter((g): g is number => g !== null);
  return {
    rows,
    medianEnrolToBlipsH: median(g1s),
    medianSeenToBlipsH: median(gSeen),
    medianBlipsToCardH: median(g2s),
    stuck: {
      enrolled: rows.filter((r) => r.stage === "enrolled").map((r) => r.dev),
      seen: rows.filter((r) => r.stage === "seen").map((r) => r.dev),
      blips: rows.filter((r) => r.stage === "blips").map((r) => r.dev),
    },
  };
}

// Upstream health, WORST FIRST: highest error share, then the slowest p95.
export function sortUpstreams(rows: UpstreamRow[]): UpstreamRow[] {
  const rate = (r: UpstreamRow) => (r.requests ? r.errors / r.requests : 0);
  return [...rows].sort((a, b) => rate(b) - rate(a) || b.p95 - a.p95 || a.upstream.localeCompare(b.upstream));
}
