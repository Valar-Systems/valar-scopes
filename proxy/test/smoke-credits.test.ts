// The smoke's checks on the basemap's credits (scripts/smoke-prod.sh; docs/basemap-deploy.md, Q-D3b). The smoke asks
// production's /credits for each needle; this reads the smoke script as TEXT and the page from its own template, so a
// needle typed in the smoke cannot drift from what the page says. SC3 is the control that SC2 can fail.
import { describe, expect, it } from "vitest";
import { renderCreditsHtml } from "../src/photolicense";
import smoke from "../scripts/smoke-prod.sh?raw";

const CHECK = /^contains "(\/credits carries the basemap's [^"]+)" (\d{3}) "([^"]+)" "([^"]+)"$/;
const checks = smoke
  .split("\n")
  .map((l) => l.replace(/\r$/, ""))
  .map((l) => CHECK.exec(l))
  .filter((m): m is RegExpExecArray => m !== null)
  .map((m) => ({ label: m[1]!, status: m[2]!, needle: m[3]!, url: m[4]! }));
const page = renderCreditsHtml([]);

describe("SC: the smoke's basemap credits checks", () => {
  it("SC1: the smoke holds exactly five basemap credits checks", () => {
    expect(checks.map((c) => c.label)).toHaveLength(5);
  });

  it("SC2: every needle appears in the credits page as rendered", () => {
    for (const c of checks) expect(page, c.label).toContain(c.needle);
  });

  it("SC3: CONTROL: a near-miss needle (the terrain line at zoom 10) is absent, so SC2 can fail", () => {
    const terrain = checks.find((c) => c.label.endsWith("terrain line"));
    expect(terrain?.needle).toContain("zoom 9");
    expect(page).not.toContain(terrain!.needle.replace("zoom 9", "zoom 10"));
  });

  it("SC4: every check asks $BASE/credits for HTTP 200", () => {
    for (const c of checks) expect([c.status, c.url], c.label).toEqual(["200", "$BASE/credits"]);
  });
});
