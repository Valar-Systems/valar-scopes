/* the rodread case's root (amendment 1i) */
extern int rd(int);
extern int rk(int);
extern int rl(int);
volatile int g_out;
void entry(void) { g_out = rd(1) + rk(2) + rl(3); }
