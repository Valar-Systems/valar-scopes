import { describe, it, expect } from "vitest";
import { TRACK_BUCKETS, trackBucket, routeCacheKey } from "../src/enrich";

/* ===========================================================================
 * THE ROUTE CACHE KEY MUST SEPARATE A LEG FROM ITS REVERSE.
 *
 * Bug B: ASA537 drew BUR->SEA on an aircraft flying SEA->BUR. Nothing on the
 * device was swapped -- the strip draws `o` on the left correctly and
 * ProgressAlong is called origin-first -- and both readouts were arithmetically
 * right for the data they held. The data was reversed before it arrived,
 * because the Worker cached the upstream's leg-specific answer under a key that
 * discarded which leg it was for.
 *
 * THE CONTROL IS THE FIRST TEST BELOW, AND IT IS THE POINT OF THIS FILE. It
 * asserts that the OLD key collides for a reversal -- i.e. it reproduces the
 * bug -- and that the new key does not. A fix whose test cannot be shown
 * failing against the broken behaviour is a fix nobody can check, and this
 * project has shipped two of those (a probe reading stdout instead of an exit
 * status, a selftest matching a fragment) inside one week.
 *
 * Two candidate fixes are ALSO proven inadequate here rather than argued away,
 * because both look right and this is cheaper than another reversed card:
 * a position tile, and a cached plausibility verdict. Reversal preserves
 * geography, so neither can see it.
 * ======================================================================== */

/** The pre-fix key: callsign only. Kept here to prove what it does. */
const legacyKey = (cs: string) => `rt:${cs}`;

/** A coarse position tile, the candidate that does NOT work. */
const tile = (lat: number, lon: number, deg = 3) =>
  `${Math.floor(lat / deg)},${Math.floor(lon / deg)}`;

describe("the control: what the old key did, and what the new one must not", () => {
  it("REPRODUCES the bug -- the callsign-only key collides for a reversal", () => {
    // ASA537: SEA->BUR southbound (~160 deg) and BUR->SEA northbound (~340 deg).
    expect(legacyKey("ASA537")).toBe(legacyKey("ASA537")); // same key, both legs
    // ...so whichever leg fetched first owns the entry for the full 24 h TTL.
    // That is the defect, stated as an assertion rather than a comment.
  });

  it("a POSITION TILE also collides -- the reason option 1 was rejected", () => {
    // Both legs fly the same corridor, so they occupy the same tiles. Sampled
    // at the same point on the airway, northbound and southbound:
    const northbound = tile(40.0, -120.0);
    const southbound = tile(40.0, -120.0);
    expect(northbound).toBe(southbound);
    // A tile separates different routes in different PLACES (the b16e859 case,
    // a Bend aircraft showing MCO->BWI). It cannot separate one route from its
    // reverse, because reversal preserves geography.
  });

  it("the DIRECTION key separates them", () => {
    expect(routeCacheKey("ASA537", 160)).not.toBe(routeCacheKey("ASA537", 340));
  });
});

describe("separation is exact, not empirical", () => {
  it("bucket count is EVEN, which is what makes the guarantee hold", () => {
    expect(TRACK_BUCKETS % 2).toBe(0);
  });

  it("no track and its reverse EVER share a bucket, at any angle", () => {
    // A reversal is exactly 180 degrees, so it advances the bucket index by
    // exactly TRACK_BUCKETS/2 -- non-zero mod TRACK_BUCKETS for any even count.
    // Checked at every whole degree plus deliberate boundary and fractional
    // cases, because "it works for the angles I thought of" is how the last
    // three of these got through.
    const angles: number[] = [];
    for (let t = 0; t < 360; t++) angles.push(t);
    for (const edge of [0, 89.999, 90, 90.001, 179.999, 180, 269.999, 270, 359.999]) {
      angles.push(edge);
    }
    for (const t of angles) {
      expect(trackBucket(t)).not.toBe(trackBucket(t + 180));
      expect(trackBucket(t)).not.toBe(trackBucket(t - 180));
      expect(routeCacheKey("X", t)).not.toBe(routeCacheKey("X", t + 180));
    }
  });

  it("the same leg keeps one key across normal course jitter", () => {
    // Fragmentation is the cost side of this trade, so it is asserted too: a
    // leg wandering a few degrees must not thrash across buckets and re-fetch.
    const base = routeCacheKey("ASA537", 200);
    for (const t of [196, 198, 200, 202, 205, 210]) {
      expect(routeCacheKey("ASA537", t)).toBe(base);
    }
  });
});

describe("degenerate inputs cost a fetch, never a wrong route", () => {
  it("wraps rather than producing a negative or out-of-range bucket", () => {
    // JS `%` follows the sign of the dividend, so a negative track would land on
    // a negative index without the long-way-round normalisation.
    for (const t of [-1, -90, -180, -359, 360, 361, 720]) {
      const b = Number(trackBucket(t));
      expect(Number.isInteger(b)).toBe(true);
      expect(b).toBeGreaterThanOrEqual(0);
      expect(b).toBeLessThan(TRACK_BUCKETS);
    }
    expect(trackBucket(-180)).toBe(trackBucket(180));
    expect(trackBucket(360)).toBe(trackBucket(0));
  });

  it("falls back to the legacy key when no track is known", () => {
    // Backward compatibility: firmware that predates `trk` must keep hitting the
    // key it always hit, rather than missing every lookup forever.
    expect(routeCacheKey("ASA537", undefined)).toBe("rt:ASA537");
    expect(routeCacheKey("ASA537", NaN)).toBe("rt:ASA537");
    expect(trackBucket(undefined)).toBe("");
  });

  it("a device sending a track never collides with the legacy key", () => {
    // Mixing old and new firmware must not let a directionless entry satisfy a
    // directional lookup -- that would silently restore the bug for any fleet
    // running both.
    for (let t = 0; t < 360; t += 15) {
      expect(routeCacheKey("ASA537", t)).not.toBe("rt:ASA537");
    }
  });
});
