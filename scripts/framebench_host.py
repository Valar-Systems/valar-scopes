# THE FRAME BENCH's pre-build step (docs/frame-time-bench.md, A4), for env:blipscope-s3-128-framebench only.
# The bench host's address comes from the UNTRACKED environment variable FRAME_BENCH_HOST ("host" or "host:port") and
# is passed in as the FRAME_BENCH_HOST macro. It is never written to a tracked file and never printed: a refusal names
# the variable, not its value.
import os
import re

Import("env")  # noqa: F821 (SCons)

host = os.environ.get("FRAME_BENCH_HOST", "")
if not re.fullmatch(r"[A-Za-z0-9.\-]{1,63}(:[0-9]{1,5})?", host):
    raise SystemExit("framebench_host.py: FRAME_BENCH_HOST is unset or not host[:port]; refusing to build the frame bench")
env.Append(CPPDEFINES=[("FRAME_BENCH_HOST", env.StringifyMacro(host))])  # noqa: F821
