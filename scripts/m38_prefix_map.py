"""M3-8's bench images only (docs/m3-push-task.md, row M3-8; envs blipscope-s3-128-m3bench-m38a/-m38b): map this PC's
PlatformIO folder out of the __FILE__ strings the core libraries embed, so a locally built image carries no account
path (blipscope-5c's binary-strings scan, 2026-11-17: 35 such strings in each image; CI's v19: 0).

COMPUTED HERE, AT BUILD TIME, from PlatformIO's own packages directory. The tracked platformio.ini never holds the path,
because writing it there would move the account name from the binary into the public repo. Both separator forms are
mapped, since the compiler embeds whichever form it was handed. Nothing here prints the path."""
import os

Import("env")  # noqa: F821  (provided by PlatformIO's SCons environment)

pk = env.subst("$PROJECT_PACKAGES_DIR")  # noqa: F821
base = os.path.dirname(pk.rstrip("\\/"))   # the PlatformIO folder that holds packages/
forms = sorted({base, base.replace("\\", "/"), base.replace("/", "\\")})
flags = []
for f in forms:
    flags += [f"-fmacro-prefix-map={f}=/pio", f"-ffile-prefix-map={f}=/pio"]
env.Append(CCFLAGS=flags)  # noqa: F821
print(f"[m38] prefix maps: {len(flags)} flags for {len(forms)} separator form(s) of the PlatformIO folder")
