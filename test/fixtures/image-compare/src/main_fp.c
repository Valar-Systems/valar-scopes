/* the fill cases' root, and ext */
extern int pick(int, int);
volatile int g_out;
int ext(int x) { return x + 1; }
void entry(void) { g_out = pick(1, 2); }
