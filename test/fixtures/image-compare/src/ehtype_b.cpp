// ehtype, B: only the catch type changes (EA -> EB). The code is byte-identical; only the exception table's type entry differs
struct EA { int v; };
struct EB { int v; };
extern void may_throw(int);
extern void note(int);
void handle(int x) {
  try { may_throw(x); } catch (const EB &e) { note(e.v); }
}
