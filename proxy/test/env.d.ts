import type { Env } from "../src/types";

declare module "cloudflare:test" {
  // Type `env` from cloudflare:test as our worker Env.
  interface ProvidedEnv extends Env {}
}
