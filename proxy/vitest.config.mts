import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { defineWorkersConfig } from "@cloudflare/vitest-pool-workers/config";

// BMPK fixtures are binary (docs/basemap-route.md, MV/ME/MG). A test imports one as a Uint8Array of the file's exact
// bytes: read here, in Node, at transform time, so the bytes never pass through a text decoder. Tests only --
// wrangler.toml and the deployed Worker are untouched.
const bmpkBytes = {
  name: "bmpk-bytes",
  load(id: string) {
    if (!id.endsWith(".bmpk")) return null;
    const b64 = readFileSync(id).toString("base64");
    return `const s = atob(${JSON.stringify(b64)}); const b = new Uint8Array(s.length);\n`
      + `for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i);\nexport default b;\n`;
  },
};

// The committed proxy/photos/credits.html as its exact text, `import x from "virtual:committed-credits"`
// (docs/credits-correctness.md, Q-C1: the committed file against the render). A virtual module, because an .html import
// never reaches a plugin under vitest-pool-workers: `?raw` and a `?text` query both failed to load. Tests only.
const CREDITS_HTML = fileURLToPath(new URL("./photos/credits.html", import.meta.url));
const committedCredits = {
  name: "committed-credits",
  resolveId(source: string) {
    return source === "virtual:committed-credits" ? "\0virtual:committed-credits" : null;
  },
  load(id: string) {
    if (id !== "\0virtual:committed-credits") return null;
    return `export default ${JSON.stringify(readFileSync(CREDITS_HTML, "utf8"))};\n`;
  },
};

// The two production ingest tomls as their exact text, `import t from "virtual:wx-ingest-tomls"` (docs/wx-opuk-split.md,
// FX: no two deploy targets check the same product -- read from what deploys, not restated). Tests only.
const INGEST_TOMLS = ["wx-ingest.wrangler.toml", "wx-ingest-opuk.wrangler.toml"];
const wxIngestTomls = {
  name: "wx-ingest-tomls",
  resolveId(source: string) {
    return source === "virtual:wx-ingest-tomls" ? "\0virtual:wx-ingest-tomls" : null;
  },
  load(id: string) {
    if (id !== "\0virtual:wx-ingest-tomls") return null;
    const files = Object.fromEntries(INGEST_TOMLS.map((f) => [f, readFileSync(fileURLToPath(new URL(`./${f}`, import.meta.url)), "utf8")]));
    return `export default ${JSON.stringify(files)};\n`;
  },
};

export default defineWorkersConfig({
  plugins: [bmpkBytes, committedCredits, wxIngestTomls],
  test: {
    poolOptions: {
      workers: {
        // Tests run against the TOP-LEVEL wrangler.toml config (no rate-limit
        // bindings there -- those are staging/production-only "unsafe" bindings
        // miniflare can't emulate; the code treats them as optional).
        wrangler: { configPath: "./wrangler.toml" },
        miniflare: {
          // Secrets aren't in wrangler.toml; give tests deterministic values.
          bindings: {
            BLIP_KEYS: "test-key",
            UPSTREAM_TIMEOUT_MS: "300",
            UPSTREAM_RETRY_DELAY_MS: "10",
            // MD2 only (scripts/basemap-md2.ts): the Node half's sha256 list; empty, and MD2 skipped, in every other run
            MD2_NODE_SHAS: process.env.MD2_NODE_SHAS ?? ""
          }
        }
      }
    }
  }
});
