/* the offset case's root */
struct S;
extern int get(const struct S *);
volatile int g_out;
static int buf[4];
void entry(void) { g_out = get((const struct S *)buf); }
