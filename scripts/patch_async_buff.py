"""Pre-build patch: make ESPAsyncWebServer's send-buffer size overridable.

The library hard-#defines its per-response send buffer as CONFIG_LWIP_TCP_MSS * 2
(~2872 B) with no include guard, so the -DASYNC_RESPONCE_BUFF_SIZE flag in
platformio.ini can't take effect. On the heap-tight single-core ESP32-C3 that buffer
is often too large to allocate in a fragmented heap, and the configuration web page
then silently fails to send (the browser just times out). Wrapping the define in
#ifndef lets the build flag win.

This runs on every build and is idempotent, so the override survives a fresh `pio`
library install -- the .pio/ tree is gitignored and not part of the repo.
"""
import os

Import("env")  # noqa: F821  (provided by PlatformIO's SCons environment)

HEADER = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],  # noqa: F821
    "ESPAsyncWebServer", "src", "WebResponseImpl.h",
)

GUARD = "#ifndef ASYNC_RESPONCE_BUFF_SIZE"
ORIGINAL = "#define ASYNC_RESPONCE_BUFF_SIZE CONFIG_LWIP_TCP_MSS * 2"
REPLACEMENT = (
    "#ifndef ASYNC_RESPONCE_BUFF_SIZE\n"
    "#define ASYNC_RESPONCE_BUFF_SIZE CONFIG_LWIP_TCP_MSS * 2\n"
    "#endif"
)

try:
    with open(HEADER, "r", encoding="utf-8") as fh:
        text = fh.read()
except FileNotFoundError:
    print("[patch_async_buff] %s not found yet; skipping" % HEADER)
else:
    if GUARD in text:
        pass  # already guarded
    elif ORIGINAL in text:
        with open(HEADER, "w", encoding="utf-8") as fh:
            fh.write(text.replace(ORIGINAL, REPLACEMENT, 1))
        print("[patch_async_buff] guarded ASYNC_RESPONCE_BUFF_SIZE in WebResponseImpl.h")
    else:
        # FATAL: neither the guard nor the original define is present, so the
        # -DASYNC_RESPONCE_BUFF_SIZE=1024 override in platformio.ini would be silently
        # defeated (the config web page then fails to send on a fragmented heap). Fail
        # the build rather than ship it -- the library changed; update this anchor.
        print("[patch_async_buff] FATAL: expected define not found; ESPAsyncWebServer "
              "changed shape and the buffer override would be ignored. Update this patch.")
        env.Exit(1)  # noqa: F821
