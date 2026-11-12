/* image-compare fixtures (docs/image-compare.md): the root every case links from. */
extern int f(int);
extern int g(int);
extern int lo_caller(int);
volatile int g_out;
void entry(void) { g_out = f(1) + g(2) + lo_caller(3); }
