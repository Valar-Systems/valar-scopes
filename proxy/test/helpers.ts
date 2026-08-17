import { createExecutionContext, env, waitOnExecutionContext } from "cloudflare:test";
import worker from "../src/index";
import type { Env } from "../src/types";

// Per-device keys are the only auth path (the shared BLIP_KEYS list was removed
// 2026-08-13), so every authenticated test request now carries BOTH an
// X-Blip-Device id and the key derived for THAT id.
//
// TEST_KEY is a real HMAC-SHA256(TEST_DEVICE_SECRET, TEST_DEVICE) hex digest,
// not a placeholder string, and it is asserted against a fresh derivation in
// deviceauth.test.ts. That matters: if it were hand-written and drifted from the
// derivation, every suite would 401 at once and the cause would read as "auth is
// broken" rather than "the fixture is stale".
export const TEST_DEVICE = "a1b2c3d4e5f60718";
export const TEST_DEVICE_SECRET = "test-device-secret";
export const TEST_KEY = "19dbb619f3db8fbe90a97e3e5ac2d9adf435f8beb156b8b67efb7dc612b06a55";

export function testEnv(overrides: Partial<Env> = {}): Env {
  return { ...env, DEVICE_KEY_SECRET: TEST_DEVICE_SECRET, ...overrides };
}

// The credential pair, for tests that build their own Request (POSTs, redirect
// probes) instead of going through apiRequest. Spread it rather than writing
// "X-Blip-Key": TEST_KEY by hand: a key without its device id is now a 401, and
// that half-pair is the easiest thing in the world to leave behind.
export const AUTH_HEADERS = { "X-Blip-Key": TEST_KEY, "X-Blip-Device": TEST_DEVICE } as const;

export function apiRequest(path: string, headers: Record<string, string> = {}): Request {
  return new Request(`https://proxy.test${path}`, {
    headers: { ...AUTH_HEADERS, ...headers },
  });
}

// Run one request through the worker, waiting for background work (SWR
// revalidations) to finish so assertions see the final state.
export async function call(request: Request, overrides: Partial<Env> = {}): Promise<Response> {
  const ctx = createExecutionContext();
  const res = await worker.fetch(
    request as unknown as Parameters<typeof worker.fetch>[0],
    testEnv(overrides),
    ctx,
  );
  await waitOnExecutionContext(ctx);
  return res;
}

// A full readsb-style aircraft entry; override to taste.
export function makeAc(overrides: Record<string, unknown> = {}): Record<string, unknown> {
  return {
    hex: "4b1817",
    flight: "SWR123 ",
    lat: 47.4,
    lon: 8.55,
    alt_baro: 37000,
    gs: 451.3,
    track: 231.4,
    baro_rate: -704,
    category: "A3",
    seen_pos: 2.1,
    r: "HB-JMB",
    t: "A343",
    ...overrides,
  };
}

// Fixture picture timestamp. MUST be relative to the real clock: the chain now
// treats a feed whose `now` is older than BLIPS_FEED_MAX_AGE_MS as degraded and
// walks on to the next feed (see fetchPointChain). A hardcoded past date would
// make every fixture look years stale, so the whole suite would silently exercise
// the degraded path instead of the normal one.
export const FIXTURE_NOW_MS = Date.now();

export function pointBody(ac: unknown[], nowMs: number = FIXTURE_NOW_MS): string {
  return JSON.stringify({ ac, msg: "No error", now: nowMs, total: ac.length, ctime: nowMs });
}

export function hexBody(ac: unknown[], nowMs: number = FIXTURE_NOW_MS): string {
  return JSON.stringify({ ac, msg: "No error", now: nowMs, total: ac.length });
}
