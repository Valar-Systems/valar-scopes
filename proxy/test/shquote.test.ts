import { describe, expect, it } from "vitest";
import { hasBackslash, q, sh } from "../scripts/shquote";

/**
 * THE NORMALISER MUST BE PROVED TO CHANGE SOMETHING.
 *
 * The bug this file exists for was a path normaliser that did nothing:
 * `p.replace(/\\/g, "/")` is the pattern for a DOUBLE backslash, so it never
 * matched a real Windows path and returned it untouched. Every path downstream
 * then looked correctly normalised while being unmodified, and the visible
 * failure was tar refusing to open a file -- layers away from the cause.
 *
 * The trap is that the obvious tests all pass against that broken version:
 * "returns a string", "leaves a POSIX path alone", "does not throw". Each is
 * equally true of a function with an empty body. So every assertion below that
 * matters is an assertion about a DIFFERENCE.
 *
 * Backslashes are built with fromCharCode rather than written as escapes,
 * because getting the escaping wrong is the entire defect and a test that
 * repeats the mistake would agree with the bug.
 */
const B = String.fromCharCode(92);

/** The normaliser exactly as it shipped broken, rebuilt from characters. */
const brokenSh = (p: string) => p.replace(new RegExp(B + B + B + B, "g"), "/");

/** A real path off this project: what tmpdir()+join() hands us on Windows. */
const WINDOWS_PATH = "C:" + B + "Users" + B + "DANIEL~1" + B + "AppData" + B + "Local" +
  B + "Temp" + B + "routes-a1b2c3" + B + "sd.tar.gz";

describe("sh() -- shell path normaliser", () => {
  // ---- NEGATIVE CONTROL, FIRST ------------------------------------------
  // Before believing anything this file says about sh(), prove the fixture can
  // expose the defect. If the broken version were to "pass" here, these tests
  // would not be measuring what they claim to measure.
  // The title says "double-backslash form" in words on purpose. Spelling the
  // regex literal here would need eight backslashes in the source to render
  // four on screen -- and mis-levelling that escape is the defect under test,
  // so the one place it must not appear is the label describing it.
  it("NEGATIVE CONTROL: the double-backslash form leaves a Windows path untouched", () => {
    expect(brokenSh(WINDOWS_PATH)).toBe(WINDOWS_PATH);
    expect(hasBackslash(brokenSh(WINDOWS_PATH))).toBe(true);
  });

  // ---- the assertion the broken version fails ---------------------------
  it("CHANGES a known Windows path", () => {
    const out = sh(WINDOWS_PATH);
    expect(out).not.toBe(WINDOWS_PATH);
    expect(hasBackslash(out)).toBe(false);
    expect(out).toBe("C:/Users/DANIEL~1/AppData/Local/Temp/routes-a1b2c3/sd.tar.gz");
  });

  it("removes EVERY separator, not just the first", () => {
    // A .replace() without the /g flag is the other silent half-fix: it
    // normalises the drive letter and leaves the rest of the path broken.
    expect(sh(B + "a" + B + "b" + B + "c")).toBe("/a/b/c");
    expect((sh(WINDOWS_PATH).match(/\//g) ?? []).length).toBe(7);
  });

  it("is a no-op on a POSIX path -- true of the bug too, so it proves nothing alone", () => {
    expect(sh("/tmp/routes-a1b2c3/sd.tar.gz")).toBe("/tmp/routes-a1b2c3/sd.tar.gz");
  });

  it("leaves a path with no separators alone", () => {
    expect(sh("sd.tar.gz")).toBe("sd.tar.gz");
  });
});

describe("q() -- shell quoting", () => {
  it("quotes only when there is a space", () => {
    expect(q("C:/Users/Daniel/sd.tar.gz")).toBe("C:/Users/Daniel/sd.tar.gz");
    expect(q("C:/Program Files/sd.tar.gz")).toBe('"C:/Program Files/sd.tar.gz"');
  });

  it("composes with sh() the way the call sites use it", () => {
    // Every execSync in ingest-routes.ts spells this q(sh(path)). Order matters:
    // sh() first, so the quoted string contains no separator a shell will eat.
    const out = q(sh("C:" + B + "Program Files" + B + "sd.tar.gz"));
    expect(out).toBe('"C:/Program Files/sd.tar.gz"');
    expect(hasBackslash(out)).toBe(false);
  });
});
