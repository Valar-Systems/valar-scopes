import { describe, expect, it } from "vitest";
import {
  classifyLicense,
  deriveBlobKey,
  pointerKey,
  renderCreditsHtml,
  SHAREALIKE_CLAUSE,
  SHAREALIKE_NOTE,
  validateEntry,
  type ManifestEntry,
} from "../src/photolicense";
import manifestText from "../photos/manifest.json?raw";
import committedCredits from "virtual:committed-credits";

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
  // Until docs/credits-correctness.md this rendered an UNVERSIONED "CC-BY-SA" and expected by-sa/4.0: a version no
  // record gave, which is the defect. The row's own version now gives the link; an unversioned token gets none (CV2).
  it("renders license links for CC-BY-SA with the changes-noted line, escapes text", () => {
    const html = renderCreditsHtml([
      entry({
        target: "F16",
        license: "CC BY-SA 2.0",
        changesNoted: "resized for device display",
        credit: "F-16 <fighting> & falcon",
      }),
    ]);
    expect(html).toContain("creativecommons.org/licenses/by-sa/2.0/");
    expect(html).not.toContain("creativecommons.org/licenses/by-sa/4.0/");
    expect(html).toContain("resized for device display");
    expect(html).toContain("F-16 &lt;fighting&gt; &amp; falcon"); // escaped
    expect(html).not.toContain("<fighting>");
  });

  // Scoped to the photo-credits section (ruled 2026-11-04, docs/wx-opuk.md): the page's data sources may carry a CC
  // licence link -- OPERA's line does -- but a public-domain PHOTO must not.
  const photoSection = (html: string) => {
    const at = html.indexOf("<h2>Photo credits</h2>");
    expect(at, "CONTROL: the photo-credits section is found").toBeGreaterThan(0);
    return html.slice(at);
  };

  it("courtesy-credits PD without a license link", () => {
    const html = renderCreditsHtml([entry({ target: "F22", license: "PD-USGov" })]);
    expect(html).toContain("Public domain");
    expect(photoSection(html)).not.toContain("creativecommons.org");
  });

  it("CONTROL: the same photo in the CC-licensed form trips the scoped check", () => {
    const html = renderCreditsHtml([entry({ target: "F22", license: "CC-BY-SA-4.0" })]);
    expect(photoSection(html)).toContain("creativecommons.org");
  });

  // docs/wx-server.md N1: the weather radar's attribution, worded as that doc has it. /wx serves MRMS crops, so the
  // page must say so; smoke-prod.sh searches the live page for the same bytes.
  it("carries the NOAA MRMS line exactly once", () => {
    const html = renderCreditsHtml([]);
    const line = "Weather radar derived from NOAA MRMS data. Not endorsed by NOAA.";
    expect(html.split(line).length - 1).toBe(1);
  });

  // docs/wx-opuk.md M5: OPERA's CC BY 4.0 attribution, before opuk serves anywhere, in the wording docs/uk-data.md A2.1
  // accepted. One line, no markup inside, ASCII bytes (&deg;); smoke-prod.sh searches the live page for the same bytes.
  it("carries the OPERA line exactly once", () => {
    const html = renderCreditsHtml([]);
    const line = "Weather radar outside the US: EUMETNET OPERA CIRRUS maximum-reflectivity composite, created by " +
      "Meteo France, licensed under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). Reprojected to a 0.01&deg; " +
      "grid and binned to 5 dBZ steps by Valar Systems. Not endorsed by EUMETNET.";
    expect(html.split(line).length - 1).toBe(1);
  });
});

// docs/credits-correctness.md. The version is read HERE, by the test's own parse of each row's own `license` (its
// Commons LicenseShortName), never from the renderer: the other side of the contract. S-CV-BLANKET forces the old
// blanket 4.0 / OGL v3.0 and must turn CV1, CV2, SA1 and Q-C1 red.
const MANIFEST: ManifestEntry[] = JSON.parse(manifestText);
const TOKEN = /^CC[ -](BY|BY-SA)[ -](\d\.\d)(?:[ -]([a-z]{2}))?$/i;
const OGL = /^OGL v(\d)\.0$/i;

/** What the record says: the label and the deed link, or null for a row that carries no licence link. */
function fromRecord(license: string): { label: string; url: string; sa: boolean } | null {
  const cc = TOKEN.exec(license.trim());
  if (cc) {
    const kind = cc[1]!.toUpperCase(), ver = cc[2]!, port = cc[3]?.toLowerCase();
    return {
      label: `CC ${kind} ${ver}${port ? ` ${port.toUpperCase()}` : ""}`,
      url: `https://creativecommons.org/licenses/${kind.toLowerCase()}/${ver}/${port ? `${port}/` : ""}`,
      sa: kind === "BY-SA",
    };
  }
  const ogl = OGL.exec(license.trim());
  if (ogl) {
    return { label: `Open Government Licence v${ogl[1]}.0`, url: `https://www.nationalarchives.gov.uk/doc/open-government-licence/version/${ogl[1]}/`, sa: false };
  }
  return null;
}
// A row runs from its <li> to its </li>, which may span lines: PC12's author field holds line breaks (the first run
// read one line per row and so missed PC12's link: docs/credits-correctness.md, Results).
const rowOf = (html: string, target: string) => {
  const i = html.indexOf(`<li><strong>${target}</strong>`);
  return i < 0 ? "" : html.slice(i, html.indexOf("</li>", i) + "</li>".length);
};
const licenceLinks = (row: string) =>
  [...row.matchAll(/href="([^"]*(?:creativecommons\.org\/licenses|open-government-licence)[^"]*)"/g)].map((m) => m[1]);

/** Rows checked, and rows whose label or link is not their record's (or that carry another licence's link). */
function grade(entries: ManifestEntry[]): { checked: number; wrong: number } {
  const html = renderCreditsHtml(entries);
  let checked = 0, wrong = 0;
  for (const e of entries) {
    const want = fromRecord(e.license);
    if (!want) continue;
    checked++;
    const row = rowOf(html, e.target);
    const ok = row.includes(`>${want.label}</a>`) && licenceLinks(row).length > 0 && licenceLinks(row).every((u) => u === want.url);
    if (!ok) wrong++;
  }
  return { checked, wrong };
}

describe("CV1/CV2: each image's licence version, from its own record", () => {
  it("CV1: the real manifest -- 176 licensed rows, each with its record's own version and port, 0 wrong", () => {
    expect(grade(MANIFEST)).toEqual({ checked: 176, wrong: 0 });
  });

  it("CV2: one row per form, and an unversioned token gets no link (11 rows, 0 wrong)", () => {
    const forms = ["CC BY 2.0", "CC BY 2.5", "CC BY 3.0", "CC BY 4.0", "CC BY-SA 2.0", "CC BY-SA 3.0", "CC BY-SA 3.0 au", "CC BY-SA 4.0", "OGL v1.0", "OGL v3.0"];
    const rows = forms.map((license, i) => entry({ target: `T${String(i).padStart(2, "0")}`, license, changesNoted: "resized for device display" }));
    const html = renderCreditsHtml([...rows, entry({ target: "TUNV", license: "CC-BY-SA", changesNoted: "resized for device display" })]);
    const versioned = grade(rows);
    const unversioned = rowOf(html, "TUNV");
    const unversionedOk = licenceLinks(unversioned).length === 0 && unversioned.includes("CC-BY-SA");
    expect({ checked: versioned.checked + 1, wrong: versioned.wrong + (unversionedOk ? 0 : 1) }).toEqual({ checked: 11, wrong: 0 });
  });
});

describe("SA1: the ShareAlike notice", () => {
  it("SA1: 114 of 114 BY-SA rows name their own licence after the clause; 0 others carry it; the paragraph once", () => {
    const html = renderCreditsHtml(MANIFEST);
    let sa = 0, saOk = 0, otherWithClause = 0;
    for (const e of MANIFEST) {
      const want = fromRecord(e.license), row = rowOf(html, e.target);
      if (want?.sa) {
        sa++;
        if (row.includes(`${SHAREALIKE_CLAUSE} <a href="${want.url}" rel="noopener">${want.label}</a>`)) saOk++;
      } else if (row.includes(SHAREALIKE_CLAUSE)) otherWithClause++;
    }
    expect({ sa, saOk, otherWithClause, paragraph: html.split(SHAREALIKE_NOTE).length - 1 }).toEqual({ sa: 114, saOk: 114, otherWithClause: 0, paragraph: 1 });
  });
});

describe("RD1: route data", () => {
  it("RD1: credits vradarserver/standing-data under CC0 1.0 exactly once, and adsbdb nowhere", () => {
    const html = renderCreditsHtml(MANIFEST);
    const line = '<li>Route data from <a href="https://github.com/vradarserver/standing-data" rel="noopener">vradarserver/standing-data</a>,\nreleased into the public domain under <a href="https://creativecommons.org/publicdomain/zero/1.0/" rel="noopener">CC0 1.0</a>.</li>';
    expect({ standingData: html.split(line).length - 1, adsbdb: html.toLowerCase().split("adsbdb").length - 1 }).toEqual({ standingData: 1, adsbdb: 0 });
  });
});

describe("Q-C1: the committed credits.html", () => {
  it("Q-C1: equals the render of the committed manifest", () => {
    expect(committedCredits === renderCreditsHtml(MANIFEST)).toBe(true);
  });
});
