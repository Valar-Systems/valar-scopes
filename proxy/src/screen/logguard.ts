// THE SCREEN'S CONSOLE GUARD (the logs-nosubject registration): every console line the screen's isolate writes goes
// through screenLogLine(), which passes it unchanged only if logallow.ts allows it, and otherwise replaces it.
//
// INSTALLED AT EVERY ENTRY, NEVER AT IMPORT. The Worker (dispatch, fetch) and the details object (serve) each call
// installLogGuard() on entry; it is idempotent. The DO's call is the one that matters: the enrich path RUNS in the DO,
// and in production the DO is its own isolate, so an install in the Worker alone would leave it unguarded. (The tests
// run both in one isolate, which is why P-NS3 uninstalls first and drives the DO directly.)
//
// A WRAPPER KNOWS ITSELF BY A MARK, NOT BY A FLAG. A test's vi.spyOn(console, ...) and its restore replace the
// method underneath us; a module flag would then say "installed" over a console that is not. Re-checking the mark on
// every entry re-wraps whatever is there. A double wrap is harmless: the guard passes its own output unchanged.
import { DROP_FROM, LOG_ALLOW, type Shape } from "./logallow";

type Level = "log" | "info" | "warn" | "error" | "debug";
type LogFn = (...args: unknown[]) => void;
const LEVELS: readonly Level[] = ["log", "info", "warn", "error", "debug"];
const MARK = Symbol.for("blipscope-screen:logguard");
const ORIG = Symbol.for("blipscope-screen:logguard:orig");
type Wrapped = LogFn & { [MARK]?: true; [ORIG]?: LogFn };

let bypass = false;
/** TEST SEAM ONLY (P-NS3a, CONTROL A): pass every line through raw, to prove the fixtures reach the subject lines. */
export function __bypassLogGuard(on: boolean): void {
  bypass = on;
}
/** TEST SEAM ONLY (P-NS3, P-NS5): put the console back as it was, so the next entry's install is the one tested. */
export function __uninstallLogGuard(): void {
  for (const level of LEVELS) {
    let f = console[level] as Wrapped;
    while (f[MARK] && f[ORIG]) f = f[ORIG] as Wrapped;
    console[level] = f;
  }
}

export function installLogGuard(): void {
  for (const level of LEVELS) {
    const current = console[level] as Wrapped;
    if (current[MARK]) continue;
    const wrapped: Wrapped = (...args: unknown[]) => {
      current.apply(console, (bypass ? args : screenLogLine(args)) as unknown[]);
    };
    wrapped[MARK] = true;
    wrapped[ORIG] = current;
    console[level] = wrapped;
  }
}

/** What a console call may write: its own arguments, unchanged, or ONE replacement line naming what was dropped. */
export function screenLogLine(args: readonly unknown[]): readonly unknown[] {
  const from = dropFrom(args);
  return from === null ? args : [JSON.stringify({ evt: "screen_log_dropped", from })];
}

/** A 6-hex run, either case, "~" or not: a string VALUE holding one drops the line, even under an allowed key. Strings
 *  only: a number such as hold's 600000 ms is six hex digits and is not an address. */
const HEXISH = /~?[0-9a-f]{6}/i;

/** null: the line passes unchanged. Otherwise the replacement's `from`: the evt when logallow.ts names it, "text" for a
 *  line that is not JSON, "other" for anything else. */
function dropFrom(args: readonly unknown[]): string | null {
  const first = args[0];
  if (typeof first !== "string") return "other";
  let o: unknown;
  try {
    o = JSON.parse(first);
  } catch {
    return "text";
  }
  if (!o || typeof o !== "object" || Array.isArray(o)) return "other";
  const rec = o as Record<string, unknown>;
  const from = typeof rec.evt === "string" && DROP_FROM.has(rec.evt) ? rec.evt : "other";
  if (args.length !== 1) return from;
  /*P-NS9*/
  if (JSON.stringify(rec) !== first) return from; // the TEXT is what is written: it must be exactly the object checked
  const shapes = typeof rec.evt === "string" && Object.hasOwn(LOG_ALLOW, rec.evt) ? LOG_ALLOW[rec.evt] : undefined;
  if (!shapes) return from;
  for (const [k, v] of Object.entries(rec)) {
    if (k === "evt") continue;
    if (!Object.hasOwn(shapes, k) || !fits(v, shapes[k])) return from;
    /*P-NS3belt*/
    if (typeof v === "string" && HEXISH.test(v)) return from;
  }
  /*P-NS4pass*/
  return null;
}

function fits(v: unknown, s: Shape): boolean {
  if (s === "number") return typeof v === "number" && Number.isFinite(v);
  if (s === "boolean") return typeof v === "boolean";
  if (s instanceof RegExp) return typeof v === "string" && s.test(v);
  return typeof v === "string" && s.has(v);
}
