// routes-tmp-signal.fixture.mts -- TESTS ONLY (SG1, SG2 in routes-guards.selftest.mts; docs/route-weekly-code.md 2h, M3).
// Makes one tmpDir, prints its path, then delivers <signal> to itself with process.emit, so the handler is exercised on
// Windows too (where an OS SIGTERM terminates a process without running handlers). If no handler ends the process, it
// kills itself hard (no exit handlers run), so the dir is LEFT and the exit code is not 130/143: the row reads red.
import { tmpDir } from "./routes-tmp";

const sig = process.argv[2] as "SIGINT" | "SIGTERM";
const d = tmpDir("routes-kl-");
process.stdout.write(d + "\n");
process.emit(sig);
process.kill(process.pid, "SIGKILL");
