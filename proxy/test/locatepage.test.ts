import { describe, it, expect } from "vitest";
import { call } from "./helpers";
import { formatCoord, isAllowedOrigin, locateHtml, locateScript, LOCATE_COPY } from "../src/locatepage";

/* ===========================================================================
 * "USE MY LOCATION" -- the helper page (docs/RELEASE-v15.md §5).
 *
 * The weaker half of the privacy proof lives here: the route's CSP, the origin
 * rule, and the page's script EXECUTED against a stub that counts every way a
 * request could leave (fetch, XHR, sendBeacon, Image). The stronger half is
 * scripts/check-locate-page.mjs, which watches the actual wire in Chromium.
 *
 * The sentinel position is obviously not a place: 11.1111111, -22.2222222.
 * ======================================================================== */

const SENTINEL = { latitude: 11.1111111, longitude: -22.2222222, accuracy: 15 };

interface Run {
  posted: Array<{ data: any; target: string }>;
  outbound: string[];          // every fetch/XHR/beacon/image attempt
  geoCalls: number;
  msg: () => string;
}

/** Execute the page's inline script with ?o=<origin>, click "Share", resolve the stub fix. */
function runPage(o: string, opts: { fail?: number } = {}, body: string = locateScript()): Run {
  const run: Run = { posted: [], outbound: [], geoCalls: 0, msg: () => els.msg.textContent };
  const listeners: Record<string, () => void> = {};
  const els: Record<string, any> = {
    msg: { textContent: "", className: "" },
    go: { disabled: false, addEventListener: (_: string, f: () => void) => { listeners.click = f; } },
  };
  const g: any = {
    location: { search: "?o=" + encodeURIComponent(o) },
    URLSearchParams,
    document: { getElementById: (id: string) => els[id] },
    window: {
      opener: { postMessage: (data: any, target: string) => run.posted.push({ data, target }) },
      close: () => {},
    },
    navigator: {
      geolocation: {
        getCurrentPosition: (ok: (p: any) => void, err: (e: any) => void) => {
          run.geoCalls++;
          if (opts.fail) err({ code: opts.fail }); else ok({ coords: SENTINEL });
        },
      },
      sendBeacon: (u: string) => { run.outbound.push("beacon " + u); return true; },
    },
    fetch: (u: string) => { run.outbound.push("fetch " + u); return Promise.resolve(); },
    XMLHttpRequest: function () { run.outbound.push("xhr"); return { open() {}, send() {} }; },
    Image: function () { const o: any = {}; Object.defineProperty(o, "src", { set: (v: string) => run.outbound.push("img " + v) }); return o; },
    setTimeout: (f: () => void) => f(),
    Math,
    JSON,
  };
  g.window.navigator = g.navigator;
  // eslint-disable-next-line no-new-func
  new Function(...Object.keys(g), body)(...Object.values(g));
  if (listeners.click) listeners.click();
  return run;
}

describe("/blipscope/locate route", () => {
  it("serves 200 with a CSP that blocks every subresource and pins the inline script and style", async () => {
    const res = await call(new Request("https://scopes.valarsystems.com/blipscope/locate?o=http%3A%2F%2Fblipscope.local"));
    expect(res.status).toBe(200);
    const csp = res.headers.get("Content-Security-Policy") ?? "";
    expect(csp).toContain("default-src 'none'");
    expect(csp).toContain("form-action 'none'");
    expect(csp).toContain("base-uri 'none'");
    const html = await res.text();
    const script = html.match(/<script>([\s\S]*)<\/script>/)![1];
    const style = html.match(/<style>([\s\S]*)<\/style>/)![1];
    const h = async (s: string) => btoa(String.fromCharCode(...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s)))));
    expect(csp).toContain(`script-src 'sha256-${await h(script)}'`);
    expect(csp).toContain(`style-src 'sha256-${await h(style)}'`);
    expect(script).toBe(locateScript());                 // the hash pins the script the tests ran
  });

  it("refuses anything but GET (the global method gate answers 405)", async () => {
    const res = await call(new Request("https://scopes.valarsystems.com/blipscope/locate", { method: "POST", body: "x" }));
    expect(res.status).toBe(405);
  });
});

describe("origin rule", () => {
  it.each(["http://blipscope.local", "http://radar-desk.local", "http://192.168.4.1", "http://10.0.0.5", "http://172.16.0.9"])(
    "accepts %s", (o) => expect(isAllowedOrigin(o)).toBe(true));
  it.each(["https://evil.example", "http://blipscope.local.evil.example", "http://8.8.8.8", "https://blipscope.local",
           "http://172.32.0.1", "http://192.168.4.1:8080", "http://blipscope.local/", ""])(
    "refuses %s", (o) => expect(isAllowedOrigin(o)).toBe(false));
});

describe("formatter", () => {
  it("4 decimals, never -0.0000", () => {
    expect(formatCoord(44.05824)).toBe("44.0582");
    expect(formatCoord(-0.00004)).toBe("0.0000");
    expect(formatCoord(-121.31534)).toBe("-121.3153");
    expect(formatCoord(90)).toBe("90.0000");
  });
});

describe("the page script, executed", () => {
  it("posts the position ONCE, to the given origin only, and sends no request", () => {
    const r = runPage("http://blipscope.local");
    expect(r.geoCalls).toBe(1);
    expect(r.posted).toHaveLength(1);
    expect(r.posted[0].target).toBe("http://blipscope.local");
    expect(r.posted[0].data).toEqual({ type: "blipscope-location", lat: "11.1111", lon: "-22.2222", acc: 15 });
    expect(r.outbound).toEqual([]);                      // the whole list, not a substring
  });

  it("CONTROL: the stub does see a request when one is made", () => {
    const leaky = locateScript().replace("say(COPY.sent,false);", "say(COPY.sent,false);navigator.sendBeacon('/x',1);");
    expect(leaky).not.toBe(locateScript());            // the plant applied
    const r = runPage("http://blipscope.local", {}, leaky);
    expect(r.outbound).toEqual(["beacon /x"]);
  });

  it("a foreign origin gets nothing: no geolocation call, no message", () => {
    const r = runPage("https://evil.example");
    expect(r.geoCalls).toBe(0);
    expect(r.posted).toEqual([]);
    expect(r.msg()).toBe(LOCATE_COPY.badOrigin);
  });

  it.each([[1, LOCATE_COPY.denied], [2, LOCATE_COPY.unavailable], [3, LOCATE_COPY.timeout]])(
    "failure code %i shows its sentence and sends no position", (code, text) => {
      const r = runPage("http://192.168.4.1", { fail: code as number });
      expect(r.msg()).toBe(text);
      expect(r.posted.map((p) => p.data.type)).toEqual(["blipscope-location-error"]);
      expect(r.outbound).toEqual([]);
    });
});

describe("copy", () => {
  it("the page carries the ask and every failure sentence", () => {
    const html = locateHtml();
    expect(html).toContain(LOCATE_COPY.ask);
    for (const k of ["denied", "unavailable", "timeout", "badOrigin"] as const)
      expect(locateScript()).toContain(JSON.stringify(LOCATE_COPY[k]).slice(1, -1));
  });
});
