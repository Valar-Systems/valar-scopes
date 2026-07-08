import { createExecutionContext, env, waitOnExecutionContext } from "cloudflare:test";
import worker from "../src/index";
import type { Env } from "../src/types";

export const TEST_KEY = "test-key";

export function testEnv(overrides: Partial<Env> = {}): Env {
  return { ...env, BLIP_KEYS: TEST_KEY, ...overrides };
}

export function apiRequest(path: string, headers: Record<string, string> = {}): Request {
  return new Request(`https://proxy.test${path}`, {
    headers: { "X-Blip-Key": TEST_KEY, ...headers },
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

export const FIXTURE_NOW_MS = 1751970245000;

export function pointBody(ac: unknown[], nowMs: number = FIXTURE_NOW_MS): string {
  return JSON.stringify({ ac, msg: "No error", now: nowMs, total: ac.length, ctime: nowMs });
}

export function hexBody(ac: unknown[], nowMs: number = FIXTURE_NOW_MS): string {
  return JSON.stringify({ ac, msg: "No error", now: nowMs, total: ac.length });
}
