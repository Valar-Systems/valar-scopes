/* the swap cases' root */
extern int sw(int);
volatile int g_out;
void entry(void) { g_out = sw(3); }
