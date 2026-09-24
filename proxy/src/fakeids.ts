import { FAKE_DEVICE_IDS } from "./fakeids.generated";
import type { Env } from "./types";

// A device id from the repo's fake-id allowlist (scripts/device-id-allowlist.txt)
// is an EXAMPLE, never a device: it is on that list precisely so it can appear in
// fixtures and docs. In production such an id is a probe or a test aimed at the
// wrong environment, and it must not enrol or be served -- on 2026-08-28..31
// beefbeefbeefbeef enrolled 195 times and made requests with no model or fw.
//
// PRODUCTION ONLY. REFUSE_FAKE_DEVICE_IDS is set in [env.production.vars] and
// nowhere else, so staging -- where tests and probes belong -- and the test
// runner are exempt by construction, not by a branch someone must remember.
//
// A 403, not the 401 a bad key gets: the credential may be perfectly valid (it
// can be derived for any id), and the answer is "this identity is not allowed
// here", which is what 403 means.
export function refusesFakeId(env: Env, deviceId: string): boolean {
  return env.REFUSE_FAKE_DEVICE_IDS === "true" && FAKE_DEVICE_IDS.has(deviceId.trim().toLowerCase());
}
