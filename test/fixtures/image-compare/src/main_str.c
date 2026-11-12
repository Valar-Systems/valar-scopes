/* the strpool case's root */
extern int use(int);
volatile int g_out;
void entry(void) { g_out = use(1); }
