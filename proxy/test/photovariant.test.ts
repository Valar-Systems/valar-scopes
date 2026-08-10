import { env } from "cloudflare:test";
import { describe, expect, it } from "vitest";
import { FULLBLEED_MIN_FW, resolvePhoto, squareSizeFor } from "../src/photos";
import { pointerKey } from "../src/photolicense";

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

  it("falls back to the rectangle when the square has not been ingested yet", async () => {
    // A new pick, an interrupted ingest, a half-deployed environment. The card
    // shows a rectangle rather than no photo -- the square library is a layer
    // over the old one, never a replacement that can strand a card.
    await env.ENRICH_KV.put(pointerKey("type", "PA24"), "photo:PA24-0badcafe");
    const ref = await resolvePhoto(env, HEX, "PA24", 240);
    expect(ref?.key).toBe("photo:PA24-0badcafe");
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
