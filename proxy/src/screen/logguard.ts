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

/** What a console call may write: its own arguments, or one replacement line. RED STUB: passes everything. */
export function screenLogLine(args: readonly unknown[]): readonly unknown[] {
  /*P-NS4*/
  return args;
}
