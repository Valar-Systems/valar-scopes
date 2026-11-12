// eh, B: handle grows ahead of its call site, so its exception table's call-site offsets move with it
struct EA { int v; };
extern void may_throw(int);
extern void note(int);
void handle(int x) {
  { volatile unsigned k = 0x5A5A5A5Au; (void)k; }
  try { may_throw(x); } catch (const EA &e) { note(e.v); }
}
