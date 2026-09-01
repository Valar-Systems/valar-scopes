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

  it('is "UNSTAMPED" under test, which is what makes it meaningful in production', () => {
    // The default comes from the `define` block in wrangler.toml. A DEPLOYED
    // Worker reporting the sentinel did not come from scripts/deploy.sh --
    // someone ran `wrangler deploy` by hand, bypassing the dirty-tree and
    // unpushed-commit guards. smoke-prod.sh asserts production never reports it,
    // and this test pins the sentinel both sides depend on.
    //
    // UNSTAMPED, NOT "dev" (#288). The old sentinel was a plausible value that
    // /healthz reported without complaint; this one cannot be mistaken for a
    // commit by a human or by a check.
    //
    // THIS TEST WAS RED ON main FROM #288 UNTIL 2026-08-31 and nobody noticed,
    // which is the argument for fixing it the hour it was found rather than
    // filing it. A suite with one known-red test is a suite people learn to skim,
    // and the next real failure arrives looking exactly like the one they have
    // been ignoring.
    expect(BUILD_COMMIT).toBe("UNSTAMPED");
  });
});

declare const BUILD_COMMIT: string;
