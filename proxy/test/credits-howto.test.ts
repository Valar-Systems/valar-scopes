import { describe, expect, it } from "vitest";
import { handleCredits } from "../src/photos";

// The owner's approved how-to-ask text (his note, 2026-11-25), transcribed byte-exact into this test independently of
// the source constant. The RENDERED /credits response must carry it exactly once: a check of the page, not of a string.
const APPROVED =
  "Asking us not to show an aircraft. If you own or operate an aircraft and would rather it not appear on our website, email support@valarsystems.com with its registration. We add it to our block list, and it is then removed from the website entirely. Aircraft in the FAA's LADD or PIA programs already appear by type only, with no registration or operator. A block does not hide an aircraft on Blipscope devices that people own: those devices show what the community networks report, and a device on its own receiver shows whatever that receiver hears.";

function count(hay: string, needle: string): number {
  let n = 0;
  for (let i = hay.indexOf(needle); i !== -1; i = hay.indexOf(needle, i + needle.length)) n++;
  return n;
}

function envWith(entries: unknown): Parameters<typeof handleCredits>[0] {
  return { ENRICH_KV: { get: async () => entries } } as unknown as Parameters<typeof handleCredits>[0];
}

describe("/credits: the owner's how-to-ask paragraph, read from the rendered response", () => {
  it("(a) appears exactly once with no manifest", async () => {
    const body = await (await handleCredits(envWith(null))).text();
    expect(count(body, APPROVED)).toBe(1);
  });

  it("(b) appears exactly once with a one-row manifest", async () => {
    const row = {
      target: "A320", kind: "type", source: "https://commons.wikimedia.org/wiki/File:Example.jpg", author: "Example",
      credit: "Example credit", license: "CC BY 4.0", layer: "auto", autoPicked: true,
    };
    const body = await (await handleCredits(envWith([row]))).text();
    expect(count(body, APPROVED)).toBe(1);
  });

  it("CONTROL: the counter finds a known line of the page exactly once", async () => {
    const body = await (await handleCredits(envWith(null))).text();
    expect(count(body, "<h1>Blipscope credits</h1>")).toBe(1);
  });
});
