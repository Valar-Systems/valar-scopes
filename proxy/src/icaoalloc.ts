// Unallocated regions of the ICAO 24-bit address space.
//
// Addresses here are NOT airframe identities. In practice they arrive as TIS-B /
// ADS-R rebroadcast targets -- transient surveillance track IDs the ground
// network mints for aircraft it sees by radar or UAT -- reaching us without the
// "~" non-ICAO prefix readsb would normally carry. No registry will ever hold a
// record for one, so enriching them is guaranteed-wasted work: an upstream
// round trip, a KV write, and on the device a TLS handshake and a card that
// stays blank.
//
// Measured 2026-08-12: they were ~49% of the distinct hexes and ~44% of the
// lookups in the enrich `type` gap backlog, and growing (692 of 855 type-gap
// lookups on the last full day). Excluding them is not cosmetic -- while they
// were counted, the backlog read as roughly twice its real size. See
// docs/enrichment-gap-notes.md.
//
// HOW THIS TABLE WAS DERIVED, which matters for how far to trust it: not from
// memory of ICAO Annex 10, but by histogramming the Mictronics aircraft-database
// export (447,224 registered airframes) into /16 blocks and taking the blocks
// with ZERO aircraft in them -- the registry is the other side of this contract,
// so it states the answer rather than us restating our intent.
//
// Only LARGE contiguous holes are listed. A single empty /16 is not evidence of
// anything: 0x410000-0x41ffff is empty in the export yet sits inside the United
// Kingdom's allocation, so a naive "empty in the registry" rule would blacklist
// real British aircraft. Every range below spans multiple consecutive /16s of
// unbroken emptiness, which a sampling artifact does not produce.
//
// Conservative by construction, and the asymmetry is the whole design: a missed
// range costs one pointless lookup, which is merely the status quo. A wrong
// range silently blanks a REAL aircraft -- and it blanks it in exactly the way
// this change exists to fix, so the bug would look like the thing it was
// supposed to have cured.
//
// ONE range ships. The histogram also found large empty regions at 0x910000,
// 0xb00000, 0xca0000 and 0xea0000, and an earlier draft of this file listed all
// five. That draft broke the existing enrich suite, which uses `f40001` as a
// fixture -- inside the 0xea0000 range -- and the failure was indistinguishable
// from a real aircraft going blank. "Empty in a registry snapshot" is evidence
// that a block is unallocated; it is not proof, and the other four blocks have
// no live-traffic evidence behind them at all: every single non-ICAO address
// observed in 30 days of production gap data fell in the one range below.
//
// So the rule is: a range earns its place by being seen in live traffic, not by
// being absent from a registry. Add another only when the gap data shows one.
interface Range {
  lo: number;
  hi: number;
}

const UNALLOCATED: Range[] = [
  // 13 consecutive /16s with zero registered airframes, and the source of every
  // non-ICAO address actually observed: all 912 2bxxxx and 71 29xxxx hexes in
  // the 2026-08-12 backlog are here.
  { lo: 0x230000, hi: 0x2fffff },
];

// True when `hex` cannot be an airframe identity. Accepts the readsb "~" prefix
// (already an explicit non-ICAO marker) and anything malformed, so callers get
// one predicate rather than three checks.
export function isNonIcaoAddress(hex: string): boolean {
  if (hex.startsWith("~")) return true;
  if (!/^[0-9a-f]{6}$/i.test(hex)) return true;
  const n = parseInt(hex, 16);
  return UNALLOCATED.some((r) => n >= r.lo && n <= r.hi);
}
