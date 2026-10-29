"""Build Q's board run (docs/weatherscope-stage3.md A12.25): run_b35.py's whole A9.19 sequence in one open, then 'Q'
and its lines, before the close.

    <penv python> -I tools/weatherscope/board/run_q.py <COMx> <board host> <empty out dir>

Resolve the port from the board's USB serial first (scripts/board-port.py weatherscope --upload), never by number.
After B3: 'Q', then every line until "[wx] qdec done" (30 s). The grade is grade_b35.py's (unchanged) and
grade_q.py's. Prints no coordinate.
"""
import importlib.util, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("run_b35", os.path.join(HERE, "run_b35.py"))
rb35 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rb35)


def q_step(tr, out):
    time.sleep(1.0)
    t0 = time.time()
    tr.note("#HOST Q: sending 'Q'")
    tr.send(b"Q")
    t, l = tr.wait(r"^\[wx\] qdec done", t0, 30)
    tr.note(f"#HOST Q: {'done' if l else 'NO qdec done line in 30 s'}")
    time.sleep(0.5)


if __name__ == "__main__":
    sys.exit(rb35.main(sys.argv, after_b3=q_step))
