/* the lstr case's root (amendment 1k) */
extern int lsr(void), lcs(void);
volatile int g_out;
void entry(void) { g_out = lsr() + lcs(); }
