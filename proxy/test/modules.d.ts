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
