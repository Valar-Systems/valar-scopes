/* the wrap case's root (amendment 1j): lo_w calls hw, which the link wraps */
extern int lo_w(int);
volatile int g_out;
void entry(void) { g_out = lo_w(4); }
