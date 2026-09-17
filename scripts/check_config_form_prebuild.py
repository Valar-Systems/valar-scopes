"""Pre-build: run the config-page gate at the BENCH, not only in CI.

CI catches this on the way to main. The person most likely to flash a board
WITHOUT ever touching CI is whoever is standing at the bench with a USB cable --
and that is exactly the path by which a blank config page reaches real hardware.
A gate that only runs on the way to main does not protect the boards that never
go that way.

Deliberately a SUBPROCESS rather than an import. check-config-form.py ends in
`sys.exit(main())`, so importing it into SCons would raise SystemExit and stop
the build on success as well as failure. Running it as a child process keeps
that script a plain CLI tool with one behaviour, instead of one that has to know
whether it is being built or invoked.

Its output is passed through UNFILTERED. A filter is written against the output
you expect, so it is blindest exactly when the command fails -- and the failure
text here is the part that names which literal drifted.
"""
import os
import subprocess
import sys

Import("env")  # noqa: F821  (provided by PlatformIO's SCons environment)

PROJECT = env.subst("$PROJECT_DIR")  # noqa: F821
SCRIPT = os.path.join(PROJECT, "scripts", "check-config-form.py")

if not os.path.isfile(SCRIPT):
    # Loud rather than silent. A gate that quietly stops existing is worse than
    # no gate, because the build still goes green and nobody re-reads this file.
    print("check-config-form: %s NOT FOUND -- the config-page gate did not run" % SCRIPT)
else:
    result = subprocess.run([sys.executable, SCRIPT], cwd=PROJECT)
    if result.returncode != 0:
        print("")
        print("check-config-form FAILED (exit %d). Refusing to build." % result.returncode)
        print("Exit 1 is a real violation; exit 2 means the checker could not")
        print("verify and is refusing to certify -- neither is a pass.")
        env.Exit(1)  # noqa: F821
