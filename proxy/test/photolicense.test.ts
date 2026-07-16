import { describe, expect, it } from "vitest";
import {
  classifyLicense,
  deriveBlobKey,
  pointerKey,
  renderCreditsHtml,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";

function entry(overrides: Partial<ManifestEntry> = {}): ManifestEntry {
  return {
    target: "A320",
    kind: "type",
    source: "https://commons.wikimedia.org/wiki/File:Example.jpg",
    author: "Jane Spotter",
    credit: "Airbus A320 by Jane Spotter",
    license: "CC-BY-4.0",
    layer: "auto",
    autoPicked: true,
    ...overrides,
  };
}

describe("classifyLicense", () => {
  it("maps clean licenses regardless of version/case", () => {
    expect(classifyLicense("PD-USGov")).toBe("PD-USGov");
    expect(classifyLicense("pd-usgov-military")).toBe("PD-USGov");
    expect(classifyLicense("CC0")).toBe("PD-USGov");
    expect(classifyLicense("CC-BY-4.0")).toBe("CC-BY");
    expect(classifyLicense("cc by sa 3.0")).toBe("CC-BY-SA");
    expect(classifyLicense("OGL-3.0")).toBe("OGL");
    expect(classifyLicense("own")).toBe("own");
  });

  it("rejects the non-free CC clauses even when combined with BY/SA", () => {
    expect(classifyLicense("CC-BY-NC")).toBe("reject-nc");
    expect(classifyLicense("CC-BY-NC-SA")).toBe("reject-nc");
    expect(classifyLicense("CC-BY-ND")).toBe("reject-nd");
  });

  it("returns unknown for unrecognized tokens", () => {
    expect(classifyLicense("all-rights-reserved")).toBe("unknown");
    expect(classifyLicense("")).toBe("unknown");
  });
});

describe("validateEntry license gate", () => {
  it("accepts a clean CC-BY auto entry", () => {
    expect(validateEntry(entry()).ok).toBe(true);
  });

  it("rejects NC / ND in both layers", () => {
    for (const layer of ["mil-tier", "auto"] as const) {
      expect(validateEntry(entry({ layer, license: "CC-BY-NC" })).ok).toBe(false);
      expect(validateEntry(entry({ layer, license: "CC-BY-ND" })).ok).toBe(false);
    }
  });

  it("rejects CC-BY-SA in mil-tier but accepts it in auto with a changes-noted line", () => {
    expect(validateEntry(entry({ layer: "mil-tier", license: "CC-BY-SA" })).ok).toBe(false);

    const noChanges = validateEntry(entry({ layer: "auto", license: "CC-BY-SA" }));
    expect(noChanges.ok).toBe(false);
    expect(noChanges.errors.join(" ")).toContain("changesNoted");

    const withChanges = validateEntry(
      entry({ layer: "auto", license: "CC-BY-SA", changesNoted: "resized for device display" }),
    );
    expect(withChanges.ok).toBe(true);
  });

  it("accepts PD/CC-BY/OGL/own in mil-tier", () => {
    for (const license of ["PD-USGov", "CC-BY", "OGL", "own"]) {
      expect(validateEntry(entry({ layer: "mil-tier", license })).ok).toBe(true);
    }
  });

  it("rejects rows missing required fields", () => {
    expect(validateEntry(entry({ author: "" })).ok).toBe(false);
    expect(validateEntry(entry({ credit: "  " })).ok).toBe(false);
    expect(validateEntry(entry({ source: "" })).ok).toBe(false);
  });

  it("enforces target shape per kind", () => {
    expect(validateEntry(entry({ kind: "type", target: "toolong" })).ok).toBe(false);
    expect(validateEntry(entry({ kind: "hex", target: "4b1817" })).ok).toBe(true);
    expect(validateEntry(entry({ kind: "hex", target: "ZZZ" })).ok).toBe(false);
  });
});

describe("deriveBlobKey / pointerKey", () => {
  it("is content-addressed and stable, and changes with content", async () => {
    const a = await deriveBlobKey("A320", new TextEncoder().encode("jpeg-bytes-A"));
    const a2 = await deriveBlobKey("A320", new TextEncoder().encode("jpeg-bytes-A"));
    const b = await deriveBlobKey("A320", new TextEncoder().encode("jpeg-bytes-B"));
    expect(a).toBe(a2);
    expect(a).not.toBe(b);
    expect(a).toMatch(/^photo:A320-[0-9a-f]{8}$/);
  });

  it("keys pointers by kind", () => {
    expect(pointerKey("hex", "4b1817")).toBe("pptr:h:4b1817");
    expect(pointerKey("type", "A320")).toBe("pptr:t:A320");
  });
});

describe("renderCreditsHtml", () => {
  it("renders license links for CC-BY-SA with the changes-noted line, escapes text", () => {
    const html = renderCreditsHtml([
      entry({
        target: "F16",
        license: "CC-BY-SA",
        changesNoted: "resized for device display",
        credit: "F-16 <fighting> & falcon",
      }),
    ]);
    expect(html).toContain("creativecommons.org/licenses/by-sa/4.0/");
    expect(html).toContain("resized for device display");
    expect(html).toContain("F-16 &lt;fighting&gt; &amp; falcon"); // escaped
    expect(html).not.toContain("<fighting>");
  });

  it("courtesy-credits PD without a license link", () => {
    const html = renderCreditsHtml([entry({ target: "F22", license: "PD-USGov" })]);
    expect(html).toContain("Public domain");
    expect(html).not.toContain("creativecommons.org");
  });
});
