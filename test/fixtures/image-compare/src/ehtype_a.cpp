// ehtype, A: catches EA (B catches EB, with byte-identical code)
struct EA { int v; };
struct EB { int v; };
extern void may_throw(int);
extern void note(int);
void handle(int x) {
  try { may_throw(x); } catch (const EA &e) { note(e.v); }
}
