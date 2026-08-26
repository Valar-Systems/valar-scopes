/**
 * Shell-argument helpers for the ingest scripts. PURE STRING WORK, NO IMPORTS --
 * that is deliberate, so a Workers-pool vitest test can import it.
 *
 * ============================================================================
 * WHY THIS IS ITS OWN FILE, AND WHY IT HAS A TEST
 *
 * The first version of `sh` was written as
 *
 *     const sh = (p: string) => p.replace(/\\\\/g, "/");
 *
 * which reads like "replace backslashes with forward slashes" and is not. By
 * the time the regex literal is parsed, `\\\\` is the pattern for a DOUBLE
 * backslash -- so it matched nothing in a normal Windows path, returned the
 * string unchanged, and every path downstream looked correctly normalised while
 * being untouched. tar then opened nothing, and the visible failure was about
 * tar, several layers from the cause.
 *
 * That is this project's recurring shape: the broken state and the working state
 * produce the same observation. A test asserting `sh()` returns a string, or
 * that it leaves a POSIX path alone, passes against the broken version -- both
 * of those are true of a function that does nothing at all.
 *
 * So the test asserts the CHANGE: a known Windows path must come back different,
 * and must contain no backslash. That is the only assertion the do-nothing
 * version fails.
 *
 * Written with fromCharCode rather than an escape sequence because the escaping
 * is precisely what went wrong; there is no level of quoting to get right here.
 */

/** A single backslash, without writing an escape sequence. */
const BACKSLASH = String.fromCharCode(92);

/**
 * Normalise a filesystem path for use as a shell argument.
 *
 * Node's join()/tmpdir() emit backslashes on Windows, and every layer between
 * here and the invoked binary treats one as an escape -- the path arrives at tar
 * as C:\Users\... with the separators eaten. Forward slashes are accepted by the
 * Windows APIs and by every tool we invoke, so they are the portable spelling.
 * A no-op on Linux, where CI runs.
 */
export const sh = (p: string): string => p.split(BACKSLASH).join("/");

/** Quote an argument if it contains a space. */
export const q = (s: string): string => (s.includes(" ") ? `"${s}"` : s);

/** True if `p` still contains a separator no shell layer will pass through. */
export const hasBackslash = (p: string): boolean => p.includes(BACKSLASH);
