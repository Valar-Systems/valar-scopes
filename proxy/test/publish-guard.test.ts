import { describe, expect, it } from "vitest";
import {
  KeyNotAllowed,
  assertAllowedKey,
  expectedPointers,
  preflight,
  verify,
} from "../scripts/publish-guard";
import type { ManifestEntry } from "../src/photolicense";

const row = (target: string, h = "aaaaaaaa"): ManifestEntry =>
  ({
    target, kind: "type", source: "s", author: "a", credit: "c", license: "CC0", layer: "auto", autoPicked: false,
    blobKey: `photo:${target}-${h}`,
    squareKeys: { "240": `photo:${target}-${h.replace(/^./, "1")}`, "412": `photo:${target}-${h.replace(/^./, "2")}`, "480": `photo:${target}-${h.replace(/^./, "3")}` },
  }) as ManifestEntry;

const sees = { knownPresentReadsBack: true, knownAbsentReadsNull: true };

// Everything a clean KV would read back for these rows.
function cleanKv(rows: ManifestEntry[]) {
  const pointers: Record<string, string | null> = {};
  for (const [k, v] of expectedPointers(rows)) pointers[k] = v;
  const blobsPresent = new Set(expectedPointers(rows).map(([, v]) => v));
  return { pointers, blobsPresent };
}

describe("allowlist (A16)", () => {
  it("CONTROL: photo:* and pptr:* keys pass", () => {
    expect(() => assertAllowedKey("photo:B738-0c7fa6a7")).not.toThrow();
    expect(() => assertAllowedKey("pptr:t:B738:s240")).not.toThrow();
    expect(() => assertAllowedKey("photo:manifest")).not.toThrow();
  });
  it("every other key family is refused", () => {
    for (const k of ["rt:AAL1719", "ap:VTF", "cfg:fleet", "enr:dev:0000000000000000", "fw:1", "", "PHOTO:x", " photo:x"]) {
      expect(() => assertAllowedKey(k), k).toThrow(KeyNotAllowed);
    }
  });
});

describe("preflight", () => {
  const two = [row("B738"), row("C182")];
  it("CONTROL: a sighted verifier and no lost rows passes", () => {
    expect(preflight({ controls: sees, published: two, next: two }).verdict).toBe("PASS");
  });
  it("a blind verifier is UNTRUSTWORTHY, not FAIL (A11)", () => {
    const blind = { knownPresentReadsBack: false, knownAbsentReadsNull: false };
    const d = preflight({ controls: blind, published: null, next: two });
    expect(d.verdict).toBe("UNTRUSTWORTHY");
    expect(d.reasons.join(" ")).toContain("Nothing was written");
  });
  it("an absent key that does not read null also makes it UNTRUSTWORTHY", () => {
    const d = preflight({ controls: { knownPresentReadsBack: true, knownAbsentReadsNull: false }, published: two, next: two });
    expect(d.verdict).toBe("UNTRUSTWORTHY");
  });
  it("a dropped row FAILs and names it (A3)", () => {
    const d = preflight({ controls: sees, published: two, next: [row("B738")] });
    expect(d.verdict).toBe("FAIL");
    expect(d.reasons.join(" ")).toContain("type:C182");
  });
  it("a first-ever publish (nothing published) is not a drop", () => {
    expect(preflight({ controls: sees, published: null, next: two }).verdict).toBe("PASS");
  });
});

describe("verify", () => {
  const rows = [row("B738"), row("C182")];
  it("CONTROL: a clean namespace passes and counts 4 pointers per row", () => {
    const r = verify({ controls: sees, manifest: rows, ...cleanKv(rows) });
    expect(r.verdict).toBe("PASS");
    expect(r.pointersChecked).toBe(8);
  });
  it("a pointer holding the wrong key FAILs, naming the pointer", () => {
    const kv = cleanKv(rows);
    kv.pointers["pptr:t:C182:s412"] = "photo:C182-deadbeef";
    const r = verify({ controls: sees, manifest: rows, ...kv });
    expect(r.verdict).toBe("FAIL");
    expect(r.pointerMismatches).toEqual(["pptr:t:C182:s412"]);
  });
  it("a missing pointer FAILs", () => {
    const kv = cleanKv(rows);
    kv.pointers["pptr:t:B738"] = null;
    expect(verify({ controls: sees, manifest: rows, ...kv }).pointerMismatches).toEqual(["pptr:t:B738"]);
  });
  it("a silently dropped blob FAILs, naming the blob", () => {
    const kv = cleanKv(rows);
    kv.blobsPresent.delete("photo:B738-aaaaaaaa");
    const r = verify({ controls: sees, manifest: rows, ...kv });
    expect(r.verdict).toBe("FAIL");
    expect(r.missingBlobs).toEqual(["photo:B738-aaaaaaaa"]);
  });
  it("a row that lost its blobKey or a square FAILs, naming the row", () => {
    const broken = [row("B738"), { ...row("C182"), squareKeys: { "240": "photo:C182-1aaaaaaa" } } as ManifestEntry];
    const r = verify({ controls: sees, manifest: broken, ...cleanKv(broken) });
    expect(r.verdict).toBe("FAIL");
    expect(r.rowsMissingKeys).toEqual(["type:C182"]);
  });
  it("losing sight of KV after the writes is UNTRUSTWORTHY, and names no missing keys", () => {
    const r = verify({ controls: { knownPresentReadsBack: false, knownAbsentReadsNull: true }, manifest: rows, pointers: {}, blobsPresent: new Set() });
    expect(r.verdict).toBe("UNTRUSTWORTHY");
    expect(r.pointerMismatches).toEqual([]);
    expect(r.missingBlobs).toEqual([]);
  });
});
