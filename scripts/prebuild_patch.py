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


def lib_declared(env, name):
    """Is library `name` (the libdeps directory name) in this env's resolved lib_deps?"""
    deps = env.GetProjectOption("lib_deps", [])
    if isinstance(deps, str):
        deps = deps.splitlines()
    want = name.lower()
    for d in deps:
        spec = str(d).strip()
        if not spec or spec.startswith(";"):
            continue
        base = spec.split("@")[0].strip().rstrip("/")
        if base.split("/")[-1].lower() == want:
            return True
    return False


def target(env, tag, lib, *parts):
    """The library file to patch; None when `lib` is not a dependency of this env (said so
    out loud). Exits the build when it is a dependency and the file is missing."""
    pioenv = env["PIOENV"]
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), pioenv, lib, *parts)
    if os.path.isfile(path):
        return path
    if not lib_declared(env, lib):
        print("%s %s is not a dependency of %s; nothing to patch" % (tag, lib, pioenv))
        return None
    print("%s FATAL: %s is a dependency of %s but %s is missing. A patch that cannot apply "
          "fails the build (docs/prebuild-patches.md). Reinstall with "
          "`pio pkg install -e %s`, then rebuild." % (tag, lib, pioenv, path, pioenv))
    env.Exit(1)
    return None
