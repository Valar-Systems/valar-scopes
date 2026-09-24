import type { LedgerRow, UpstreamRow } from "./analytics";

// The setup funnel: enrolment -> first /blips request -> first card open.
// PURE -- the caller fetches the ledger and the first-request times.
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

export type Stage = "enrolled" | "blips" | "card";

export interface FunnelRow {
  dev: string;
  enrolledAt: string;
  firstBlips: string;
  firstCard: string;
  stage: Stage; // the furthest stage reached
  gapEnrolToBlipsH: number | null; // null = not measurable (see flags)
  gapBlipsToCardH: number | null;
  flag: "" | "enrolled before retention" | "requested before enrolment";
}

export interface Funnel {
  rows: FunnelRow[];
  medianEnrolToBlipsH: number | null;
  medianBlipsToCardH: number | null;
  stuck: { enrolled: string[]; blips: string[] }; // devices stuck AT that stage
}

const ms = (iso: string): number => Date.parse(iso.endsWith("Z") || iso.includes("+") ? iso : `${iso.replace(" ", "T")}Z`);
const hoursBetween = (a: string, b: string): number => Math.round(((ms(b) - ms(a)) / 3600000) * 10) / 10;

export function median(xs: number[]): number | null {
  if (!xs.length) return null;
  const s = [...xs].sort((a, b) => a - b);
  const m = Math.floor(s.length / 2);
  return s.length % 2 ? (s[m] as number) : Math.round((((s[m - 1] as number) + (s[m] as number)) / 2) * 10) / 10;
}

export function computeFunnel(
  ledger: LedgerRow[],
  firstBlips: Map<string, string>,
  firstCard: Map<string, string>,
  retentionStartMs: number,
): Funnel {
  const rows: FunnelRow[] = ledger.map((l) => {
    const fb = firstBlips.get(l.dev) ?? "";
    const fc = firstCard.get(l.dev) ?? "";
    const stage: Stage = fc ? "card" : fb ? "blips" : "enrolled";
    let flag: FunnelRow["flag"] = "";
    if (l.firstEnrolled && ms(l.firstEnrolled) < retentionStartMs) flag = "enrolled before retention";
    else if (fb && l.firstEnrolled && ms(fb) < ms(l.firstEnrolled)) flag = "requested before enrolment";
    const g1 = !flag && fb && l.firstEnrolled ? hoursBetween(l.firstEnrolled, fb) : null;
    const g2 = !flag && fb && fc ? hoursBetween(fb, fc) : null;
    return { dev: l.dev, enrolledAt: l.firstEnrolled, firstBlips: fb, firstCard: fc, stage, gapEnrolToBlipsH: g1, gapBlipsToCardH: g2, flag };
  });
  const g1s = rows.map((r) => r.gapEnrolToBlipsH).filter((g): g is number => g !== null);
  const g2s = rows.map((r) => r.gapBlipsToCardH).filter((g): g is number => g !== null);
  return {
    rows,
    medianEnrolToBlipsH: median(g1s),
    medianBlipsToCardH: median(g2s),
    stuck: {
      enrolled: rows.filter((r) => r.stage === "enrolled").map((r) => r.dev),
      blips: rows.filter((r) => r.stage === "blips").map((r) => r.dev),
    },
  };
}

// Upstream health, WORST FIRST: highest error share, then the slowest p95.
export function sortUpstreams(rows: UpstreamRow[]): UpstreamRow[] {
  const rate = (r: UpstreamRow) => (r.requests ? r.errors / r.requests : 0);
  return [...rows].sort((a, b) => rate(b) - rate(a) || b.p95 - a.p95 || a.upstream.localeCompare(b.upstream));
}
