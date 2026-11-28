"""THE v20 SIBLING's FT pre-build step (docs/frame-time-bench.md, "FT on the sibling", B; PlatformIO `pre:` script,
in the two FT envs only). Before anything compiles it:
  1. reads the env's pin list (custom_ft_pins) with scripts/ft_packages.py, the one reader of pins;
  2. reads each pinned package from the UNTRACKED directory named by FRAME_BENCH_PKG_DIR and runs FT-PKG (present,
     length, sha256, the BMPK magic) and FT-CTR (its own centre at exactly 2 dp) on its bytes;
  3. requires the env's board_build.embed_files to be exactly .pio/ftpkg/<POINT>-<variant>.bmpk for each pin, in the
     pin list's order (FrameBenchFt.h's table assumes that order);
  4. copies the verified bytes to .pio/ftpkg/, and removes anything else there.
Any failure stops the build (exit 1) and names the pin: a substitute is never built in. It prints names, booleans and
counts only, never a coordinate."""
import importlib.util
import os

Import("env")  # noqa: F821 -- PlatformIO's SCons environment

PROJ = env.subst("$PROJECT_DIR")  # noqa: F821
_spec = importlib.util.spec_from_file_location("ft_packages", os.path.join(PROJ, "scripts", "ft_packages.py"))
ftp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ftp)


def fail(msg):
    print(f"FT PRE-BUILD: REFUSED: {msg}")
    env.Exit(1)  # noqa: F821


pins_rel = env.GetProjectOption("custom_ft_pins", "")  # noqa: F821
if not pins_rel:
    fail("this env names no custom_ft_pins")
try:
    pins = ftp.read_pins(os.path.join(PROJ, pins_rel))
except ftp.Stop as e:
    fail(str(e))
src_dir = os.environ.get("FRAME_BENCH_PKG_DIR", "")
if not src_dir or not os.path.isdir(src_dir):
    fail("FRAME_BENCH_PKG_DIR is not set to a directory (the packages are untracked by design)")

want = [f".pio/ftpkg/{p['point']}-{p['variant']}.bmpk" for p in pins]
raw = env.GetProjectOption("board_build.embed_files", "")  # noqa: F821
got = [x.strip() for x in (raw if isinstance(raw, str) else "\n".join(raw)).split() if x.strip()]
if got != want:
    fail(f"board_build.embed_files is not the pin list in its order ({len(got)} listed, {len(want)} pinned)")

blobs = []
for p in pins:
    data = ftp.load(src_dir, p)
    pkg, ctr, detail = ftp.judge(p, data)
    if not (pkg and ctr):
        fail(f"{p['point']} {p['variant']} ({p['sha'][:12]}): FT-PKG {pkg}, FT-CTR {ctr}; {detail}")
    blobs.append((p, data))

out = os.path.join(PROJ, ".pio", "ftpkg")
os.makedirs(out, exist_ok=True)
keep = set()
for p, data in blobs:
    name = f"{p['point']}-{p['variant']}.bmpk"
    keep.add(name)
    dst = os.path.join(out, name)
    if not (os.path.isfile(dst) and open(dst, "rb").read() == data):
        with open(dst, "wb") as f:
            f.write(data)
for name in os.listdir(out):
    if name not in keep:
        os.remove(os.path.join(out, name))
print(f"FT PRE-BUILD: {len(pins)} pins from {os.path.basename(pins_rel)}: FT-PKG and FT-CTR ok on every one; "
      f"copied to .pio/ftpkg/ in pin order")
