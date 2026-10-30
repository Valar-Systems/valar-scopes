// MA1, the file half (docs/basemap-route.md A1.12): the attribution file carries every credit line, every processing
// text, SRTM's notice (year 2000, AQ1) and the OGL - Canada statement with its link. The credit and processing texts are
// taken from the BUILDER's own strings (provenance.ts), the other side of the contract, so a change to either fails it.
import { describe, expect, it } from "vitest";
import { CREDIT_BITS, processingText } from "../src/basemap/provenance";
import doc from "../../docs/basemap-attribution.md?raw";

const NOTICE = "© COPYRIGHT 2000 BY THE UNITED STATES GOVERNMENT. NO COPYRIGHT CLAIMED UNDER TITLE 17 U.S.C.";

describe("MA1, the attribution file", () => {
  it("MA1: every credit, every processing text, SRTM's notice and the OGL statement with its link", () => {
    expect(CREDIT_BITS.length).toBe(4); // CONTROL: the builder's list is the one being read
    for (const [, text] of CREDIT_BITS) expect(doc, text).toContain(text);
    for (const [lv, pk] of [[true, false], [false, true], [true, true]] as const) expect(doc).toContain(processingText(lv, pk));
    expect(doc).toContain(NOTICE);
    expect(doc).toContain("Open Government Licence – Canada. https://open.canada.ca/en/open-government-licence-canada");
  });
});
