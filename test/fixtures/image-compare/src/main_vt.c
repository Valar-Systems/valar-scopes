/* the vtab case's root (amendment 1j). ktab is a second read-only unit, in another object, kept by its address */
extern int s1(void), s2(void), s3(void), rs(int), rv(int);
int puts_(const char *p) { return p[0]; }
const int ktab[2] = {5, 6};
volatile int g_out;
volatile const void *g_keep;
void entry(void) { g_keep = ktab; g_out = s1() + s2() + s3() + rs(1) + rv(1); }
