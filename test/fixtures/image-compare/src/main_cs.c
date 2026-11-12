/* the callswap case's root (amendment 1i): fa and fb both kept (addresses taken), so only caller's target differs */
extern int caller(int);
extern int fa(int);
extern int fb(int);
volatile int g_out;
volatile void *g_keep[2];
void entry(void) { g_keep[0] = (void *)fa; g_keep[1] = (void *)fb; g_out = caller(3); }
