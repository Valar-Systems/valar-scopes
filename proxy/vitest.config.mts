import { readFileSync } from "node:fs";
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

export default defineWorkersConfig({
  plugins: [bmpkBytes],
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
            UPSTREAM_RETRY_DELAY_MS: "10"
          }
        }
      }
    }
  }
});
