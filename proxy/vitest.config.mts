import { defineWorkersConfig } from "@cloudflare/vitest-pool-workers/config";

export default defineWorkersConfig({
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
