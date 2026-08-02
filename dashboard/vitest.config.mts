import { defineWorkersConfig } from "@cloudflare/vitest-pool-workers/config";

export default defineWorkersConfig({
  test: {
    poolOptions: {
      workers: {
        wrangler: { configPath: "./wrangler.toml" },
        miniflare: {
          // Secrets and Access config aren't in wrangler.toml; tests set their
          // own per-case values on top of these deterministic defaults.
          bindings: {
            ACCESS_TEAM_DOMAIN: "test.cloudflareaccess.com",
            ACCESS_AUD: "test-aud",
            CF_ACCOUNT_ID: "test-account",
            CF_API_TOKEN: "test-token"
          }
        }
      }
    }
  }
});
