import { describe, expect, it } from "vitest";
import { PathOutsidePhotos, assertPhotoPaths, planPublish, validateRows } from "../scripts/publish-plan";
import type { ManifestEntry } from "../src/photolicense";

const row = (target: string, source = "https://commons.wikimedia.org/wiki/File:A.jpg"): ManifestEntry =>
  ({
    target, kind: "type", source, author: "Someone", credit: `${target} photo`,
    license: "CC0", layer: "auto", autoPicked: false, file: `src/${target.toLowerCase()}.jpg`,
  }) as ManifestEntry;
const repick = (e: ManifestEntry, n: number) => ({ ...e, source: `https://commons.wikimedia.org/wiki/File:${e.target}-${n}.jpg` });

describe("assertPhotoPaths (A15)", () => {
  it("CONTROL: paths under proxy/photos/ pass", () => {
    expect(() => assertPhotoPaths(["proxy/photos/manifest.json", "proxy/photos/src/b738.jpg", "proxy/photos/credits.html"])).not.toThrow();
  });
  it("anything else is refused, and named", () => {
    for (const p of ["src/main.cpp", ".github/workflows/photos.yml", "proxy/src/photos.ts", "proxy/photos/../src/x.ts", "proxy\\photos\\x", "proxy/photosX/a"]) {
      expect(() => assertPhotoPaths(["proxy/photos/manifest.json", p]), p).toThrow(PathOutsidePhotos);
    }
    expect(() => assertPhotoPaths(["README.md"])).toThrow(/README\.md/);
  });
});

describe("validateRows (A17)", () => {
  it("CONTROL: well-formed rows pass", () => {
    expect(validateRows([row("B738"), row("C182")])).toEqual([]);
  });
  it("a malformed row is refused by name", () => {
    const bad = [row("B738"), { ...row("C182"), kind: "plane" }, { ...row("F22"), license: "CC BY-NC 4.0" }];
    const p = validateRows(bad);
    expect(p.map((x) => x.target)).toEqual(["C182", "F22"]);
  });
  it("a row with no usable target is still named by position", () => {
    expect(validateRows([row("B738"), { kind: "type" }])[0]!.target).toBe("(row 2)");
  });
  it("duplicates and a non-array are refused", () => {
    expect(validateRows([row("B738"), row("B738")])[0]!.errors).toContain("duplicate row");
    expect(validateRows({})[0]!.target).toBe("(manifest)");
  });
});

describe("planPublish", () => {
  const base = [row("A332"), row("B738"), row("C182")];
  it("CONTROL: no local edits is an empty publish", () => {
    const p = planPublish(base, base, base);
    expect(p).toMatchObject({ changed: [], conflicts: [], removed: [] });
  });
  it("one edit is applied onto main as it is now", () => {
    const p = planPublish(base, base, [base[0]!, repick(base[1]!, 1), base[2]!]);
    expect(p.changed).toEqual(["B738"]);
    expect(p.merged.find((e) => e.target === "B738")!.source).toContain("B738-1");
  });
  it("A5: someone else's newer row on main is kept, not overwritten by our stale copy", () => {
    const current = [repick(base[0]!, 7), base[1]!, base[2]!]; // A332 changed on main since
    const local = [base[0]!, repick(base[1]!, 1), base[2]!]; // we only edited B738
    const p = planPublish(base, current, local);
    expect(p.changed).toEqual(["B738"]);
    expect(p.conflicts).toEqual([]);
    expect(p.merged.find((e) => e.target === "A332")!.source).toContain("A332-7");
  });
  it("A6: the same row edited here and on main is a conflict, named, and not applied", () => {
    const current = [base[0]!, repick(base[1]!, 2), base[2]!];
    const local = [base[0]!, repick(base[1]!, 1), base[2]!];
    const p = planPublish(base, current, local);
    expect(p.conflicts).toEqual(["B738"]);
    expect(p.changed).toEqual([]);
    expect(p.merged.find((e) => e.target === "B738")!.source).toContain("B738-2");
  });
  it("a row main already holds exactly as edited here is neither a change nor a conflict", () => {
    const edited = [base[0]!, repick(base[1]!, 1), base[2]!];
    expect(planPublish(base, edited, edited)).toMatchObject({ changed: [], conflicts: [] });
  });
  it("editing a row someone removed from main is a conflict", () => {
    const p = planPublish(base, [base[0]!, base[2]!], [base[0]!, repick(base[1]!, 1), base[2]!]);
    expect(p.conflicts).toEqual(["B738"]);
  });
  it("a locally dropped row is reported as removed", () => {
    expect(planPublish(base, base, [base[0]!, base[2]!]).removed).toEqual(["B738"]);
  });
  it("a new row is appended", () => {
    const p = planPublish(base, base, [...base, row("K35R")]);
    expect(p.changed).toEqual(["K35R"]);
    expect(p.merged.at(-1)!.target).toBe("K35R");
  });
});
