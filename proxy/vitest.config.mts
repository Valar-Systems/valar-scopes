import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { defineWorkersConfig } from "@cloudflare/vitest-pool-workers/config";

// Wrangler's debug log (docs/wrangler-run.md). vitest-pool-workers loads wrangler's config to build miniflare. With no
// WRANGLER_LOG_PATH, wrangler appends to ~/.wrangler/logs, so three test runs on 2026-11-13 left files there. Point it
// at a temp dir made for THIS run and removed at exit, as scripts/wrangler-run.mjs does for every wrangler call. A
// caller's own WRANGLER_LOG_PATH is respected.
if (!process.env.WRANGLER_LOG_PATH) {
  const wranglerLogDir = mkdtempSync(join(tmpdir(), "vitest-wrangler-log-"));
  process.env.WRANGLER_LOG_PATH = join(wranglerLogDir, "wrangler.log");
  process.on("exit", () => {
    try { rmSync(wranglerLogDir, { recursive: true, force: true }); } catch { /* best effort: the OS clears tmp */ }
  });
}

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

// The production ingest tomls (cref's, and the OPERA products' since the code stage) as their exact text, `import t from "virtual:wx-ingest-tomls"` (docs/wx-opuk-split.md,
// FX: no two deploy targets check the same product -- read from what deploys, not restated). Tests only.
const INGEST_TOMLS = ["wx-ingest.wrangler.toml", "wx-ingest-opera.wrangler.toml"];
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
            // K1 (docs/basemap-name-key.md): a synthetic name key, so A1's rows run as they do with a real one.
            BASEMAP_NAME_KEY: "ab".repeat(32),
            // MD2 only (scripts/basemap-md2.ts): the Node half's sha256 list; empty, and MD2 skipped, in every other run
            MD2_NODE_SHAS: process.env.MD2_NODE_SHAS ?? ""
          }
        }
      }
    }
  }
});
