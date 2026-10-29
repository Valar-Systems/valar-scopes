/// <reference types="vite/client" />
// MG0 (A1.7) and MG0b (A2.21): provenance.ts against Weatherscope's stored Bend package and its manifest, on main.
// The manifest is imported raw, so MG0's store check hashes its exact bytes. Each test is named by its case id.
import { describe, expect, it } from "vitest";
import { decode } from "../src/basemap/format";
import { type FileRef, LAYER_ORDER, layerDigest, provenanceText } from "../src/basemap/provenance";
import bend from "../../test/fixtures/wx/basemap/bend.bmpk";
import manifestText from "../../test/fixtures/wx/basemap/bend.manifest.json?raw";

const hex = (b: ArrayBuffer) => [...new Uint8Array(b)].map((x) => x.toString(16).padStart(2, "0")).join("");
const sha12 = async (s: string) => hex(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s))).slice(0, 12);

const pkg = decode(bend);
const provLines = (() => {
  const s = pkg.sections.find((x) => x.type === 7) as { text: Uint8Array } | undefined;
  if (!s) throw new Error("bend.bmpk has no PROVENANCE (MG is BLIND)");
  return new TextDecoder("utf-8", { fatal: true, ignoreBOM: false }).decode(s.text).split("\n");
})();
const value = (key: string) => {
  const hit = provLines.filter((l) => l.startsWith(`${key}: `));
  if (hit.length !== 1) throw new Error(`bend.bmpk's PROVENANCE has ${hit.length} "${key}" lines`);
  return hit[0]!.slice(key.length + 2);
};
const manifest = JSON.parse(manifestText) as { files: Array<FileRef & { layer: string }> };
const layerFiles = (l: string): FileRef[] => manifest.files.filter((f) => f.layer === l).map(({ url, sha256, vintage }) => ({ url, sha256, vintage }));

describe("MG, the golden row (the PROVENANCE half)", () => {
  it("MG0: every layer's files and sha256, and store:, recomputed from bend.manifest.json equal bend.bmpk's", async () => {
    for (const l of LAYER_ORDER) {
      expect(value(`files.${l}`), l).toBe(String(layerFiles(l).length));
      expect(value(`sha256.${l}`), l).toBe(await layerDigest(layerFiles(l)));
    }
    expect(value("store")).toBe(await sha12(manifestText));
  });

  it("MG0b: provenanceText from the manifest and bend's own counts equals bend.bmpk's PROVENANCE but for the builder line", async () => {
    const input = {
      commit: "1".repeat(40), storeId: await sha12(manifestText),
      files: Object.fromEntries(LAYER_ORDER.map((l) => [l, layerFiles(l)])),
      tiles: { levels: true, peaks: true, creditBits: 1 },
      epsilonMm: pkg.toleranceMm, dropped: {},
      collapsed: Object.fromEntries(LAYER_ORDER.map((l) => [l, Number(value(`collapsed.${l}`))])),
    };
    const mine = (await provenanceText(input)).text.split("\n");
    expect(mine.length).toBe(provLines.length);
    expect(mine.flatMap((x, i) => (x === provLines[i] ? [] : [i]))).toEqual([1]);
    expect(mine[1]).toBe(`builder: blipscope-basemap ${"1".repeat(40)}`);
    expect(provLines[1]).toMatch(/^builder: weatherscope-basemap [0-9a-f]{40}$/);
    // CONTROL: one water file's sha256 changed differs in exactly one line, sha256.water
    const bent = { ...input, files: { ...input.files, water: input.files.water!.map((f, i) => (i === 0 ? { ...f, sha256: "0".repeat(64) } : f)) } };
    const other = (await provenanceText(bent)).text.split("\n");
    const moved = other.flatMap((x, i) => (x === mine[i] ? [] : [i]));
    expect(moved.map((i) => other[i]!.split(":")[0])).toEqual(["sha256.water"]);
  });
});
