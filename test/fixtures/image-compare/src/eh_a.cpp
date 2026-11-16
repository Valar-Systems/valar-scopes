// eh, A (amendment 1e): a function with a try/catch, so it has its own .gcc_except_table
struct EA { int v; };
extern void may_throw(int);
extern void note(int);
void handle(int x) {
  try { may_throw(x); } catch (const EA &e) { note(e.v); }
}
