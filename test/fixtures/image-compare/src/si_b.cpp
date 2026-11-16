// inittry, B: the initializer grows ahead of its try region, so its table's call-site offsets move
extern int may_throw_i(int);
static inline int init_value() {
  { volatile unsigned k = 0x5A5A5A5Au; (void)k; }
  try { return may_throw_i(1); } catch (...) { return -1; }
}
int g_val = init_value();
