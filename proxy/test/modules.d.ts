// Ambient module declarations for the tests. A file of its own, with no import: in a file that imports
// (env.d.ts), `declare module "*..."` is an augmentation, and the wildcard would not apply.
// Vite's ?raw import: a repo file as text. Used where a test must take its input from the OTHER side of
// a contract (test/wx-crop.test.ts reads the radar's projection constants out of the firmware source).
declare module "*?raw" {
  const text: string;
  export default text;
}

// wrangler's default module rule imports .txt as a string (a Text module).
declare module "*.txt" {
  const text: string;
  export default text;
}

// wrangler's default module rule imports .bin as an ArrayBuffer (a Data module): the opuk fixtures' byte ranges.
declare module "*.bin" {
  const data: ArrayBuffer;
  export default data;
}

// vitest.config.mts's bmpk-bytes plugin imports a .bmpk fixture as its exact bytes.
declare module "*.bmpk" {
  const bytes: Uint8Array;
  export default bytes;
}

// Vite's ?raw import of a Python source (ME1b reads synth.py's own data lines).
declare module "*.py?raw" {
  const text: string;
  export default text;
}

// vitest.config.mts's committed-credits plugin: proxy/photos/credits.html as its exact text (Q-C1).
declare module "virtual:committed-credits" {
  const text: string;
  export default text;
}

// vitest.config.mts's wx-ingest-tomls plugin: the two production ingest tomls as their exact text, by file name
// (docs/wx-opuk-split.md, FX).
declare module "virtual:wx-ingest-tomls" {
  const files: Record<string, string>;
  export default files;
}

// vitest.config.mts's opera-fixture-py plugin: tools/wx/opera_fixture.py as its exact text
// (docs/wx-opera-backup-fallback.md, UA3).
declare module "virtual:opera-fixture-py" {
  const text: string;
  export default text;
}
