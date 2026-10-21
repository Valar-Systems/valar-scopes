"""One rule for the pre: library patches when their file is not there (docs/prebuild-patches.md).

patch_async_buff.py, patch_wm_copy.py and patch_lgfx_rgb_intr.py each patch one file
under .pio/libdeps/<env>/. They used to print "not found yet; skipping" and carry on,
which lumped together two different situations:

  * the library is not a dependency of this env -- nothing to patch, the build is right
    (animtest-s3-128 does not use ESPAsyncWebServer or WiFiManager);
  * the library IS a dependency and the file is missing -- an install is broken, and
    building on would ship unpatched code (for the RGB patch, a dead-display image).

Ruled 2026-10-21: a patch that cannot apply fails the build, in CI and locally. So the
first case is an explicit line and the second is FATAL. PlatformIO installs lib_deps
before the pre: scripts run (measured on every fresh CI leg of v17 and v18), so on a
healthy tree the FATAL never fires; it is there for the tree where it would otherwise
build around a hole.

Shared by all three scripts so the rule exists once: two copies of one rule is two
rules, and the second is always the stale one (CLAUDE.md).
"""
import os


def declared_in(specs, name):
    """Does library `name` appear ANYWHERE in one of these lib_deps entries (case-insensitive)?

    Deliberately loose (ruled 2026-10-21). Every spec form counts -- `owner/Name @ ver`, a bare
    `Name`, a git or zip URL -- so the only error this rule can make is a false MATCH, which
    costs a FATAL with a remedy. The stricter last-path-segment match it replaced read a
    `https://github.com/tzapu/WiFiManager.git#v2.0.17` spec as "not a dependency": a silent
    skip, the exact failure this file exists to remove, in a new shape."""
    want = name.lower()
    for d in specs:
        spec = str(d).strip()
        if spec and not spec.startswith(";") and want in spec.lower():
            return True
    return False


def lib_declared(env, name):
    """Is library `name` in this env's resolved lib_deps (see declared_in)?"""
    deps = env.GetProjectOption("lib_deps", [])
    if isinstance(deps, str):
        deps = deps.splitlines()
    return declared_in(deps, name)


def lib_present(env, name):
    """Is the library's folder installed for this env -- pulled in by another library, say?"""
    return os.path.isdir(os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"], name))


def target(env, tag, lib, *parts):
    """The library file to patch; None when `lib` is not a dependency of this env (said so
    out loud). Exits the build when it is a dependency, or its folder is installed, and the
    file is missing."""
    pioenv = env["PIOENV"]
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), pioenv, lib, *parts)
    if os.path.isfile(path):
        return path
    if not lib_declared(env, lib) and not lib_present(env, lib):
        print("%s %s is not a dependency of %s; nothing to patch" % (tag, lib, pioenv))
        return None
    print("%s FATAL: %s is a dependency of %s but %s is missing. A patch that cannot apply "
          "fails the build (docs/prebuild-patches.md). Reinstall with "
          "`pio pkg install -e %s`, then rebuild." % (tag, lib, pioenv, path, pioenv))
    env.Exit(1)
    return None


def _selftest():
    """T2 (docs/prebuild-patches.md): declared_in on every spec form, plus an unrelated library."""
    cases = [
        (["tzapu/WiFiManager @ 2.0.17"], "WiFiManager", True),
        (["WiFiManager"], "WiFiManager", True),
        (["https://github.com/tzapu/WiFiManager.git#v2.0.17"], "WiFiManager", True),
        (["https://github.com/tzapu/WiFiManager/archive/refs/tags/v2.0.17.zip"], "WiFiManager", True),
        (["https://github.com/someone/ESPAsyncWebServer-fork.git"], "ESPAsyncWebServer", True),
        (["bblanchon/ArduinoJson @ 7.4.3", "lovyan03/LovyanGFX @ 1.2.21"], "WiFiManager", False),
        (["; tzapu/WiFiManager @ 2.0.17"], "WiFiManager", False),
    ]
    ok = True
    for specs, name, want in cases:
        got = declared_in(specs, name)
        ok &= got == want
        print("  %s %-60s %-17s want %-5s got %s" % ("ok  " if got == want else "FAIL", specs[0][:60], name, want, got))
    print("selftest " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    import sys
    sys.exit(_selftest() if sys.argv[1:] == ["--selftest"] else 2)
