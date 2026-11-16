// inittry (amendment 1g), A: a static initializer with an inlined try region: .text.startup.<name> + its own exception table
extern int may_throw_i(int);
static inline int init_value() {
  try { return may_throw_i(1); } catch (...) { return -1; }
}
int g_val = init_value();
