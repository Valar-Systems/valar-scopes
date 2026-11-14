# rehearse_n6: the step-count row, RECORDED OFF

**A count and its own list are two claims.**

- **Predicted** (rehearse_n6.pred 65790718571c, "STEP COUNTS"): 48 steps; NEW 33 (21 exit 1, 12 exit 0), OLD 15 (7 exit 1, 8 exit 0).
- **Read** (rehearse_n6.out d38939bd5f8e at 73b99ba5): 50 steps; NEW 33 (21 / 12), as predicted; **OLD 17 (7 / 10)**:
  O0 1, O1 2, O2a 4, O2b 4, O3a 1, O3b 1, O3c 3, O3d 1.
- **The cause:** the frozen count disagreed with the same file's frozen per-step list, which names all 17 OLD steps. The run
  matched the list. The count was typed by hand rather than derived from the list.
- **The alternative, excluded:** "the script ran steps it was not meant to". Every per-step row the prediction lists read as
  predicted, and the script's own `step` lines number 50.
- **The ruling** (the orchestrator, 2026-11-14 after 07:11:48Z): recorded OFF, NOT re-graded, NOT re-run. A re-run is
  deterministic and would measure nothing. The rehearsal's verdict rests on the per-step rows, the stronger claim. This is the
  table's first off row; a second goes to the owner.
- **The fix, for every registration from now on:** a count is derived from the list by the script that writes the prediction,
  never typed beside it.
