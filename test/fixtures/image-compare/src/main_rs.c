/* the rod* cases' root: f0 and f2 weak and only address-taken -- a CALL to an undefined weak symbol makes the
   linker rewrite the long call (rodshift's first reading, 19:04Z: "UNDECIDED entry ... neither relaxed nor kept") */
extern int f1(void);
extern int f0(void) __attribute__((weak));
extern int f2(void) __attribute__((weak));
volatile int g_out;
volatile void *g_keep[2];
void entry(void) { g_keep[0] = (void *)f0; g_keep[1] = (void *)f2; g_out = f1(); }
