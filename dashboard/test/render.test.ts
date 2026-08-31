import { describe, expect, it } from "vitest";
import { fleetBody, page } from "../src/render";
import type { DeviceRow } from "../src/types";

const row = (over: Partial<DeviceRow> = {}): DeviceRow => ({
  dev: "0123456789abcdef", model: "s3-128", fw: "5",
  requests: 6200, errors: 3, cards: 14, enriches: 40, staleServed: 2,
  lastSeen: new Date().toISOString(), revoked: false, ...over,
});

const totals = { devices: 2, requests: 12400, errors: 3, cards: 14, unattributed: 0 };

describe("fleet rendering", () => {
  it("renders a device row with its numbers", () => {
    const html = fleetBody([row()], totals, 24, "");
    expect(html).toContain("0123456789abcdef");
    expect(html).toContain("6,200");
    expect(html).toContain("Revoke");
  });

  it("offers Restore, not Revoke, for a device already on the denylist", () => {
    const html = fleetBody([row({ revoked: true })], totals, 24, "");
    expect(html).toContain("Restore");
    expect(html).toContain('name="to" value="0"');
  });

  // The display name comes from the leaderboard, which any device can submit --
  // so it is attacker-controlled text arriving on an admin page.
  it("escapes a device name rather than trusting the leaderboard", () => {
    const html = fleetBody([row({ name: '<img src=x onerror=alert(1)>"' })], totals, 24, "");
    // The payload must survive only as text. Asserting on the substring alone
    // would be wrong -- "onerror=alert(1)" is still THERE, inertly, inside the
    // escaped run; what matters is that no tag was ever opened.
    expect(html).not.toContain("<img");
    expect(html).toContain("&lt;img src=x onerror=alert(1)&gt;&quot;");
  });

  it("escapes a device id, model and firmware too", () => {
    const html = fleetBody([row({ dev: "<b>x</b>", model: '"><i>', fw: "<u>" })], totals, 24, "");
    expect(html).toContain("&lt;b&gt;x&lt;/b&gt;");
    expect(html).toContain("&quot;&gt;&lt;i&gt;");
    expect(html).toContain("&lt;u&gt;");
    // And nothing broke out of the hidden input that carries the id to /revoke.
    expect(html).toContain('name="dev" value="&lt;b&gt;x&lt;/b&gt;"');
  });

  it("says something useful when there is no attributed traffic yet", () => {
    const html = fleetBody([], { ...totals, devices: 0 }, 24, "");
    expect(html).toContain("No device-attributed requests");
    expect(html).toContain("redeploy");
  });

  it("states plainly that requests are not attention", () => {
    const html = fleetBody([row()], totals, 24, "");
    expect(html).toContain("measures uptime, not attention");
  });

  it("produces a complete document with the window preserved in the nav", () => {
    const html = page({ title: "Fleet", email: "me@example.com", hours: 72, active: "/", body: "<section></section>" });
    expect(html.startsWith("<!doctype html>")).toBe(true);
    expect(html).toContain("</html>");
    expect(html).toContain('href="/ota?hours=72"');
    expect(html).toContain('aria-current="page"');
  });

  it("escapes the operator email in the header", () => {
    const html = page({ title: "Fleet", email: "<script>x</script>", hours: 24, active: "/", body: "" });
    expect(html).not.toContain("<script>x");
  });
});
