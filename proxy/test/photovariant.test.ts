import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { FULLBLEED_MIN_FW, isValidPhotoKey, resolvePhoto, squareSizeFor } from "../src/photos";
import { deriveBlobKey, pointerKey } from "../src/photolicense";

// The full-bleed gate (issue #209). Firmware up to FW 6 draws the photo into a
// fixed 150x100 slot and its drawJpg call passes no scale, so maxWidth/maxHeight
// CLIP: a 240x240 blob renders as its own top-left corner, on every card, with a
// 200 on the wire and nothing in any log. Nothing else in the system prevents
// that -- the device fetches whatever path it is handed -- so these tests are the
// guard, and they are written to fail loudly in the direction that ships a
// broken card rather than the one that merely misses an upgrade.

describe("squareSizeFor", () => {
  it("serves the legacy rectangle to every firmware that predates the full-bleed card", () => {
    for (let fw = 0; fw < FULLBLEED_MIN_FW; fw++) {
      expect(squareSizeFor(String(fw), "s3-128"), `fw ${fw}`).toBeNull();
    }
  });

  it("serves a square only at or past the introducing version", () => {
    expect(squareSizeFor(String(FULLBLEED_MIN_FW), "s3-128")).toBe(240);
    expect(squareSizeFor(String(FULLBLEED_MIN_FW + 40), "s3-128")).toBe(240);
  });

  it("maps each shipping SKU to its own panel, not to a single assumed size", () => {
    const fw = String(FULLBLEED_MIN_FW);
    expect(squareSizeFor(fw, "s3-128")).toBe(240);
    expect(squareSizeFor(fw, "s3-146")).toBe(412);
    expect(squareSizeFor(fw, "s3-21")).toBe(480);
  });

  it("falls back to the rectangle for anything it does not positively recognise", () => {
    const fw = String(FULLBLEED_MIN_FW);
    // A future SKU whose square artifacts do not exist yet must not be guessed at.
    expect(squareSizeFor(fw, "s3-175-amoled")).toBeNull();
    expect(squareSizeFor(fw, "brand-new-sku")).toBeNull();
    expect(squareSizeFor(fw, "")).toBeNull();
    expect(squareSizeFor(fw, null)).toBeNull();
    // Garbage, absent or hostile FW headers are all "too old", never "new enough".
    for (const bad of [null, "", "   ", "abc", "NaN", "-1", "6.9", "v7", "1e9999"]) {
      expect(squareSizeFor(bad, "s3-128"), `fw ${JSON.stringify(bad)}`).toBeNull();
    }
  });
});

describe("resolvePhoto variant selection", () => {
  const HEX = "a1b2c3";
  const TYPE = "E75L";

  it("hands an old device the legacy blob even when a square exists", async () => {
    await env.ENRICH_KV.put(pointerKey("type", TYPE), "photo:E75L-deadbeef");
    await env.ENRICH_KV.put(pointerKey("type", TYPE, 240), "photo:E75L-5240fee0");

    const ref = await resolvePhoto(env, HEX, TYPE, squareSizeFor("6", "s3-128"));
    expect(ref?.key).toBe("photo:E75L-deadbeef");
  });

  it("hands a new device the square blob", async () => {
    await env.ENRICH_KV.put(pointerKey("type", TYPE), "photo:E75L-deadbeef");
    await env.ENRICH_KV.put(pointerKey("type", TYPE, 240), "photo:E75L-5240fee0");

    const ref = await resolvePhoto(env, HEX, TYPE, squareSizeFor("7", "s3-128"));
    expect(ref?.key).toBe("photo:E75L-5240fee0");
  });

  it("gives a full-bleed device NOTHING rather than a rectangle it cannot place", async () => {
    // A new pick, an interrupted ingest, a half-deployed environment. The
    // tempting answer is "serve the rectangle, some photo beats no photo" -- and
    // it is wrong: the full-bleed card has no slot, so a 150x100 blob in a 240
    // disc is misplaced wherever it lands. Observed on the bench 2026-08-10 as a
    // fragment in the corner of an otherwise blank card, with hasPhoto=1 and no
    // error logged anywhere. null puts the card into its DESIGNED no-photo state.
    await env.ENRICH_KV.put(pointerKey("type", "PA24"), "photo:PA24-0badcafe");
    expect(await resolvePhoto(env, HEX, "PA24", 240)).toBeNull();
    // ...and the same library still serves that rectangle to old firmware.
    expect((await resolvePhoto(env, HEX, "PA24", null))?.key).toBe("photo:PA24-0badcafe");
  });

  it("does not let one device's panel size read another's pointer", async () => {
    await env.ENRICH_KV.put(pointerKey("type", "B738", 240), "photo:B738-11112222");
    await env.ENRICH_KV.put(pointerKey("type", "B738", 480), "photo:B738-33334444");
    expect((await resolvePhoto(env, HEX, "B738", 240))?.key).toBe("photo:B738-11112222");
    expect((await resolvePhoto(env, HEX, "B738", 480))?.key).toBe("photo:B738-33334444");
  });

  it("prefers a per-hex square override over a type square", async () => {
    await env.ENRICH_KV.put(pointerKey("hex", "abc123", 240), "photo:abc123-aaaa1111");
    await env.ENRICH_KV.put(pointerKey("type", "A320", 240), "photo:A320-bbbb2222");
    const ref = await resolvePhoto(env, "abc123", "A320", 240);
    expect(ref).toEqual({ key: "photo:abc123-aaaa1111", kind: "hex" });
  });

  it("still returns null when the library has neither variant", async () => {
    expect(await resolvePhoto(env, "ffffff", "ZZZZ", 240)).toBeNull();
    expect(await resolvePhoto(env, "ffffff", "ZZZZ", null)).toBeNull();
  });
});

describe("the keys the ingest actually derives are servable", () => {
  // THIS IS THE TEST THAT WAS MISSING, and its absence shipped a bug to
  // production KV. Every fixture above is a hand-written key of the correct
  // shape -- a DESCRIPTION of what the ingest emits. The ingest was emitting
  // `photo:C182-s240-<hash>` (size suffixed into the target segment), which
  // BLOB_KEY_RE rejects because the target must be alphanumeric. resolvePhoto
  // therefore discarded every square pointer and fell back to the rectangle,
  // silently, with a 200 on the wire and nothing in any log.
  //
  // So this asserts on deriveBlobKey's real output rather than on a key shaped
  // the way we believe it should be. Same rule as the photo playbook's contact
  // sheet: the check operates on the artifact, never on a description of it.
  it("round-trips deriveBlobKey through isValidPhotoKey", async () => {
    const bytes = new Uint8Array([0xff, 0xd8, 0xff, 0xe0, 1, 2, 3, 4]);
    for (const target of ["C182", "B738", "E75L", "A320", "H60", "a1b2c3", "U2"]) {
      const key = await deriveBlobKey(target, bytes);
      expect(isValidPhotoKey(key), `${target} -> ${key}`).toBe(true);
    }
  });

  it("rejects the suffixed shape the ingest briefly emitted", () => {
    // The literal key read back out of production KV on 2026-08-10. Pinned so the
    // failure is documented rather than only fixed: if someone reintroduces a
    // suffix in the blob key, this says what breaks and how quietly.
    expect(isValidPhotoKey("photo:C182-s240-ea58fa97")).toBe(false);
    expect(isValidPhotoKey("photo:B738-s240-13da843b")).toBe(false);
    // ...while the shape it should have been stays servable.
    expect(isValidPhotoKey("photo:C182-ea58fa97")).toBe(true);
  });

  it("gives the same type's three panel sizes three distinct servable keys", async () => {
    // Content-addressing alone separates them -- which is why no size suffix is
    // needed in the blob key, and why adding one broke serving for no gain.
    const keys = await Promise.all(
      [240, 412, 480].map((n) => deriveBlobKey("C182", new Uint8Array([n & 0xff, n >> 8, 9, 9]))),
    );
    expect(new Set(keys).size).toBe(3);
    for (const k of keys) expect(isValidPhotoKey(k), k).toBe(true);
  });
});

describe("pointerKey", () => {
  it("leaves the legacy key byte-identical, which is what protects the field", () => {
    // If this ever changes, every device already shipped stops finding its photo.
    expect(pointerKey("type", "A320")).toBe("pptr:t:A320");
    expect(pointerKey("hex", "a1b2c3")).toBe("pptr:h:a1b2c3");
  });

  it("suffixes the square variants distinctly per size", () => {
    expect(pointerKey("type", "A320", 240)).toBe("pptr:t:A320:s240");
    expect(pointerKey("hex", "a1b2c3", 480)).toBe("pptr:h:a1b2c3:s480");
  });
});
