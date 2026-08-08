import { describe, expect, it } from "vitest";
import { call } from "./helpers";

// /healthz carries the commit the running bundle was built from.
//
// The question "what commit is production actually running?" used to be answered
// by correlating deploy timestamps against git log and assuming the working tree
// had been clean at the time -- an assumption that cannot be checked afterwards,
// and which `wrangler deploy` bundling the working tree makes genuinely unsafe.
// scripts/deploy.sh substitutes the real SHA at deploy time so the running Worker
// can simply be asked.
describe("build stamp", () => {
  it("reports a commit on /healthz", async () => {
    const res = await call(new Request("https://proxy.test/healthz"));
    expect(res.status).toBe(200);
    const body = (await res.json()) as { ok: boolean; commit: string };
    expect(body.ok).toBe(true);
    expect(typeof body.commit).toBe("string");
    expect(body.commit.length).toBeGreaterThan(0);
  });

  it('is "dev" under test, which is what makes it meaningful in production', () => {
    // The default comes from the `define` block in wrangler.toml. A DEPLOYED
    // Worker reporting "dev" did not come from scripts/deploy.sh -- someone ran
    // `wrangler deploy` by hand, bypassing the dirty-tree and unpushed-commit
    // guards. smoke-prod.sh asserts production never says "dev" for exactly that
    // reason, and this test pins the sentinel both sides depend on.
    expect(BUILD_COMMIT).toBe("dev");
  });
});

declare const BUILD_COMMIT: string;
