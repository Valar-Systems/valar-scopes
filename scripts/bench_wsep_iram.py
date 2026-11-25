"""THE FRAME BENCH (REG-W-sep Corrections 3 and 4; the separation env blipscope-s3-128-fp-wsep only, never merged).

Reads scripts/wsep_iram.ld between the framework's memory.ld and sections.ld, the one order ld 2.43.1 accepts for its
INSERT (after sections.ld it says ".iram0.text not found for insert"). Refuses the build, rather than linking an image
whose copy Wi is not in IRAM, if sections.ld is not a -T operand of LINKFLAGS, if memory.ld does not precede it, or if
the script is missing. Whether Wi really landed in IRAM is E-IRAM's question, read from the ELF.
"""
import os

Import("env")  # noqa: F821  (injected by SCons)

LD = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "wsep_iram.ld")  # noqa: F821


def _refuse(why):
    print("[wsep] REFUSED: %s -- the separation image would link without Wi in IRAM" % why)
    env.Exit(1)  # noqa: F821


if not os.path.isfile(LD):
    _refuse("%s not found" % LD)
flags = list(env["LINKFLAGS"])  # noqa: F821
if "sections.ld" not in flags:
    _refuse("sections.ld is not in LINKFLAGS")
i = flags.index("sections.ld")
if i < 1 or flags[i - 1] != "-T":
    _refuse("sections.ld is not a -T operand")
if "memory.ld" not in flags[:i]:
    _refuse("memory.ld does not precede sections.ld")
flags[i - 1:i - 1] = ["-T", LD]
env.Replace(LINKFLAGS=flags)  # noqa: F821
print("[wsep] IRAM insert read before sections.ld: %s" % LD)
