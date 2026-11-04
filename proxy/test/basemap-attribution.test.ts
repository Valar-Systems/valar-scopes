// MA1, the file half (docs/basemap-route.md A1.12): the attribution file carries every credit line, every processing
// text, SRTM's notice (year 2000, AQ1) and the OGL - Canada statement with its link. The credit and processing texts are
// taken from the BUILDER's own strings (provenance.ts), the other side of the contract, so a change to either fails it.
import { describe, expect, it } from "vitest";
import { CREDIT_BITS, processingText } from "../src/basemap/provenance";
import { renderCreditsHtml } from "../src/photolicense";
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

describe("MA2, the credits page", () => {
  // docs/basemap-deploy.md, the D3 PR's C3: the page carries every terrain credit and SRTM's notice, HTML-escaped. The
  // credits are the builder's own strings and the notice is read out of the attribution file: both the other side.
  it("MA2: /credits carries the four terrain credits and SRTM's notice, each HTML-escaped", () => {
    const html = renderCreditsHtml([]);
    const esc = (s: string) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    const notice = /"(© COPYRIGHT [^"]+)"/.exec(doc)?.[1];
    expect(notice, "the attribution file's notice line").toBeDefined();
    // CONTROL: the ETOPO1 credit holds a '>', so its escaped form differs from its raw one and this row cannot pass by
    // matching raw text
    expect(esc(CREDIT_BITS.find(([b]) => b === 4)![1])).toContain("&gt;");
    for (const [, text] of CREDIT_BITS) expect(html, text).toContain(esc(text));
    expect(html).toContain(esc(notice!));
  });
});
